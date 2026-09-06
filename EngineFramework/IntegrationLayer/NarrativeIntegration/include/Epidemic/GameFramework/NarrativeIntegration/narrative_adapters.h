#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/GameFramework/Narrative/narrative.h"
#include "Epidemic/GameFramework/SaveGame/save_game.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::narrative_integration
{
struct NarrativeEventContract
{
    TypeId id{};
    narrative::NarrativeEventTypeId event_type{};
    TagId required_tag{};
};

class NarrativeIntegrationContractRegistry
{
  public:
    [[nodiscard]] static constexpr TypeId DiscoveryContractId() noexcept
    {
        return TypeId::FromString("integration.narrative.discovery");
    }
    [[nodiscard]] static constexpr TypeId WorldEventContractId() noexcept
    {
        return TypeId::FromString("integration.narrative.world_event");
    }
    [[nodiscard]] static constexpr TypeId EncounterCompletedContractId() noexcept
    {
        return TypeId::FromString("integration.narrative.encounter_completed");
    }
    [[nodiscard]] static constexpr narrative::NarrativeEventTypeId DiscoveryEventType() noexcept
    {
        return narrative::NarrativeEventTypeId::FromString("narrative.discovery");
    }
    [[nodiscard]] static constexpr narrative::NarrativeEventTypeId WorldEventType() noexcept
    {
        return narrative::NarrativeEventTypeId::FromString("narrative.world_event");
    }
    [[nodiscard]] static constexpr narrative::NarrativeEventTypeId EncounterCompletedEventType() noexcept
    {
        return narrative::NarrativeEventTypeId::FromString("narrative.encounter_completed");
    }
    [[nodiscard]] static constexpr TagId DiscoveryTag() noexcept
    {
        return TagId::FromString("narrative.discovery");
    }
    [[nodiscard]] static constexpr TagId EncounterCompletedTag() noexcept
    {
        return TagId::FromString("encounter.completed");
    }

    [[nodiscard]] foundation::Result<void> RegisterContract(NarrativeEventContract contract);
    [[nodiscard]] foundation::Result<void> RegisterStandardContracts();
    [[nodiscard]] foundation::Result<void> Freeze();
    [[nodiscard]] const NarrativeEventContract *Find(TypeId id) const noexcept;
    [[nodiscard]] bool Frozen() const noexcept { return frozen_; }

  private:
    std::unordered_map<TypeId, NarrativeEventContract> contracts_;
    bool frozen_ = false;
};

class NarrativeSemanticEventAdapter
{
  public:
    explicit NarrativeSemanticEventAdapter(const NarrativeIntegrationContractRegistry &contracts) noexcept
        : contracts_(contracts)
    {
    }

    [[nodiscard]] foundation::Result<void> ProcessDiscovery(
        narrative::NarrativeService &service,
        GameplayObjectRef discoverer,
        GameplayObjectRef subject,
        GameplayObjectRef area,
        TypeId topic,
        GameplayContext context,
        narrative::NarrativeProcessBudget budget = {}) const;

    [[nodiscard]] foundation::Result<void> ProcessWorldEvent(
        narrative::NarrativeService &service,
        GameplayObjectRef subject,
        GameplayObjectRef area,
        TagId event_tag,
        GameplayContext context,
        narrative::NarrativeProcessBudget budget = {}) const;

    [[nodiscard]] foundation::Result<void> ProcessEncounterCompleted(
        narrative::NarrativeService &service,
        GameplayObjectRef encounter,
        GameplayObjectRef actor,
        GameplayObjectRef area,
        GameplayContext context,
        narrative::NarrativeProcessBudget budget = {}) const;

  private:
    [[nodiscard]] foundation::Result<const NarrativeEventContract *> RequireContract(TypeId id) const;

    const NarrativeIntegrationContractRegistry &contracts_;
};

enum class ExternalConsequenceDeliveryState : std::uint8_t
{
    Pending,
    Retryable,
    Applied,
    FailedTerminal,
};

struct NarrativeExternalConsequenceDelivery
{
    narrative::NarrativeConsequenceExecutionId execution{};
    narrative::NarrativeConsequenceId consequence{};
    narrative::NarrativeConsequenceTypeId type{};
    narrative::NarrativeThreadId thread{};
    narrative::NarrativeObjectiveId objective{};
    GameplayObjectRef owner{};
    CorrelationId correlation{};
    ExternalConsequenceDeliveryState state = ExternalConsequenceDeliveryState::Pending;
    std::vector<std::byte> payload;
    OperationId external_operation{};
    GameplayTimePoint created_at{};
    GameplayTimePoint updated_at{};
    Revision revision{};
};

struct NarrativeExternalConsequenceSnapshot
{
    std::vector<NarrativeExternalConsequenceDelivery> deliveries;
    Revision revision{};
};

class NarrativeExternalConsequenceOutbox final : public narrative::INarrativeConsequenceHandler
{
  public:
    explicit NarrativeExternalConsequenceOutbox(std::size_t max_records = 4096) noexcept : max_records_(max_records) {}

    [[nodiscard]] narrative::NarrativeConsequenceResult Execute(
        const narrative::NarrativeConsequenceDefinition &definition,
        const narrative::NarrativeConsequenceExecution &execution,
        const narrative::NarrativeExecutionContext &context) const override;

    [[nodiscard]] std::vector<NarrativeExternalConsequenceDelivery> PendingDeliveries() const;
    [[nodiscard]] const NarrativeExternalConsequenceDelivery *FindDelivery(
        narrative::NarrativeConsequenceExecutionId execution) const noexcept;
    [[nodiscard]] foundation::Result<void> MarkRetryable(narrative::NarrativeConsequenceExecutionId execution,
                                                         GameplayTimePoint now = {});
    [[nodiscard]] foundation::Result<void> AcknowledgeApplied(narrative::NarrativeConsequenceExecutionId execution,
                                                              OperationId external_operation,
                                                              GameplayTimePoint now = {});
    [[nodiscard]] foundation::Result<void> FailTerminal(narrative::NarrativeConsequenceExecutionId execution,
                                                        GameplayTimePoint now = {});
    // Call only after the corresponding Narrative consequence terminal state is durably saved.
    [[nodiscard]] foundation::Result<void> PruneConfirmedTerminal(
        narrative::NarrativeConsequenceExecutionId execution);

    [[nodiscard]] NarrativeExternalConsequenceSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> ValidateSnapshot(const NarrativeExternalConsequenceSnapshot &snapshot) const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(NarrativeExternalConsequenceSnapshot snapshot);
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return max_records_; }

  private:
    [[nodiscard]] NarrativeExternalConsequenceDelivery *FindMutable(
        narrative::NarrativeConsequenceExecutionId execution) noexcept;
    [[nodiscard]] bool Matches(const NarrativeExternalConsequenceDelivery &delivery,
                               const narrative::NarrativeConsequenceDefinition &definition,
                               const narrative::NarrativeConsequenceExecution &execution) const noexcept;
    void Bump() const noexcept { ++revision_.value; }

    std::size_t max_records_ = 4096;
    mutable Revision revision_{};
    mutable std::vector<NarrativeExternalConsequenceDelivery> deliveries_;
};

class NarrativeExternalConsequenceSaveParticipant final : public savegame::ISaveParticipant
{
  public:
    explicit NarrativeExternalConsequenceSaveParticipant(NarrativeExternalConsequenceOutbox &outbox) noexcept
        : outbox_(outbox)
    {
    }

    [[nodiscard]] savegame::SaveParticipantId Id() const noexcept override;
    [[nodiscard]] savegame::SaveSchemaVersion SchemaVersion() const noexcept override { return 1; }
    [[nodiscard]] std::vector<savegame::SaveParticipantId> Dependencies() const override { return {}; }
    [[nodiscard]] foundation::Result<savegame::SaveSection> CaptureSnapshot(
        const savegame::SaveContext &context) const override;
    [[nodiscard]] foundation::Result<void> ValidateSnapshot(const savegame::SaveSection &section,
                                                            const savegame::RestoreContext &context) const override;
    [[nodiscard]] foundation::Result<std::unique_ptr<savegame::IRestoreStage>> StageRestore(
        const savegame::SaveSection &section,
        const savegame::RestoreContext &context) override;
    void CommitRestore(savegame::IRestoreStage &stage) noexcept override;

  private:
    NarrativeExternalConsequenceOutbox &outbox_;
};

struct KnowledgeNarrativeReference
{
    static constexpr std::uint32_t kSchemaVersion = 1;
    std::uint32_t schema_version = kSchemaVersion;
    knowledge::KnowledgeRecordId record{};
    knowledge::BeliefTypeId belief_type{};
    knowledge::KnowledgeAssertionValue assertion = knowledge::KnowledgeAssertionValue::Unknown;
    knowledge::KnowledgeEpistemicState epistemic_state = knowledge::KnowledgeEpistemicState::Suspected;
    knowledge::KnowledgeConfidence confidence = knowledge::KnowledgeConfidence::None;
    GameplayObjectRef subject{};
    knowledge::KnowledgeSourceId source_kind{};
    GameplayObjectRef source{};
    knowledge::KnowledgeRecordId derived_from{};
    knowledge::KnowledgeSourceId original_source_kind{};
    GameplayObjectRef original_source{};
    std::uint32_t transmission_depth = 0;
    Revision knowledge_revision{};
};

class KnowledgeNarrativeAdapter
{
  public:
    [[nodiscard]] foundation::Result<narrative::ClueId> CreateClueFromKnowledge(
        narrative::NarrativeService &narrative_service,
        const knowledge::KnowledgeService &knowledge_service,
        knowledge::KnowledgeRecordId record,
        GameplayObjectRef area,
        GameplayContext context) const;

    [[nodiscard]] foundation::Result<narrative::RumorId> CreateRumorFromKnowledge(
        narrative::NarrativeService &narrative_service,
        const knowledge::KnowledgeService &knowledge_service,
        knowledge::KnowledgeRecordId record,
        GameplayObjectRef scope,
        GameplayDuration lifetime,
        GameplayContext context) const;

    [[nodiscard]] static foundation::Result<KnowledgeNarrativeReference> DecodeReference(
        std::span<const std::byte> payload);
};
} // namespace epidemic::gameplay::narrative_integration
