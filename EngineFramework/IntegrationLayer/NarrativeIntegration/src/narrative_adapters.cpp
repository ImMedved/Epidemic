#include "Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_set>

namespace epidemic::gameplay::narrative_integration
{
namespace
{
constexpr std::uint32_t kOutboxWireMagic = 0x314F494Eu; // NIO1, little-endian on the wire.
constexpr std::uint32_t kOutboxWireVersion = 1;
constexpr std::size_t kMaxPersistentPayloadBytes = 1024u * 1024u;

foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <class E> bool EnumInRange(E value, E max_value) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<U>(value) >= static_cast<U>(0) && static_cast<U>(value) <= static_cast<U>(max_value);
}

class ByteWriter
{
  public:
    void U8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void U32(std::uint32_t value)
    {
        for (std::uint32_t i = 0; i < 4; ++i)
            U8(static_cast<std::uint8_t>((value >> (i * 8u)) & 0xFFu));
    }
    void U64(std::uint64_t value)
    {
        for (std::uint32_t i = 0; i < 8; ++i)
            U8(static_cast<std::uint8_t>((value >> (i * 8u)) & 0xFFu));
    }
    void I64(std::int64_t value) { U64(static_cast<std::uint64_t>(value)); }
    void ObjectId(GameplayObjectId value)
    {
        U64(value.High());
        U64(value.Low());
    }
    void Correlation(CorrelationId value)
    {
        U64(value.High());
        U64(value.Low());
    }
    void Operation(OperationId value)
    {
        U64(value.High());
        U64(value.Low());
    }
    void Ref(GameplayObjectRef value)
    {
        U64(value.domain.Raw());
        ObjectId(value.id);
    }
    void Payload(std::span<const std::byte> payload)
    {
        U32(static_cast<std::uint32_t>(payload.size()));
        bytes_.insert(bytes_.end(), payload.begin(), payload.end());
    }
    [[nodiscard]] std::vector<std::byte> Take() && { return std::move(bytes_); }

  private:
    std::vector<std::byte> bytes_;
};

class ByteReader
{
  public:
    explicit ByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool U8(std::uint8_t &value)
    {
        if (position_ >= bytes_.size())
            return false;
        value = std::to_integer<std::uint8_t>(bytes_[position_++]);
        return true;
    }
    [[nodiscard]] bool U32(std::uint32_t &value)
    {
        value = 0;
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            std::uint8_t part = 0;
            if (!U8(part))
                return false;
            value |= static_cast<std::uint32_t>(part) << (i * 8u);
        }
        return true;
    }
    [[nodiscard]] bool U64(std::uint64_t &value)
    {
        value = 0;
        for (std::uint32_t i = 0; i < 8; ++i)
        {
            std::uint8_t part = 0;
            if (!U8(part))
                return false;
            value |= static_cast<std::uint64_t>(part) << (i * 8u);
        }
        return true;
    }
    [[nodiscard]] bool I64(std::int64_t &value)
    {
        std::uint64_t raw = 0;
        if (!U64(raw))
            return false;
        value = static_cast<std::int64_t>(raw);
        return true;
    }
    [[nodiscard]] bool ObjectId(GameplayObjectId &value)
    {
        std::uint64_t high = 0, low = 0;
        if (!U64(high) || !U64(low))
            return false;
        value = GameplayObjectId::FromRaw(high, low);
        return true;
    }
    [[nodiscard]] bool Correlation(CorrelationId &value)
    {
        std::uint64_t high = 0, low = 0;
        if (!U64(high) || !U64(low))
            return false;
        value = CorrelationId::FromRaw(high, low);
        return true;
    }
    [[nodiscard]] bool Operation(OperationId &value)
    {
        std::uint64_t high = 0, low = 0;
        if (!U64(high) || !U64(low))
            return false;
        value = OperationId::FromRaw(high, low);
        return true;
    }
    [[nodiscard]] bool Ref(GameplayObjectRef &value)
    {
        std::uint64_t domain = 0;
        GameplayObjectId id{};
        if (!U64(domain) || !ObjectId(id))
            return false;
        value = {GameplayDomainId::FromRaw(domain), id};
        return true;
    }
    [[nodiscard]] bool Payload(std::vector<std::byte> &payload)
    {
        std::uint32_t size = 0;
        if (!U32(size) || size > kMaxPersistentPayloadBytes || position_ + size > bytes_.size())
            return false;
        payload.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                       bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + size));
        position_ += size;
        return true;
    }
    [[nodiscard]] bool Finished() const noexcept { return position_ == bytes_.size(); }

  private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

std::int64_t ConfidenceMicro(knowledge::KnowledgeConfidence confidence) noexcept
{
    switch (confidence)
    {
    case knowledge::KnowledgeConfidence::None:
        return 0;
    case knowledge::KnowledgeConfidence::Low:
        return 250'000;
    case knowledge::KnowledgeConfidence::Medium:
        return 500'000;
    case knowledge::KnowledgeConfidence::High:
        return 750'000;
    case knowledge::KnowledgeConfidence::Certain:
        return 1'000'000;
    }
    return 0;
}

OperationId StableExternalOperation(narrative::NarrativeConsequenceExecutionId execution) noexcept
{
    constexpr std::uint64_t kHighSalt = 0x4E41525241544956ull;
    constexpr std::uint64_t kLowSalt = 0x455F4F5554424F58ull;
    return OperationId::FromRaw(execution.value.High() ^ kHighSalt, execution.value.Low() ^ kLowSalt);
}

bool DeliveryRestoreOrder(const NarrativeExternalConsequenceDelivery &a,
                          const NarrativeExternalConsequenceDelivery &b) noexcept
{
    if (a.revision != b.revision)
        return a.revision < b.revision;
    return a.execution < b.execution;
}

std::vector<std::byte> EncodeKnowledgeReference(const knowledge::KnowledgeRecord &record)
{
    ByteWriter writer;
    writer.U32(KnowledgeNarrativeReference::kSchemaVersion);
    writer.ObjectId(record.id.value);
    writer.U64(record.type.value.Raw());
    writer.U8(static_cast<std::uint8_t>(record.assertion));
    writer.U8(static_cast<std::uint8_t>(record.epistemic_state));
    writer.U8(static_cast<std::uint8_t>(record.confidence));
    writer.Ref(record.subject);
    writer.U64(record.source_kind.value.Raw());
    writer.Ref(record.source);
    writer.ObjectId(record.derived_from.value);
    writer.U64(record.original_source_kind.value.Raw());
    writer.Ref(record.original_source);
    writer.U32(record.transmission_depth);
    writer.U64(record.revision.value);
    return std::move(writer).Take();
}

foundation::Result<NarrativeExternalConsequenceSnapshot> DecodeOutboxSnapshot(
    std::span<const std::byte> payload,
    std::size_t max_records)
{
    ByteReader reader(payload);
    std::uint32_t magic = 0, version = 0, count = 0;
    std::uint64_t revision = 0;
    if (!reader.U32(magic) || !reader.U32(version) || !reader.U64(revision) || !reader.U32(count) ||
        magic != kOutboxWireMagic || version != kOutboxWireVersion)
    {
        return foundation::Result<NarrativeExternalConsequenceSnapshot>::Failure(
            Error("gameplay.narrative_integration.invalid_delivery_snapshot", "invalid external consequence snapshot header"));
    }
    if (static_cast<std::size_t>(count) > max_records)
    {
        return foundation::Result<NarrativeExternalConsequenceSnapshot>::Failure(
            Error("gameplay.narrative_integration.delivery_snapshot_capacity", "external consequence snapshot exceeds configured capacity"));
    }
    NarrativeExternalConsequenceSnapshot snapshot;
    snapshot.revision = Revision{revision};
    snapshot.deliveries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        NarrativeExternalConsequenceDelivery delivery;
        GameplayObjectId raw{};
        std::uint64_t type = 0, record_revision = 0;
        std::uint8_t state = 0;
        if (!reader.ObjectId(raw))
            goto invalid;
        delivery.execution = narrative::NarrativeConsequenceExecutionId{raw};
        if (!reader.ObjectId(raw))
            goto invalid;
        delivery.consequence = narrative::NarrativeConsequenceId{raw};
        if (!reader.U64(type))
            goto invalid;
        delivery.type = narrative::NarrativeConsequenceTypeId{TypeId::FromRaw(type)};
        if (!reader.ObjectId(raw))
            goto invalid;
        delivery.thread = narrative::NarrativeThreadId{raw};
        if (!reader.ObjectId(raw))
            goto invalid;
        delivery.objective = narrative::NarrativeObjectiveId{raw};
        if (!reader.Ref(delivery.owner) || !reader.Correlation(delivery.correlation) || !reader.U8(state) ||
            !reader.Payload(delivery.payload) || !reader.Operation(delivery.external_operation) ||
            !reader.I64(delivery.created_at.ticks) || !reader.I64(delivery.updated_at.ticks) ||
            !reader.U64(record_revision))
            goto invalid;
        delivery.state = static_cast<ExternalConsequenceDeliveryState>(state);
        delivery.revision = Revision{record_revision};
        snapshot.deliveries.push_back(std::move(delivery));
        continue;
    invalid:
        return foundation::Result<NarrativeExternalConsequenceSnapshot>::Failure(
            Error("gameplay.narrative_integration.invalid_delivery_snapshot", "truncated external consequence snapshot"));
    }
    if (!reader.Finished())
    {
        return foundation::Result<NarrativeExternalConsequenceSnapshot>::Failure(
            Error("gameplay.narrative_integration.invalid_delivery_snapshot", "external consequence snapshot has trailing bytes"));
    }
    return foundation::Result<NarrativeExternalConsequenceSnapshot>::Success(std::move(snapshot));
}

std::vector<std::byte> EncodeOutboxSnapshot(const NarrativeExternalConsequenceSnapshot &snapshot)
{
    ByteWriter writer;
    writer.U32(kOutboxWireMagic);
    writer.U32(kOutboxWireVersion);
    writer.U64(snapshot.revision.value);
    writer.U32(static_cast<std::uint32_t>(snapshot.deliveries.size()));
    for (const auto &delivery : snapshot.deliveries)
    {
        writer.ObjectId(delivery.execution.value);
        writer.ObjectId(delivery.consequence.value);
        writer.U64(delivery.type.value.Raw());
        writer.ObjectId(delivery.thread.value);
        writer.ObjectId(delivery.objective.value);
        writer.Ref(delivery.owner);
        writer.Correlation(delivery.correlation);
        writer.U8(static_cast<std::uint8_t>(delivery.state));
        writer.Payload(delivery.payload);
        writer.Operation(delivery.external_operation);
        writer.I64(delivery.created_at.ticks);
        writer.I64(delivery.updated_at.ticks);
        writer.U64(delivery.revision.value);
    }
    return std::move(writer).Take();
}

narrative::ClueId DiscoveryClueId(GameplayObjectRef discoverer,
                                  GameplayObjectRef subject,
                                  TypeId topic,
                                  CorrelationId correlation) noexcept
{
    constexpr std::uint64_t kMix = 0x9E3779B97F4A7C15ull;
    auto high = correlation.IsValid() ? correlation.High() : subject.id.High();
    auto low = correlation.IsValid() ? correlation.Low() : subject.id.Low();
    high ^= discoverer.id.High() + kMix + (high << 6u) + (high >> 2u);
    low ^= discoverer.id.Low() + (topic.Raw() * kMix) + (low << 6u) + (low >> 2u);
    if (high == 0 && low == 0)
        low = topic.IsValid() ? topic.Raw() : 1u;
    return narrative::ClueId::FromRaw(high, low);
}

class NarrativeExternalConsequenceRestoreStage final : public savegame::IRestoreStage
{
  public:
    explicit NarrativeExternalConsequenceRestoreStage(NarrativeExternalConsequenceSnapshot value)
        : snapshot(std::move(value))
    {
    }
    NarrativeExternalConsequenceSnapshot snapshot;
};
} // namespace

foundation::Result<void> NarrativeIntegrationContractRegistry::RegisterContract(NarrativeEventContract contract)
{
    if (frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.contracts_frozen", "narrative integration contracts are frozen"));
    if (!contract.id.IsValid() || !contract.event_type.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_contract", "invalid narrative integration event contract"));
    if (contracts_.contains(contract.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.duplicate_contract", "duplicate narrative integration contract id"));
    for (const auto &[id, existing] : contracts_)
    {
        (void)id;
        if (existing.event_type == contract.event_type)
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative_integration.duplicate_event_type", "duplicate narrative event type contract"));
    }
    contracts_.emplace(contract.id, contract);
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeIntegrationContractRegistry::RegisterStandardContracts()
{
    NarrativeIntegrationContractRegistry staged = *this;
    const std::array<NarrativeEventContract, 3> standard{{
        {DiscoveryContractId(), DiscoveryEventType(), DiscoveryTag()},
        {WorldEventContractId(), WorldEventType(), {}},
        {EncounterCompletedContractId(), EncounterCompletedEventType(), EncounterCompletedTag()},
    }};
    for (const auto &contract : standard)
    {
        if (const auto *existing = staged.Find(contract.id))
        {
            if (existing->event_type != contract.event_type || existing->required_tag != contract.required_tag)
                return foundation::Result<void>::Failure(
                    Error("gameplay.narrative_integration.standard_contract_conflict",
                          "standard narrative integration contract conflicts with existing registration"));
            continue;
        }
        auto registered = staged.RegisterContract(contract);
        if (!registered)
            return registered;
    }
    *this = std::move(staged);
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeIntegrationContractRegistry::Freeze()
{
    if (frozen_)
        return foundation::Result<void>::Success();
    const auto validate = [this](TypeId id, narrative::NarrativeEventTypeId event_type, TagId required_tag) {
        auto it = contracts_.find(id);
        return it != contracts_.end() && it->second.event_type == event_type && it->second.required_tag == required_tag;
    };
    if (!validate(DiscoveryContractId(), DiscoveryEventType(), DiscoveryTag()) ||
        !validate(WorldEventContractId(), WorldEventType(), {}) ||
        !validate(EncounterCompletedContractId(), EncounterCompletedEventType(), EncounterCompletedTag()))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.standard_contract_missing",
                  "standard narrative integration contracts are missing or incompatible"));
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}

const NarrativeEventContract *NarrativeIntegrationContractRegistry::Find(TypeId id) const noexcept
{
    const auto it = contracts_.find(id);
    return it == contracts_.end() ? nullptr : &it->second;
}

foundation::Result<const NarrativeEventContract *> NarrativeSemanticEventAdapter::RequireContract(TypeId id) const
{
    if (!contracts_.Frozen())
        return foundation::Result<const NarrativeEventContract *>::Failure(
            Error("gameplay.narrative_integration.contracts_not_frozen",
                  "narrative integration contracts must be frozen before runtime processing"));
    const auto *contract = contracts_.Find(id);
    if (!contract)
        return foundation::Result<const NarrativeEventContract *>::Failure(
            Error("gameplay.narrative_integration.contract_missing", "narrative integration contract is missing"));
    return foundation::Result<const NarrativeEventContract *>::Success(contract);
}

foundation::Result<void> NarrativeSemanticEventAdapter::ProcessDiscovery(narrative::NarrativeService &service,
                                                                         GameplayObjectRef discoverer,
                                                                         GameplayObjectRef subject,
                                                                         GameplayObjectRef area,
                                                                         TypeId topic,
                                                                         GameplayContext context,
                                                                         narrative::NarrativeProcessBudget budget) const
{
    if (!discoverer.IsValid() || !subject.IsValid() || !topic.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_discovery", "invalid discovery integration request"));
    auto required = RequireContract(NarrativeIntegrationContractRegistry::DiscoveryContractId());
    if (!required)
        return foundation::Result<void>::Failure(required.GetError());
    const auto *contract = required.Value();

    narrative::ClueRecord clue;
    clue.owner = discoverer;
    clue.type = TypeId::FromString("narrative.clue.discovery");
    clue.topic = topic;
    clue.area = area;
    clue.confidence = 1'000'000;
    clue.id = DiscoveryClueId(discoverer, subject, topic, context.correlation);

    narrative::NarrativeEvent event;
    event.type = contract->event_type;
    event.subject = subject;
    event.instigator = discoverer;
    event.area = area;
    event.time = context.time;
    event.correlation = context.correlation;
    if (contract->required_tag.IsValid())
        event.tags.Add(contract->required_tag);

    const auto diagnostics_before = service.GetDiagnostics();
    auto processed = service.ProcessNarrativeEvent(std::move(event), budget);
    if (!processed)
        return processed;
    const auto diagnostics_after = service.GetDiagnostics();
    if (diagnostics_after.budget_exhaustions > diagnostics_before.budget_exhaustions)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.discovery_deferred", "discovery narrative event exhausted its budget"));
    if (service.GetClue(clue.id) != nullptr)
        return foundation::Result<void>::Success();
    auto discovered = service.DiscoverClue(std::move(clue), context);
    if (!discovered)
        return foundation::Result<void>::Failure(discovered.GetError());
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeSemanticEventAdapter::ProcessWorldEvent(narrative::NarrativeService &service,
                                                                          GameplayObjectRef subject,
                                                                          GameplayObjectRef area,
                                                                          TagId event_tag,
                                                                          GameplayContext context,
                                                                          narrative::NarrativeProcessBudget budget) const
{
    if (!subject.IsValid() || !event_tag.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_world_event", "invalid world event integration request"));
    auto required = RequireContract(NarrativeIntegrationContractRegistry::WorldEventContractId());
    if (!required)
        return foundation::Result<void>::Failure(required.GetError());
    narrative::NarrativeEvent event;
    event.type = required.Value()->event_type;
    event.subject = subject;
    event.instigator = context.actor;
    event.area = area;
    event.time = context.time;
    event.correlation = context.correlation;
    event.tags.Add(event_tag);
    return service.ProcessNarrativeEvent(std::move(event), budget);
}

foundation::Result<void> NarrativeSemanticEventAdapter::ProcessEncounterCompleted(
    narrative::NarrativeService &service,
    GameplayObjectRef encounter,
    GameplayObjectRef actor,
    GameplayObjectRef area,
    GameplayContext context,
    narrative::NarrativeProcessBudget budget) const
{
    if (!encounter.IsValid() || !actor.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_encounter_event", "invalid encounter integration request"));
    auto required = RequireContract(NarrativeIntegrationContractRegistry::EncounterCompletedContractId());
    if (!required)
        return foundation::Result<void>::Failure(required.GetError());
    const auto *contract = required.Value();
    narrative::NarrativeEvent event;
    event.type = contract->event_type;
    event.subject = encounter;
    event.instigator = actor;
    event.area = area;
    event.time = context.time;
    event.correlation = context.correlation;
    if (contract->required_tag.IsValid())
        event.tags.Add(contract->required_tag);
    return service.ProcessNarrativeEvent(std::move(event), budget);
}

NarrativeExternalConsequenceDelivery *NarrativeExternalConsequenceOutbox::FindMutable(
    narrative::NarrativeConsequenceExecutionId execution) noexcept
{
    auto it = std::find_if(deliveries_.begin(), deliveries_.end(),
                           [execution](const auto &delivery) { return delivery.execution == execution; });
    return it == deliveries_.end() ? nullptr : &*it;
}

const NarrativeExternalConsequenceDelivery *NarrativeExternalConsequenceOutbox::FindDelivery(
    narrative::NarrativeConsequenceExecutionId execution) const noexcept
{
    auto it = std::find_if(deliveries_.begin(), deliveries_.end(),
                           [execution](const auto &delivery) { return delivery.execution == execution; });
    return it == deliveries_.end() ? nullptr : &*it;
}

bool NarrativeExternalConsequenceOutbox::Matches(const NarrativeExternalConsequenceDelivery &delivery,
                                                 const narrative::NarrativeConsequenceDefinition &definition,
                                                 const narrative::NarrativeConsequenceExecution &execution) const noexcept
{
    return delivery.execution == execution.id && delivery.consequence == definition.id &&
           delivery.type == definition.type && delivery.thread == execution.thread &&
           delivery.objective == execution.objective && delivery.correlation == execution.correlation;
}

narrative::NarrativeConsequenceResult NarrativeExternalConsequenceOutbox::Execute(
    const narrative::NarrativeConsequenceDefinition &definition,
    const narrative::NarrativeConsequenceExecution &execution,
    const narrative::NarrativeExecutionContext &context) const
{
    if (!definition.id.IsValid() || !definition.type.IsValid() || !execution.id.IsValid() ||
        definition.payload.size() > kMaxPersistentPayloadBytes)
        return {narrative::ConsequenceExecutionState::FailedPermanent, revision_};

    if (const auto *existing = FindDelivery(execution.id))
    {
        if (!Matches(*existing, definition, execution))
            return {narrative::ConsequenceExecutionState::FailedPermanent, revision_};
        switch (existing->state)
        {
        case ExternalConsequenceDeliveryState::Pending:
        case ExternalConsequenceDeliveryState::Retryable:
            return {narrative::ConsequenceExecutionState::Deferred, revision_};
        case ExternalConsequenceDeliveryState::Applied:
            return {narrative::ConsequenceExecutionState::AlreadyApplied, revision_};
        case ExternalConsequenceDeliveryState::FailedTerminal:
            return {narrative::ConsequenceExecutionState::FailedPermanent, revision_};
        }
    }

    if (deliveries_.size() >= max_records_)
        return {narrative::ConsequenceExecutionState::FailedRetryable, revision_};

    Bump();
    NarrativeExternalConsequenceDelivery delivery;
    delivery.execution = execution.id;
    delivery.consequence = definition.id;
    delivery.type = definition.type;
    delivery.thread = execution.thread;
    delivery.objective = execution.objective;
    delivery.owner = context.default_owner;
    delivery.correlation = execution.correlation;
    delivery.state = ExternalConsequenceDeliveryState::Pending;
    delivery.payload = definition.payload;
    delivery.external_operation = StableExternalOperation(execution.id);
    delivery.created_at = context.now;
    delivery.updated_at = context.now;
    delivery.revision = revision_;
    deliveries_.push_back(std::move(delivery));
    return {narrative::ConsequenceExecutionState::Deferred, revision_};
}

std::vector<NarrativeExternalConsequenceDelivery> NarrativeExternalConsequenceOutbox::PendingDeliveries() const
{
    std::vector<NarrativeExternalConsequenceDelivery> result;
    for (const auto &delivery : deliveries_)
    {
        if (delivery.state == ExternalConsequenceDeliveryState::Pending ||
            delivery.state == ExternalConsequenceDeliveryState::Retryable)
            result.push_back(delivery);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.revision != b.revision)
            return a.revision < b.revision;
        return a.execution < b.execution;
    });
    return result;
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::MarkRetryable(
    narrative::NarrativeConsequenceExecutionId execution,
    GameplayTimePoint now)
{
    auto *delivery = FindMutable(execution);
    if (!delivery)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_missing", "external consequence delivery missing"));
    if (delivery->state == ExternalConsequenceDeliveryState::Applied ||
        delivery->state == ExternalConsequenceDeliveryState::FailedTerminal)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_terminal", "external consequence delivery is terminal"));
    if (delivery->state == ExternalConsequenceDeliveryState::Retryable &&
        (now.ticks == 0 || delivery->updated_at == now))
        return foundation::Result<void>::Success();
    Bump();
    delivery->state = ExternalConsequenceDeliveryState::Retryable;
    if (now.ticks != 0)
        delivery->updated_at = now;
    delivery->revision = revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::AcknowledgeApplied(
    narrative::NarrativeConsequenceExecutionId execution,
    OperationId external_operation,
    GameplayTimePoint now)
{
    if (!external_operation.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_external_operation", "external operation id is required"));
    auto *delivery = FindMutable(execution);
    if (!delivery)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_missing", "external consequence delivery missing"));
    if (delivery->external_operation != external_operation)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_conflict",
                  "external operation id does not match the stable delivery operation"));
    if (delivery->state == ExternalConsequenceDeliveryState::Applied)
        return foundation::Result<void>::Success();
    if (delivery->state == ExternalConsequenceDeliveryState::FailedTerminal)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_terminal", "external consequence delivery already failed"));
    Bump();
    delivery->state = ExternalConsequenceDeliveryState::Applied;
    if (now.ticks != 0)
        delivery->updated_at = now;
    delivery->revision = revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::FailTerminal(
    narrative::NarrativeConsequenceExecutionId execution,
    GameplayTimePoint now)
{
    auto *delivery = FindMutable(execution);
    if (!delivery)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_missing", "external consequence delivery missing"));
    if (delivery->state == ExternalConsequenceDeliveryState::FailedTerminal)
        return foundation::Result<void>::Success();
    if (delivery->state == ExternalConsequenceDeliveryState::Applied)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_terminal", "applied delivery cannot be failed"));
    Bump();
    delivery->state = ExternalConsequenceDeliveryState::FailedTerminal;
    if (now.ticks != 0)
        delivery->updated_at = now;
    delivery->revision = revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::PruneConfirmedTerminal(
    narrative::NarrativeConsequenceExecutionId execution)
{
    auto it = std::find_if(deliveries_.begin(), deliveries_.end(),
                           [execution](const auto &delivery) { return delivery.execution == execution; });
    if (it == deliveries_.end())
        return foundation::Result<void>::Success();
    if (it->state != ExternalConsequenceDeliveryState::Applied &&
        it->state != ExternalConsequenceDeliveryState::FailedTerminal)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_not_terminal", "only confirmed terminal delivery may be pruned"));
    Bump();
    deliveries_.erase(it);
    return foundation::Result<void>::Success();
}

NarrativeExternalConsequenceSnapshot NarrativeExternalConsequenceOutbox::CaptureSnapshot() const
{
    NarrativeExternalConsequenceSnapshot snapshot;
    snapshot.deliveries = deliveries_;
    std::sort(snapshot.deliveries.begin(), snapshot.deliveries.end(), DeliveryRestoreOrder);
    snapshot.revision = revision_;
    return snapshot;
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::ValidateSnapshot(
    const NarrativeExternalConsequenceSnapshot &snapshot) const
{
    if (snapshot.deliveries.size() > max_records_)
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.delivery_snapshot_capacity", "delivery snapshot exceeds configured capacity"));
    std::unordered_set<GameplayObjectId> ids;
    for (const auto &delivery : snapshot.deliveries)
    {
        if (!delivery.execution.IsValid() || !delivery.consequence.IsValid() || !delivery.type.IsValid() ||
            !EnumInRange(delivery.state, ExternalConsequenceDeliveryState::FailedTerminal) ||
            delivery.payload.size() > kMaxPersistentPayloadBytes || delivery.revision > snapshot.revision ||
            delivery.updated_at < delivery.created_at || !delivery.external_operation.IsValid() ||
            !ids.emplace(delivery.execution.value).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.narrative_integration.invalid_delivery_snapshot", "invalid external consequence delivery snapshot"));
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<NarrativeExternalConsequenceSnapshot> NarrativeExternalConsequenceOutbox::PrepareSnapshotForRestore(
    NarrativeExternalConsequenceSnapshot snapshot) const
{
    auto valid = ValidateSnapshot(snapshot);
    if (!valid)
        return foundation::Result<NarrativeExternalConsequenceSnapshot>::Failure(valid.GetError());
    std::sort(snapshot.deliveries.begin(), snapshot.deliveries.end(), DeliveryRestoreOrder);
    return foundation::Result<NarrativeExternalConsequenceSnapshot>::Success(std::move(snapshot));
}

void NarrativeExternalConsequenceOutbox::PublishPreparedSnapshot(
    NarrativeExternalConsequenceSnapshot &&snapshot) noexcept
{
    deliveries_.swap(snapshot.deliveries);
    revision_ = snapshot.revision;
}

foundation::Result<void> NarrativeExternalConsequenceOutbox::RestoreSnapshot(
    NarrativeExternalConsequenceSnapshot snapshot)
{
    auto prepared = PrepareSnapshotForRestore(std::move(snapshot));
    if (!prepared)
        return foundation::Result<void>::Failure(prepared.GetError());
    auto prepared_snapshot = std::move(prepared).Value();
    PublishPreparedSnapshot(std::move(prepared_snapshot));
    return foundation::Result<void>::Success();
}

savegame::SaveParticipantId NarrativeExternalConsequenceSaveParticipant::Id() const noexcept
{
    return savegame::SaveParticipantId::FromString("integration.narrative.external_consequence_delivery");
}

foundation::Result<savegame::SaveSection> NarrativeExternalConsequenceSaveParticipant::CaptureSnapshot(
    const savegame::SaveContext &) const
{
    const auto snapshot = outbox_.CaptureSnapshot();
    if (snapshot.deliveries.size() > std::numeric_limits<std::uint32_t>::max())
        return foundation::Result<savegame::SaveSection>::Failure(
            Error("gameplay.narrative_integration.delivery_snapshot_too_large", "too many external consequence deliveries"));
    savegame::SaveSection section;
    section.participant = Id();
    section.schema_version = SchemaVersion();
    section.payload = EncodeOutboxSnapshot(snapshot);
    section.payload_hash = savegame::SaveGameOrchestrator::HashBytes(section.payload);
    return foundation::Result<savegame::SaveSection>::Success(std::move(section));
}

foundation::Result<void> NarrativeExternalConsequenceSaveParticipant::ValidateSnapshot(
    const savegame::SaveSection &section,
    const savegame::RestoreContext &) const
{
    if (section.participant != Id() || section.schema_version != SchemaVersion() ||
        section.payload_hash != savegame::SaveGameOrchestrator::HashBytes(section.payload))
        return foundation::Result<void>::Failure(
            Error("gameplay.narrative_integration.invalid_save_section", "invalid external consequence save section"));
    auto decoded = DecodeOutboxSnapshot(section.payload, outbox_.Capacity());
    if (!decoded)
        return foundation::Result<void>::Failure(decoded.GetError());
    return outbox_.ValidateSnapshot(decoded.Value());
}

foundation::Result<std::unique_ptr<savegame::IRestoreStage>> NarrativeExternalConsequenceSaveParticipant::StageRestore(
    const savegame::SaveSection &section,
    const savegame::RestoreContext &)
{
    if (section.participant != Id() || section.schema_version != SchemaVersion() ||
        section.payload_hash != savegame::SaveGameOrchestrator::HashBytes(section.payload))
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(
            Error("gameplay.narrative_integration.invalid_save_section", "invalid external consequence save section"));
    }
    auto decoded = DecodeOutboxSnapshot(section.payload, outbox_.Capacity());
    if (!decoded)
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(decoded.GetError());
    auto prepared = outbox_.PrepareSnapshotForRestore(std::move(decoded.Value()));
    if (!prepared)
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(prepared.GetError());
    return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Success(
        std::make_unique<NarrativeExternalConsequenceRestoreStage>(std::move(prepared.Value())));
}

void NarrativeExternalConsequenceSaveParticipant::CommitRestore(savegame::IRestoreStage &stage) noexcept
{
    auto *typed = dynamic_cast<NarrativeExternalConsequenceRestoreStage *>(&stage);
    if (!typed)
        return;
    outbox_.PublishPreparedSnapshot(std::move(typed->snapshot));
}

foundation::Result<narrative::ClueId> KnowledgeNarrativeAdapter::CreateClueFromKnowledge(
    narrative::NarrativeService &narrative_service,
    const knowledge::KnowledgeService &knowledge_service,
    knowledge::KnowledgeRecordId record_id,
    GameplayObjectRef area,
    GameplayContext context) const
{
    const auto *record = knowledge_service.FindKnowledge(record_id);
    if (!record)
        return foundation::Result<narrative::ClueId>::Failure(
            Error("gameplay.narrative_integration.knowledge_missing", "knowledge record does not exist"));
    narrative::ClueRecord clue;
    clue.owner = record->owner;
    clue.type = TypeId::FromString("narrative.clue.knowledge_reference");
    clue.topic = record->topic.id.value;
    clue.area = area;
    clue.confidence = ConfidenceMicro(record->confidence);
    clue.payload = EncodeKnowledgeReference(*record);
    return narrative_service.DiscoverClue(std::move(clue), context);
}

foundation::Result<narrative::RumorId> KnowledgeNarrativeAdapter::CreateRumorFromKnowledge(
    narrative::NarrativeService &narrative_service,
    const knowledge::KnowledgeService &knowledge_service,
    knowledge::KnowledgeRecordId record_id,
    GameplayObjectRef scope,
    GameplayDuration lifetime,
    GameplayContext context) const
{
    if (!scope.IsValid() || lifetime.ticks < 0)
        return foundation::Result<narrative::RumorId>::Failure(
            Error("gameplay.narrative_integration.invalid_knowledge_rumor", "invalid knowledge rumor request"));
    const auto *record = knowledge_service.FindKnowledge(record_id);
    if (!record)
        return foundation::Result<narrative::RumorId>::Failure(
            Error("gameplay.narrative_integration.knowledge_missing", "knowledge record does not exist"));
    const auto expires = CheckedAdd(context.time, lifetime);
    if (!expires)
        return foundation::Result<narrative::RumorId>::Failure(
            Error("gameplay.narrative_integration.knowledge_rumor_time_overflow", "knowledge rumor lifetime overflows gameplay time"));
    narrative::RumorRecord rumor;
    rumor.owner_or_scope = scope;
    rumor.topic = record->topic.id.value;
    rumor.confidence = ConfidenceMicro(record->confidence);
    rumor.expires_at = *expires;
    rumor.payload = EncodeKnowledgeReference(*record);
    return narrative_service.CreateRumor(std::move(rumor), context);
}

foundation::Result<KnowledgeNarrativeReference> KnowledgeNarrativeAdapter::DecodeReference(
    std::span<const std::byte> payload)
{
    ByteReader reader(payload);
    KnowledgeNarrativeReference result;
    GameplayObjectId raw{};
    std::uint64_t value = 0, revision = 0;
    std::uint8_t assertion = 0, epistemic = 0, confidence = 0;
    if (!reader.U32(result.schema_version) || result.schema_version != KnowledgeNarrativeReference::kSchemaVersion ||
        !reader.ObjectId(raw))
        return foundation::Result<KnowledgeNarrativeReference>::Failure(
            Error("gameplay.narrative_integration.invalid_knowledge_reference", "invalid knowledge reference payload"));
    result.record = knowledge::KnowledgeRecordId{raw};
    if (!reader.U64(value))
        goto invalid;
    result.belief_type = knowledge::BeliefTypeId{TypeId::FromRaw(value)};
    if (!reader.U8(assertion) || !reader.U8(epistemic) || !reader.U8(confidence) || !reader.Ref(result.subject) ||
        !reader.U64(value))
        goto invalid;
    result.assertion = static_cast<knowledge::KnowledgeAssertionValue>(assertion);
    result.epistemic_state = static_cast<knowledge::KnowledgeEpistemicState>(epistemic);
    result.confidence = static_cast<knowledge::KnowledgeConfidence>(confidence);
    result.source_kind = knowledge::KnowledgeSourceId{TypeId::FromRaw(value)};
    if (!reader.Ref(result.source) || !reader.ObjectId(raw))
        goto invalid;
    result.derived_from = knowledge::KnowledgeRecordId{raw};
    if (!reader.U64(value))
        goto invalid;
    result.original_source_kind = knowledge::KnowledgeSourceId{TypeId::FromRaw(value)};
    if (!reader.Ref(result.original_source) || !reader.U32(result.transmission_depth) || !reader.U64(revision) ||
        !reader.Finished())
        goto invalid;
    result.knowledge_revision = Revision{revision};
    if (!result.record.IsValid() || !result.belief_type.IsValid() || !result.source_kind.IsValid() ||
        !EnumInRange(result.assertion, knowledge::KnowledgeAssertionValue::Denied) ||
        !EnumInRange(result.epistemic_state, knowledge::KnowledgeEpistemicState::Outdated) ||
        !EnumInRange(result.confidence, knowledge::KnowledgeConfidence::Certain))
        goto invalid;
    return foundation::Result<KnowledgeNarrativeReference>::Success(result);

invalid:
    return foundation::Result<KnowledgeNarrativeReference>::Failure(
        Error("gameplay.narrative_integration.invalid_knowledge_reference", "invalid knowledge reference payload"));
}
} // namespace epidemic::gameplay::narrative_integration
