#include "Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::knowledge;
using namespace epidemic::gameplay::narrative;
using namespace epidemic::gameplay::narrative_integration;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

class DiscoveryResolver final : public INarrativeConditionResolver
{
  public:
    NarrativeConditionResult Evaluate(const NarrativeConditionDefinition &definition,
                                      const NarrativeEvaluationContext &context) const override
    {
        return {definition.id,
                context.event.tags.HasExact(NarrativeIntegrationContractRegistry::DiscoveryTag())
                    ? ConditionEvaluationState::Satisfied
                    : ConditionEvaluationState::Unsatisfied,
                1'000'000,
                {},
                {}};
    }
};

bool ContainsExecution(const std::vector<NarrativeConsequenceExecution> &executions,
                       NarrativeConsequenceExecutionId id)
{
    return std::any_of(executions.begin(), executions.end(),
                       [id](const auto &execution) { return execution.id == id; });
}

bool SameDelivery(const NarrativeExternalConsequenceDelivery &a, const NarrativeExternalConsequenceDelivery &b)
{
    return a.execution == b.execution && a.consequence == b.consequence && a.type == b.type &&
           a.thread == b.thread && a.objective == b.objective && a.owner == b.owner &&
           a.correlation == b.correlation && a.state == b.state && a.payload == b.payload &&
           a.external_operation == b.external_operation && a.created_at == b.created_at &&
           a.updated_at == b.updated_at && a.revision == b.revision;
}

bool SameSnapshot(const NarrativeExternalConsequenceSnapshot &a, const NarrativeExternalConsequenceSnapshot &b)
{
    if (a.revision != b.revision || a.deliveries.size() != b.deliveries.size())
        return false;
    for (std::size_t i = 0; i < a.deliveries.size(); ++i)
    {
        if (!SameDelivery(a.deliveries[i], b.deliveries[i]))
            return false;
    }
    return true;
}

void WriteU32Le(std::vector<std::byte> &payload, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        payload[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xFFu);
}

savegame::SaveSection DuplicateFirstDeliverySection(savegame::SaveSection section)
{
    constexpr std::size_t kOutboxHeaderBytes = 20;
    Check(section.payload.size() > kOutboxHeaderBytes, "outbox test section has at least one delivery");
    std::vector<std::byte> record(section.payload.begin() + static_cast<std::ptrdiff_t>(kOutboxHeaderBytes),
                                  section.payload.end());
    WriteU32Le(section.payload, 16, 2);
    section.payload.insert(section.payload.end(), record.begin(), record.end());
    section.payload_hash = savegame::SaveGameOrchestrator::HashBytes(section.payload);
    return section;
}

class DummyRestoreStage final : public savegame::IRestoreStage
{
};
} // namespace

int main()
{
    NarrativeService service;
    DiscoveryResolver discovery;
    NarrativeExternalConsequenceOutbox outbox;

    const auto condition_type = NarrativeConditionTypeId::FromString("condition.discovery");
    const auto consequence_type = NarrativeConsequenceTypeId::FromString("narrative.external_reward");
    Check(static_cast<bool>(service.RegisterConditionResolver(condition_type, discovery)), "condition resolver");
    Check(static_cast<bool>(service.RegisterConsequenceHandler(consequence_type, outbox)), "outbox handler");

    const auto thread = NarrativeThreadId::FromString("thread.corpse");
    const auto objective = NarrativeObjectiveId::FromString("objective.investigate_corpse");
    const auto beat = NarrativeBeatId::FromString("beat.corpse_discovered");
    const auto condition = NarrativeConditionId::FromString("condition.corpse_discovered");
    const auto consequence = NarrativeConsequenceId::FromString("consequence.notify_ai");

    NarrativeConditionDefinition cond;
    cond.id = condition;
    cond.type = condition_type;
    Check(static_cast<bool>(service.RegisterConditionDefinition(cond)), "condition definition");

    NarrativeConsequenceDefinition cons;
    cons.id = consequence;
    cons.type = consequence_type;
    cons.payload = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    Check(static_cast<bool>(service.RegisterConsequenceDefinition(cons)), "consequence definition");

    NarrativeBeatDefinition beat_def;
    beat_def.id = beat;
    beat_def.thread = thread;
    beat_def.activation_conditions.push_back(condition);
    beat_def.consequences.push_back(consequence);
    Check(static_cast<bool>(service.RegisterBeatDefinition(beat_def)), "beat definition");

    NarrativeObjectiveDefinition obj;
    obj.id = objective;
    obj.thread = thread;
    obj.start_conditions.push_back(condition);
    Check(static_cast<bool>(service.RegisterObjectiveDefinition(obj)), "objective definition");

    NarrativeThreadDefinition thread_def;
    thread_def.id = thread;
    thread_def.beats.push_back(beat);
    thread_def.root_objectives.push_back(objective);
    Check(static_cast<bool>(service.RegisterThreadDefinition(thread_def)), "thread definition");
    Check(static_cast<bool>(service.FreezeDefinitions()), "freeze narrative definitions");

    NarrativeIntegrationContractRegistry contracts;
    Check(static_cast<bool>(contracts.RegisterStandardContracts()), "register standard narrative contracts");
    Check(static_cast<bool>(contracts.RegisterStandardContracts()), "standard contract registration is idempotent");
    Check(!static_cast<bool>(contracts.RegisterContract(
              {TypeId::FromString("integration.narrative.duplicate"),
               NarrativeIntegrationContractRegistry::DiscoveryEventType(),
               {}})),
          "duplicate event type rejected");

    const auto player = Ref("game.actor", "player");
    const auto corpse = Ref("entity", "corpse");
    const auto forest = Ref("world.area", "forest");
    const auto discovery_context = GameplayContext{.time = GameplayTimePoint{100},
                                                   .correlation = CorrelationId::FromString("corpse.discovery"),
                                                   .actor = player};

    NarrativeSemanticEventAdapter events(contracts);
    Check(!static_cast<bool>(events.ProcessWorldEvent(service, corpse, forest, TagId::FromString("world.test"),
                                                       discovery_context)),
          "runtime event processing requires frozen integration contracts");
    Check(static_cast<bool>(contracts.Freeze()), "freeze integration contracts");
    Check(!static_cast<bool>(contracts.RegisterContract(
              {TypeId::FromString("integration.narrative.after_freeze"),
               NarrativeEventTypeId::FromString("narrative.after_freeze"),
               {}})),
          "contract registration blocked after freeze");

    NarrativeProcessBudget exhausted_budget;
    exhausted_budget.max_condition_evaluations = 0;
    Check(!static_cast<bool>(events.ProcessDiscovery(service, player, corpse, forest, TypeId::FromString("topic.corpse"),
                                                      discovery_context, exhausted_budget)),
          "discovery budget exhaustion fails atomically");
    Check(service.FindClues(player, TypeId::FromString("topic.corpse")).empty(),
          "failed discovery did not leak clue");
    Check(service.GetThreadState(thread) == nullptr, "failed discovery did not mutate thread state");

    Check(static_cast<bool>(events.ProcessDiscovery(service, player, corpse, forest, TypeId::FromString("topic.corpse"),
                                                     discovery_context)),
          "process discovery adapter");
    Check(service.FindClues(player, TypeId::FromString("topic.corpse")).size() == 1,
          "successful discovery created one clue");
    Check(service.GetThreadState(thread) != nullptr &&
              service.GetThreadState(thread)->state == NarrativeRuntimeState::Active,
          "thread active from discovery");
    Check(service.GetObjectiveState(objective) != nullptr &&
              service.GetObjectiveState(objective)->state == NarrativeObjectiveRuntimeState::Active,
          "objective active from discovery");

    Check(static_cast<bool>(events.ProcessDiscovery(service, player, corpse, forest, TypeId::FromString("topic.corpse"),
                                                     discovery_context)),
          "duplicate discovery is idempotent");
    Check(service.FindClues(player, TypeId::FromString("topic.corpse")).size() == 1,
          "duplicate discovery did not create second clue");

    const auto first_execution = service.ExecutePendingConsequences(
        {.default_owner = player, .now = GameplayTimePoint{110}}, 8);
    Check(first_execution.empty(), "external consequence remains deferred until downstream acknowledgment");
    auto pending = outbox.PendingDeliveries();
    Check(pending.size() == 1, "external consequence written to durable outbox");
    Check(pending.front().payload == cons.payload, "outbox preserves consequence payload");
    const auto execution_id = pending.front().execution;
    Check(ContainsExecution(service.FindConsequences(ConsequenceExecutionState::Deferred), execution_id),
          "narrative consequence is deferred while outbox is pending");

    NarrativeExternalConsequenceSaveParticipant save_participant(outbox);
    const auto captured_snapshot = outbox.CaptureSnapshot();
    auto captured = save_participant.CaptureSnapshot({});
    Check(static_cast<bool>(captured), "capture external consequence save participant");

    auto over_capacity = captured.Value();
    WriteU32Le(over_capacity.payload, 16, static_cast<std::uint32_t>(outbox.Capacity() + 1));
    over_capacity.payload_hash = savegame::SaveGameOrchestrator::HashBytes(over_capacity.payload);
    const auto over_capacity_validation = save_participant.ValidateSnapshot(over_capacity, {});
    Check(!static_cast<bool>(over_capacity_validation) &&
              over_capacity_validation.GetError().HasCode("gameplay.narrative_integration.delivery_snapshot_capacity"),
          "outbox wire count above configured capacity rejected before decode allocation");
    Check(!static_cast<bool>(save_participant.StageRestore(over_capacity, {})),
          "outbox stage rejects count above configured capacity");

    auto huge_count = captured.Value();
    WriteU32Le(huge_count.payload, 16, std::numeric_limits<std::uint32_t>::max());
    huge_count.payload_hash = savegame::SaveGameOrchestrator::HashBytes(huge_count.payload);
    const auto huge_count_validation = save_participant.ValidateSnapshot(huge_count, {});
    Check(!static_cast<bool>(huge_count_validation) &&
              huge_count_validation.GetError().HasCode("gameplay.narrative_integration.delivery_snapshot_capacity"),
          "huge wire count in short payload is rejected by configured capacity");

    auto corrupted = captured.Value();
    corrupted.payload_hash ^= 0x55u;
    Check(!static_cast<bool>(save_participant.ValidateSnapshot(corrupted, {})), "corrupted save section rejected");
    Check(!static_cast<bool>(save_participant.StageRestore(corrupted, {})), "corrupted save section rejected during stage");
    Check(outbox.PendingDeliveries().size() == 1, "failed save validation did not mutate outbox");

    NarrativeExternalConsequenceOutbox restored_outbox;
    NarrativeExternalConsequenceSaveParticipant restored_participant(restored_outbox);
    Check(static_cast<bool>(restored_participant.ValidateSnapshot(captured.Value(), {})), "validate persisted outbox");
    auto staged = restored_participant.StageRestore(captured.Value(), {});
    Check(static_cast<bool>(staged), "stage persisted outbox");
    auto stage = std::move(staged).Value();
    restored_participant.CommitRestore(*stage);
    Check(restored_outbox.PendingDeliveries().size() == 1 &&
              restored_outbox.PendingDeliveries().front().execution == execution_id,
          "pending external consequence survives save load");
    Check(SameSnapshot(restored_outbox.CaptureSnapshot(), captured_snapshot),
          "committed outbox restore matches captured snapshot");

    DummyRestoreStage wrong_stage;
    restored_participant.CommitRestore(wrong_stage);
    Check(SameSnapshot(restored_outbox.CaptureSnapshot(), captured_snapshot),
          "wrong restore stage does not publish partial state");

    const auto duplicate_section = DuplicateFirstDeliverySection(captured.Value());
    Check(!static_cast<bool>(save_participant.ValidateSnapshot(duplicate_section, {})),
          "duplicate delivery save section rejected during validation");
    Check(!static_cast<bool>(save_participant.StageRestore(duplicate_section, {})),
          "duplicate delivery save section rejected during stage");
    Check(SameSnapshot(restored_outbox.CaptureSnapshot(), captured_snapshot),
          "failed duplicate stage did not mutate restored outbox");

    auto bad_snapshot = restored_outbox.CaptureSnapshot();
    bad_snapshot.deliveries.push_back(bad_snapshot.deliveries.front());
    const auto before_bad_restore = restored_outbox.CaptureSnapshot();
    Check(!static_cast<bool>(restored_outbox.RestoreSnapshot(std::move(bad_snapshot))),
          "duplicate delivery snapshot rejected");
    Check(SameSnapshot(restored_outbox.CaptureSnapshot(), before_bad_restore),
          "invalid outbox restore is transactional");

    NarrativeExternalConsequenceOutbox ordered_outbox;
    NarrativeConsequenceExecution ordered_first;
    ordered_first.id = NarrativeConsequenceExecutionId::FromString("execution.restore.order.first");
    ordered_first.consequence = consequence;
    ordered_first.correlation = CorrelationId::FromString("restore.order.first");
    NarrativeConsequenceExecution ordered_second = ordered_first;
    ordered_second.id = NarrativeConsequenceExecutionId::FromString("execution.restore.order.second");
    ordered_second.correlation = CorrelationId::FromString("restore.order.second");
    Check(ordered_outbox.Execute(cons, ordered_first, {.default_owner = player, .now = GameplayTimePoint{115}}).state ==
              ConsequenceExecutionState::Deferred,
          "ordered outbox first delivery");
    Check(ordered_outbox.Execute(cons, ordered_second, {.default_owner = player, .now = GameplayTimePoint{116}}).state ==
              ConsequenceExecutionState::Deferred,
          "ordered outbox second delivery");
    auto unordered_snapshot = ordered_outbox.CaptureSnapshot();
    std::reverse(unordered_snapshot.deliveries.begin(), unordered_snapshot.deliveries.end());
    auto prepared_order = ordered_outbox.PrepareSnapshotForRestore(std::move(unordered_snapshot));
    Check(static_cast<bool>(prepared_order), "prepare restore order snapshot");
    Check(prepared_order.Value().deliveries.size() == 2 &&
              prepared_order.Value().deliveries[0].revision < prepared_order.Value().deliveries[1].revision,
          "stage restore normalizes delivery order before commit");

    auto repeated_stage = restored_participant.StageRestore(captured.Value(), {});
    Check(static_cast<bool>(repeated_stage), "repeat stage persisted outbox");
    auto repeated = std::move(repeated_stage).Value();
    restored_participant.CommitRestore(*repeated);
    Check(SameSnapshot(restored_outbox.CaptureSnapshot(), captured_snapshot), "repeated restore is stable");

    NarrativeExternalConsequenceOutbox empty_source;
    NarrativeExternalConsequenceSaveParticipant empty_participant(empty_source);
    auto empty_section = empty_participant.CaptureSnapshot({});
    Check(static_cast<bool>(empty_section), "capture empty outbox snapshot");
    auto empty_stage = restored_participant.StageRestore(empty_section.Value(), {});
    Check(static_cast<bool>(empty_stage), "stage empty outbox snapshot");
    auto empty = std::move(empty_stage).Value();
    restored_participant.CommitRestore(*empty);
    Check(restored_outbox.CaptureSnapshot().deliveries.empty(), "empty outbox restore publishes empty state");

    const auto external_operation = pending.front().external_operation;
    Check(external_operation.IsValid(), "outbox exposes stable downstream operation id");
    Check(!static_cast<bool>(outbox.AcknowledgeApplied(execution_id, OperationId::FromString("wrong.operation"),
                                                        GameplayTimePoint{111})),
          "outbox rejects non-stable downstream operation id");
    Check(static_cast<bool>(outbox.AcknowledgeApplied(execution_id, external_operation, GameplayTimePoint{111})),
          "acknowledge external consequence");
    Check(static_cast<bool>(outbox.AcknowledgeApplied(execution_id, external_operation, GameplayTimePoint{111})),
          "external consequence acknowledgment is idempotent");
    Check(service.ExecutePendingConsequences({.default_owner = player, .now = GameplayTimePoint{112}}, 8).size() == 1,
          "narrative observes downstream acknowledgment exactly once");
    Check(ContainsExecution(service.FindConsequences(ConsequenceExecutionState::Applied), execution_id),
          "narrative consequence becomes applied after outbox acknowledgment");
    Check(static_cast<bool>(outbox.PruneConfirmedTerminal(execution_id)), "confirmed terminal delivery can be pruned");
    Check(outbox.FindDelivery(execution_id) == nullptr, "pruned delivery removed from bounded outbox");


    NarrativeExternalConsequenceOutbox bounded_outbox(1);
    NarrativeConsequenceExecution bounded_first;
    bounded_first.id = NarrativeConsequenceExecutionId::FromString("execution.bounded.first");
    bounded_first.consequence = consequence;
    bounded_first.correlation = CorrelationId::FromString("bounded.first");
    NarrativeConsequenceExecution bounded_second = bounded_first;
    bounded_second.id = NarrativeConsequenceExecutionId::FromString("execution.bounded.second");
    bounded_second.correlation = CorrelationId::FromString("bounded.second");
    const NarrativeExecutionContext bounded_context{.default_owner = player, .now = GameplayTimePoint{120}};
    Check(bounded_outbox.Execute(cons, bounded_first, bounded_context).state == ConsequenceExecutionState::Deferred,
          "bounded outbox accepts first delivery");
    Check(bounded_outbox.Execute(cons, bounded_second, bounded_context).state == ConsequenceExecutionState::FailedRetryable,
          "bounded outbox applies backpressure instead of growing without limit");
    NarrativeExternalConsequenceSaveParticipant bounded_participant(bounded_outbox);
    const auto bounded_section = bounded_participant.CaptureSnapshot({});
    Check(static_cast<bool>(bounded_section), "capture outbox exactly at configured capacity");
    NarrativeExternalConsequenceOutbox bounded_restored(1);
    NarrativeExternalConsequenceSaveParticipant bounded_restored_participant(bounded_restored);
    Check(static_cast<bool>(bounded_restored_participant.ValidateSnapshot(bounded_section.Value(), {})),
          "validate outbox snapshot exactly at configured capacity");
    auto bounded_stage = bounded_restored_participant.StageRestore(bounded_section.Value(), {});
    Check(static_cast<bool>(bounded_stage), "stage outbox snapshot exactly at configured capacity");
    auto bounded_restore_stage = std::move(bounded_stage).Value();
    bounded_restored_participant.CommitRestore(*bounded_restore_stage);
    Check(bounded_restored.PendingDeliveries().size() == 1,
          "restore outbox snapshot exactly at configured capacity");

    KnowledgeService knowledge_service;
    const auto speaker = Ref("game.actor", "witness");
    const auto listener = Ref("game.actor", "listener");
    KnowledgeProfile speaker_profile;
    speaker_profile.subject = speaker;
    KnowledgeProfile listener_profile;
    listener_profile.subject = listener;
    Check(static_cast<bool>(knowledge_service.CreateProfile(speaker_profile)), "speaker knowledge profile");
    Check(static_cast<bool>(knowledge_service.CreateProfile(listener_profile)), "listener knowledge profile");
    Check(static_cast<bool>(knowledge_service.FreezeDefinitions()), "freeze knowledge definitions");

    KnowledgeTopic bridge_topic;
    bridge_topic.id = KnowledgeTopicId::FromString("topic.destroyed_bridge");
    bridge_topic.primary_subject = Ref("world.feature", "bridge");
    bridge_topic.scope = forest;
    LearnKnowledgeRequest learn_request;
    learn_request.learner = speaker;
    learn_request.type = BeliefTypeId::FromString("belief.world_state");
    learn_request.topic = bridge_topic;
    learn_request.source_kind = KnowledgeSourceId::FromString("knowledge.direct_perception");
    learn_request.source_object = bridge_topic.primary_subject;
    learn_request.assertion = KnowledgeAssertionValue::Affirmed;
    learn_request.epistemic_state = KnowledgeEpistemicState::Known;
    learn_request.confidence = KnowledgeConfidence::Certain;
    learn_request.context = {.time = GameplayTimePoint{200}, .actor = speaker};
    auto learned = knowledge_service.Learn(std::move(learn_request));
    Check(static_cast<bool>(learned), "learn source knowledge");
    auto shared = knowledge_service.Share({.speaker = speaker,
                                           .listener = listener,
                                           .record = learned.Value(),
                                           .mode = KnowledgeShareMode::Tell,
                                           .context = {.time = GameplayTimePoint{201}, .actor = speaker}});
    Check(static_cast<bool>(shared), "share knowledge with provenance");

    KnowledgeNarrativeAdapter knowledge_adapter;
    auto knowledge_clue = knowledge_adapter.CreateClueFromKnowledge(
        service, knowledge_service, shared.Value(), forest, {.time = GameplayTimePoint{202}, .actor = listener});
    Check(static_cast<bool>(knowledge_clue), "create clue from actual knowledge record");
    const auto *clue = service.GetClue(knowledge_clue.Value());
    Check(clue != nullptr && clue->owner == listener && clue->topic == bridge_topic.id.value,
          "knowledge clue is a derived narrative projection");
    auto reference = KnowledgeNarrativeAdapter::DecodeReference(clue->payload);
    Check(static_cast<bool>(reference), "decode versioned knowledge provenance reference");
    Check(reference.Value().record == shared.Value(), "knowledge reference preserves source record id");
    Check(reference.Value().derived_from == learned.Value(), "knowledge reference preserves transmission provenance");
    Check(reference.Value().original_source_kind == KnowledgeSourceId::FromString("knowledge.direct_perception"),
          "knowledge reference preserves original source kind");
    Check(reference.Value().original_source == bridge_topic.primary_subject,
          "knowledge reference preserves original source object");
    Check(reference.Value().assertion == KnowledgeAssertionValue::Affirmed &&
              reference.Value().epistemic_state == KnowledgeEpistemicState::Known &&
              reference.Value().confidence == KnowledgeConfidence::Certain,
          "knowledge reference preserves epistemic semantics");

    auto rumor = knowledge_adapter.CreateRumorFromKnowledge(service, knowledge_service, shared.Value(), listener,
                                                            GameplayDuration{10},
                                                            {.time = GameplayTimePoint{210}, .actor = listener});
    Check(static_cast<bool>(rumor), "create rumor from knowledge reference");
    Check(service.FindRumors(listener, bridge_topic.id.value).size() == 1, "knowledge-backed rumor query");
    auto expired = service.ExpireRumors(GameplayTimePoint{221}, {.time = GameplayTimePoint{221}, .actor = listener});
    Check(expired.size() == 1, "knowledge-backed rumor expiration");

    return 0;
}
