#include "Epidemic/GameFramework/TraversalNavigationConstructionIntegration/traversal_navigation_construction_adapters.h"

#include <cstring>
#include <type_traits>

namespace epidemic::gameplay::traversal_navigation_construction
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

constexpr auto NavigationOutputType = construction::PlacementOutputTypeId::FromString("construction.output.navigation_layer");
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
    navigation_semantics::NavigationLinkId link) const
{
    auto nav_result = navigation.CanUseLink(subject, link);
    if (nav_result.decision == navigation_semantics::NavigationDecisionKind::Deny)
        return nav_result;

    const auto *semantic_link = navigation.FindLink(link);
    if (semantic_link == nullptr)
    {
        nav_result.decision = navigation_semantics::NavigationDecisionKind::Deny;
        nav_result.availability = navigation_semantics::NavigationAvailability::PhysicallyImpossible;
        return nav_result;
    }
    const auto *state = traversal.FindState(subject);
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
    static_assert(std::is_trivially_copyable_v<ConstructionNavigationLayerPayload>);
    std::vector<std::byte> bytes(sizeof(payload));
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    return bytes;
}

foundation::Result<navigation_semantics::NavigationSemanticLayer> ConstructionNavigationAdapter::DecodeLayer(
    const construction::PlacementOutputOperation &output,
    navigation_semantics::NavigationLayerId stable_id) const
{
    if (output.type != NavigationOutputType || !output.subject.IsValid() ||
        output.payload.size() != sizeof(ConstructionNavigationLayerPayload))
        return foundation::Result<navigation_semantics::NavigationSemanticLayer>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation output is malformed"));

    ConstructionNavigationLayerPayload payload{};
    std::memcpy(&payload, output.payload.data(), sizeof(payload));
    if (!payload.layer_type.IsValid() || payload.multiplier_micro < 0 || payload.required_parameter_micro < 0)
        return foundation::Result<navigation_semantics::NavigationSemanticLayer>::Failure(
            Error("gameplay.integration.construction_navigation_payload_invalid",
                  "construction navigation output contains invalid semantics"));

    navigation_semantics::NavigationSemanticLayer layer;
    layer.id = stable_id;
    layer.area = output.subject;
    layer.type = payload.layer_type;
    layer.decision = payload.decision;
    layer.lifetime = payload.lifetime;
    layer.priority = payload.priority;
    layer.additive_cost_micro = payload.additive_cost_micro;
    layer.multiplier_micro = payload.multiplier_micro;
    layer.requirement = payload.requirement;
    layer.required_parameter_micro = payload.required_parameter_micro;
    return foundation::Result<navigation_semantics::NavigationSemanticLayer>::Success(std::move(layer));
}

foundation::Result<void> ConstructionNavigationAdapter::ApplyPlacementOutputs(
    const construction::PlacementCommitResult &result,
    navigation_semantics::NavigationSemanticsService &navigation,
    GameplayContext context) const
{
    for (const auto &output : result.outputs)
    {
        if (output.type != NavigationOutputType)
            continue;
        auto decoded = DecodeLayer(output);
        if (!decoded)
            return foundation::Result<void>::Failure(decoded.GetError());
        auto added = navigation.AddLayer(std::move(decoded).Value(), context);
        if (!added)
            return foundation::Result<void>::Failure(added.GetError());
    }
    return foundation::Result<void>::Success();
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
        const auto stable_id = navigation_semantics::NavigationLayerId::FromRaw(entry.id.value.High(), entry.id.value.Low());
        if (!navigation.FindLayer(stable_id))
        {
            auto decoded = DecodeLayer(entry.operation, stable_id);
            if (!decoded)
                return foundation::Result<std::size_t>::Failure(decoded.GetError());
            auto added = navigation.AddLayer(std::move(decoded).Value(), context);
            if (!added)
                return foundation::Result<std::size_t>::Failure(added.GetError());
        }
        auto acknowledged = construction.AcknowledgeOutput(entry.id, context);
        if (!acknowledged)
            return foundation::Result<std::size_t>::Failure(acknowledged.GetError());
        ++processed;
    }
    return foundation::Result<std::size_t>::Success(processed);
}

foundation::Result<void> ConstructionTraversalAdapter::CancelTraversalSessionsBlockedByDestroyedLink(
    traversal::TraversalService &traversal,
    navigation_semantics::NavigationLinkId link,
    std::span<const traversal::TraversalSessionId> affected_sessions,
    GameplayContext context) const
{
    (void)link;
    const auto reason = traversal::TraversalReasonId::FromString("traversal.route_invalidated");
    for (const auto session : affected_sessions)
    {
        auto cancelled = traversal.CancelSession(session, reason, context);
        if (!cancelled)
            return cancelled;
    }
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::traversal_navigation_construction
