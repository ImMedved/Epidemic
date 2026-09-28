#include "Epidemic/GameFramework/TraversalNavigationConstructionIntegration/traversal_navigation_construction_adapters.h"

#include <bit>
#include <limits>

namespace epidemic::gameplay::traversal_navigation_construction
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

constexpr auto NavigationOutputType = construction::PlacementOutputTypeId::FromString("construction.output.navigation_layer");
constexpr auto ConstructionPlacedObjectDomain = GameplayDomainId::FromString("framework.construction.placed_object");
constexpr std::uint16_t NavigationPayloadVersion = 1;
constexpr std::size_t NavigationPayloadSize = 70;

constexpr std::uint64_t Mix64(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

void AppendU8(std::vector<std::byte>& bytes, std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void AppendU16(std::vector<std::byte>& bytes, std::uint16_t value)
{
    for (unsigned shift = 0; shift < 16; shift += 8)
        AppendU8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void AppendU32(std::vector<std::byte>& bytes, std::uint32_t value)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        AppendU8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void AppendU64(std::vector<std::byte>& bytes, std::uint64_t value)
{
    for (unsigned shift = 0; shift < 64; shift += 8)
        AppendU8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

void AppendI32(std::vector<std::byte>& bytes, std::int32_t value)
{
    AppendU32(bytes, std::bit_cast<std::uint32_t>(value));
}

void AppendI64(std::vector<std::byte>& bytes, std::int64_t value)
{
    AppendU64(bytes, std::bit_cast<std::uint64_t>(value));
}

[[nodiscard]] bool ReadU8(std::span<const std::byte> bytes, std::size_t& offset, std::uint8_t& value) noexcept
{
    if (offset >= bytes.size())
        return false;
    value = std::to_integer<std::uint8_t>(bytes[offset++]);
    return true;
}

[[nodiscard]] bool ReadU16(std::span<const std::byte> bytes, std::size_t& offset, std::uint16_t& value) noexcept
{
    if (bytes.size() - offset < 2)
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 16; shift += 8)
        value |= static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset++])) << shift;
    return true;
}

[[nodiscard]] bool ReadU32(std::span<const std::byte> bytes, std::size_t& offset, std::uint32_t& value) noexcept
{
    if (bytes.size() - offset < 4)
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset++])) << shift;
    return true;
}

[[nodiscard]] bool ReadU64(std::span<const std::byte> bytes, std::size_t& offset, std::uint64_t& value) noexcept
{
    if (bytes.size() - offset < 8)
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset++])) << shift;
    return true;
}

[[nodiscard]] bool ReadI32(std::span<const std::byte> bytes, std::size_t& offset, std::int32_t& value) noexcept
{
    std::uint32_t raw = 0;
    if (!ReadU32(bytes, offset, raw))
        return false;
    value = std::bit_cast<std::int32_t>(raw);
    return true;
}

[[nodiscard]] bool ReadI64(std::span<const std::byte> bytes, std::size_t& offset, std::int64_t& value) noexcept
{
    std::uint64_t raw = 0;
    if (!ReadU64(bytes, offset, raw))
        return false;
    value = std::bit_cast<std::int64_t>(raw);
    return true;
}

[[nodiscard]] bool IsValidOperation(std::uint8_t raw) noexcept
{
    return raw == static_cast<std::uint8_t>(ConstructionNavigationOperation::Add) ||
           raw == static_cast<std::uint8_t>(ConstructionNavigationOperation::Update) ||
           raw == static_cast<std::uint8_t>(ConstructionNavigationOperation::Remove);
}

[[nodiscard]] bool IsValidDecision(std::uint8_t raw) noexcept
{
    return raw <= static_cast<std::uint8_t>(navigation_semantics::NavigationDecisionKind::Prefer);
}

[[nodiscard]] bool IsValidLifetime(std::uint8_t raw) noexcept
{
    return raw <= static_cast<std::uint8_t>(navigation_semantics::NavigationLayerLifetime::Timed);
}

[[nodiscard]] bool SameLayerSemantics(const navigation_semantics::NavigationSemanticLayer& current,
                                      const navigation_semantics::NavigationSemanticLayer& desired) noexcept
{
    return current.id == desired.id && current.area == desired.area && current.source == desired.source &&
           current.type == desired.type && current.priority == desired.priority &&
           current.lifetime == desired.lifetime && current.decision == desired.decision &&
           current.additive_cost_micro == desired.additive_cost_micro &&
           current.multiplier_micro == desired.multiplier_micro && current.tags.Values().empty() &&
           current.requirement == desired.requirement &&
           current.required_parameter_micro == desired.required_parameter_micro &&
           current.expires_at == desired.expires_at;
}
} // namespace

bool TraversalNavigationCapabilityProvider::HasCapability(GameplayObjectRef subject, TypeId capability,
                                                          navigation_semantics::Fixed min_parameter_micro) const noexcept
{
    return traversal_.HasCapability(subject, traversal::TraversalCapabilityId{capability}, min_parameter_micro);
}

navigation_semantics::NavigationPermissionResult TraversalNavigationAdapter::CanUseLink(
    const traversal::TraversalService &traversal,
    const navigation_semantics::NavigationSemanticsService &navigation,
    GameplayObjectRef subject,
    navigation_semantics::NavigationLinkId link,
    const GameplayContext &context) const
{
    const auto *state = traversal.FindState(subject);
    const auto traversal_mode = state != nullptr
                                    ? navigation_semantics::TraversalModeSemanticId{state->current_mode.value}
                                    : navigation_semantics::TraversalModeSemanticId{};

    // NavigationSemantics::CanUseLink is the canonical link-aware evaluator. It forwards the complete
    // GameplayContext to the same evaluator used by path queries and also enforces the concrete link state.
    auto nav_result = navigation.CanUseLink(subject, link, traversal_mode, context);
    if (nav_result.decision == navigation_semantics::NavigationDecisionKind::Deny)
        return nav_result;

    if (state == nullptr ||
        traversal.CanUseMode(subject, state->current_mode).kind == traversal::TraversalResultKind::Rejected)
    {
        nav_result.decision = navigation_semantics::NavigationDecisionKind::Deny;
        nav_result.availability = navigation_semantics::NavigationAvailability::TraversalUnsupported;
        nav_result.reasons.push_back({navigation_semantics::NavigationReasonId::FromString("navigation.traversal_mode_rejected"),
                                      {}, {}, link, navigation_semantics::NavigationDecisionKind::Deny});
    }
    return nav_result;
}

std::vector<std::byte> EncodeNavigationLayerPayload(const ConstructionNavigationLayerPayload &payload)
{
    std::vector<std::byte> bytes;
    bytes.reserve(NavigationPayloadSize);
    AppendU8(bytes, static_cast<std::uint8_t>('E'));
    AppendU8(bytes, static_cast<std::uint8_t>('N'));
    AppendU8(bytes, static_cast<std::uint8_t>('A'));
    AppendU8(bytes, static_cast<std::uint8_t>('V'));
    AppendU16(bytes, NavigationPayloadVersion);
    AppendU8(bytes, static_cast<std::uint8_t>(payload.operation));
    AppendU8(bytes, static_cast<std::uint8_t>(payload.decision));
    AppendU8(bytes, static_cast<std::uint8_t>(payload.lifetime));
    AppendU8(bytes, payload.expires_at.has_value() ? 1u : 0u);
    AppendU64(bytes, payload.source_key.Raw());
    AppendU64(bytes, payload.layer_type.value.Raw());
    AppendI32(bytes, payload.priority);
    AppendI64(bytes, payload.additive_cost_micro);
    AppendI64(bytes, payload.multiplier_micro);
    AppendU64(bytes, payload.requirement.Raw());
    AppendI64(bytes, payload.required_parameter_micro);
    AppendI64(bytes, payload.expires_at ? payload.expires_at->ticks : 0);
    return bytes;
}

navigation_semantics::NavigationLayerId NavigationLayerIdFor(construction::PlacedObjectId placed_object,
                                                              TypeId source_key) noexcept
{
    if (!placed_object.IsValid() || !source_key.IsValid())
        return {};
    auto high = Mix64(placed_object.value.High() ^ source_key.Raw() ^ 0x4E41564C41594552ull);
    auto low = Mix64(placed_object.value.Low() ^ source_key.Raw() ^ 0x434F4E5354525543ull);
    if (high == 0 && low == 0)
        low = 1;
    return navigation_semantics::NavigationLayerId::FromRaw(high, low);
}

foundation::Result<ConstructionNavigationLayerPayload> ConstructionNavigationAdapter::DecodePayload(
    const construction::PlacementOutputOperation &output) const
{
    if (output.type != NavigationOutputType || !output.placed_record.IsValid() ||
        output.payload.size() != NavigationPayloadSize)
        return foundation::Result<ConstructionNavigationLayerPayload>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation output is malformed"));

    const std::span<const std::byte> bytes(output.payload);
    std::size_t offset = 0;
    std::uint8_t magic_e = 0, magic_n = 0, magic_a = 0, magic_v = 0;
    std::uint16_t version = 0;
    std::uint8_t operation = 0, decision = 0, lifetime = 0, flags = 0;
    std::uint64_t source_key = 0, layer_type = 0, requirement = 0;
    std::int32_t priority = 0;
    std::int64_t additive = 0, multiplier = 0, required_parameter = 0, expires_at = 0;

    if (!ReadU8(bytes, offset, magic_e) || !ReadU8(bytes, offset, magic_n) ||
        !ReadU8(bytes, offset, magic_a) || !ReadU8(bytes, offset, magic_v) ||
        !ReadU16(bytes, offset, version) || !ReadU8(bytes, offset, operation) ||
        !ReadU8(bytes, offset, decision) || !ReadU8(bytes, offset, lifetime) ||
        !ReadU8(bytes, offset, flags) || !ReadU64(bytes, offset, source_key) ||
        !ReadU64(bytes, offset, layer_type) || !ReadI32(bytes, offset, priority) ||
        !ReadI64(bytes, offset, additive) || !ReadI64(bytes, offset, multiplier) ||
        !ReadU64(bytes, offset, requirement) || !ReadI64(bytes, offset, required_parameter) ||
        !ReadI64(bytes, offset, expires_at) || offset != bytes.size() ||
        magic_e != static_cast<std::uint8_t>('E') || magic_n != static_cast<std::uint8_t>('N') ||
        magic_a != static_cast<std::uint8_t>('A') || magic_v != static_cast<std::uint8_t>('V') ||
        version != NavigationPayloadVersion || !IsValidOperation(operation) || !IsValidDecision(decision) ||
        !IsValidLifetime(lifetime) || (flags & ~1u) != 0u)
        return foundation::Result<ConstructionNavigationLayerPayload>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation output schema is invalid or unsupported"));

    ConstructionNavigationLayerPayload payload;
    payload.operation = static_cast<ConstructionNavigationOperation>(operation);
    payload.source_key = TypeId::FromRaw(source_key);
    payload.layer_type = navigation_semantics::NavigationLayerTypeId{TypeId::FromRaw(layer_type)};
    payload.decision = static_cast<navigation_semantics::NavigationDecisionKind>(decision);
    payload.lifetime = static_cast<navigation_semantics::NavigationLayerLifetime>(lifetime);
    payload.priority = priority;
    payload.additive_cost_micro = additive;
    payload.multiplier_micro = multiplier;
    payload.requirement = TypeId::FromRaw(requirement);
    payload.required_parameter_micro = required_parameter;
    if ((flags & 1u) != 0u)
        payload.expires_at = GameplayTimePoint{expires_at};

    if (!payload.source_key.IsValid() || payload.multiplier_micro < 0 || payload.required_parameter_micro < 0)
        return foundation::Result<ConstructionNavigationLayerPayload>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation output contains invalid semantics"));
    if (payload.operation != ConstructionNavigationOperation::Remove)
    {
        if (!output.subject.IsValid() || !payload.layer_type.IsValid() ||
            (payload.lifetime == navigation_semantics::NavigationLayerLifetime::Timed && !payload.expires_at) ||
            ((payload.decision == navigation_semantics::NavigationDecisionKind::RequireCapability ||
              payload.decision == navigation_semantics::NavigationDecisionKind::RequireFact) &&
             !payload.requirement.IsValid()))
            return foundation::Result<ConstructionNavigationLayerPayload>::Failure(
                Error("gameplay.integration.construction_navigation_payload_invalid",
                      "construction navigation add/update semantics are incomplete"));
    }
    return foundation::Result<ConstructionNavigationLayerPayload>::Success(payload);
}

foundation::Result<navigation_semantics::NavigationSemanticLayer> ConstructionNavigationAdapter::BuildLayer(
    const construction::PlacementOutputOperation &output,
    const ConstructionNavigationLayerPayload &payload,
    navigation_semantics::NavigationLayerId stable_id) const
{
    if (!stable_id.IsValid() || payload.operation == ConstructionNavigationOperation::Remove)
        return foundation::Result<navigation_semantics::NavigationSemanticLayer>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation layer cannot be built from this output"));

    navigation_semantics::NavigationSemanticLayer layer;
    layer.id = stable_id;
    layer.area = output.subject;
    layer.source = GameplayObjectRef{ConstructionPlacedObjectDomain, output.placed_record.value};
    layer.type = payload.layer_type;
    layer.decision = payload.decision;
    layer.lifetime = payload.lifetime;
    layer.priority = payload.priority;
    layer.additive_cost_micro = payload.additive_cost_micro;
    layer.multiplier_micro = payload.multiplier_micro;
    layer.requirement = payload.requirement;
    layer.required_parameter_micro = payload.required_parameter_micro;
    layer.expires_at = payload.expires_at;
    return foundation::Result<navigation_semantics::NavigationSemanticLayer>::Success(std::move(layer));
}

foundation::Result<construction::PlacementOutputId> ConstructionNavigationAdapter::QueueNavigationOperation(
    construction::ConstructionService &construction,
    construction::PlacedObjectId placed_object,
    GameplayObjectRef area,
    const ConstructionNavigationLayerPayload &payload,
    GameplayContext context) const
{
    if (!placed_object.IsValid() || !payload.source_key.IsValid() ||
        (payload.operation != ConstructionNavigationOperation::Remove && !area.IsValid()))
        return foundation::Result<construction::PlacementOutputId>::Failure(
            Error("gameplay.integration.construction_navigation_operation_invalid",
                  "construction navigation operation is missing stable identity or target area"));

    construction::PlacementOutputOperation output;
    output.type = NavigationOutputType;
    output.subject = area;
    output.placed_record = placed_object;
    output.payload = EncodeNavigationLayerPayload(payload);
    return construction.EnqueuePlacedObjectOutput(placed_object, std::move(output), context);
}

foundation::Result<std::size_t> ConstructionNavigationAdapter::ProcessPendingOutputs(
    construction::ConstructionService &construction,
    navigation_semantics::NavigationSemanticsService &navigation,
    GameplayContext context) const
{
    std::size_t processed = 0;
    for (const auto &entry : construction.PendingOutputs())
    {
        if (entry.operation.type != NavigationOutputType)
            continue;

        auto decoded = DecodePayload(entry.operation);
        if (!decoded)
            return foundation::Result<std::size_t>::Failure(decoded.GetError());
        const auto &payload = decoded.Value();
        const auto stable_id = NavigationLayerIdFor(entry.operation.placed_record, payload.source_key);
        if (!stable_id.IsValid())
            return foundation::Result<std::size_t>::Failure(
                Error("gameplay.integration.construction_navigation_identity_invalid",
                      "construction navigation output has no stable layer identity"));

        const auto *existing = navigation.FindLayer(stable_id);
        if (payload.operation == ConstructionNavigationOperation::Remove)
        {
            // Missing means the same remove was already delivered, so acknowledgement is safe.
            if (existing != nullptr)
            {
                auto removed = navigation.RemoveLayer(stable_id, context);
                if (!removed)
                    return foundation::Result<std::size_t>::Failure(removed.GetError());
            }
        }
        else
        {
            auto built = BuildLayer(entry.operation, payload, stable_id);
            if (!built)
                return foundation::Result<std::size_t>::Failure(built.GetError());
            auto desired = std::move(built).Value();

            if (payload.operation == ConstructionNavigationOperation::Add)
            {
                if (existing == nullptr)
                {
                    auto added = navigation.AddLayer(std::move(desired), context);
                    if (!added)
                        return foundation::Result<std::size_t>::Failure(added.GetError());
                }
                else if (!SameLayerSemantics(*existing, desired))
                {
                    return foundation::Result<std::size_t>::Failure(
                        Error("gameplay.integration.construction_navigation_add_conflict",
                              "construction navigation add conflicts with the existing stable layer"));
                }
            }
            else
            {
                if (existing == nullptr)
                    return foundation::Result<std::size_t>::Failure(
                        Error("gameplay.integration.construction_navigation_update_missing",
                              "construction navigation update targets a missing stable layer"));
                if (existing->lifetime != desired.lifetime)
                    return foundation::Result<std::size_t>::Failure(
                        Error("gameplay.integration.construction_navigation_update_identity_conflict",
                              "construction navigation update cannot change immutable layer lifetime"));
                if (!SameLayerSemantics(*existing, desired))
                {
                    auto updated = navigation.UpdateLayer(stable_id, std::move(desired), existing->revision, context);
                    if (!updated)
                        return foundation::Result<std::size_t>::Failure(updated.GetError());
                }
            }
        }

        auto acknowledged = construction.AcknowledgeOutput(entry.id, context);
        if (!acknowledged)
            return foundation::Result<std::size_t>::Failure(acknowledged.GetError());
        ++processed;
    }
    return foundation::Result<std::size_t>::Success(processed);
}

foundation::Result<void> ConstructionTraversalAdapter::CancelTraversalSessions(
    traversal::TraversalService &traversal,
    std::span<const traversal::TraversalSessionId> sessions,
    GameplayContext context) const
{
    const auto reason = traversal::TraversalReasonId::FromString("traversal.route_invalidated");
    for (const auto session : sessions)
    {
        // A repeated reconciliation pass is allowed after the session has already reached a terminal state.
        if (traversal.FindSession(session) == nullptr)
            continue;
        auto cancelled = traversal.CancelSession(session, reason, context);
        if (!cancelled)
            return cancelled;
    }
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::traversal_navigation_construction
