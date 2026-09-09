#include "Epidemic/GameFramework/AI/ai.h"

#include <limits>
#include <stdexcept>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ai;

namespace
{
GameplayObjectRef Ref(const char *name)
{
    return {GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString(name)};
}

AIInputValue FixedInput(AIInputKeyId key, Fixed value, AIAccessFlag access)
{
    AIInputValue input;
    input.key = key;
    input.kind = AIInputValueKind::Fixed;
    input.access = access;
    input.fixed_value = value;
    return input;
}

class ThrowingEvaluator final : public IAIConsiderationEvaluator
{
  public:
    epidemic::foundation::Result<Fixed> Evaluate(const AIConsideration &, const AIContextSnapshot &,
                                       const AITargetCandidate *) const override
    {
        throw std::runtime_error("boom");
    }
};

epidemic::foundation::Result<void> RegisterCoreDefinitions(AIService &service, AIProfileId profile_id, AIGoalId goal_id,
                                                 AIIntentTypeId intent_type, AIInputKeyId score_key,
                                                 AIMaterializationPolicy materialization =
                                                     AIMaterializationPolicy::AbstractCapable,
                                                 GameplayDuration repeat_cooldown = {})
{
    AIGoalDefinition goal;
    goal.id = goal_id;
    goal.canonical_name = "game.attack_target";
    goal.intent_type = intent_type;
    goal.target_policy = AITargetPolicy::Required;
    AIConsideration consideration;
    consideration.id = AIConsiderationId::FromString("game.target_score");
    consideration.evaluator = AIService::InputEvaluatorId();
    consideration.input_key = score_key;
    consideration.scope = AIConsiderationScope::Target;
    consideration.weight_micro = kFixedOne;
    goal.considerations = {consideration};
    if (auto result = service.RegisterGoal(goal); !result)
        return result;

    AIProfile profile;
    profile.id = profile_id;
    profile.canonical_name = "game.guard";
    profile.default_goals = {goal_id};
    profile.materialization_policy = materialization;
    profile.think_interval = {1};
    profile.goal_repeat_cooldown = repeat_cooldown;
    if (auto result = service.RegisterProfile(profile); !result)
        return result;
    return epidemic::foundation::Result<void>::Success();
}
} // namespace

int main()
{
    const auto profile_id = AIProfileId::FromString("game.guard");
    const auto goal_id = AIGoalId::FromString("game.attack_target");
    const auto intent_type = AIIntentTypeId::FromString("game.attack");
    const auto score_key = AIInputKeyId::FromString("game.target_threat");
    const auto guard = Ref("guard");
    const auto target_a = Ref("target_a");
    const auto target_b = Ref("target_b");

    AIService ai;
    if (!RegisterCoreDefinitions(ai, profile_id, goal_id, intent_type, score_key, AIMaterializationPolicy::AbstractCapable,
                                 GameplayDuration{5}))
        return 1;
    if (ai.RegisterAgent(guard, profile_id))
        return 2;
    BlackboardKeyDefinition persistent_key;
    persistent_key.key = BlackboardKeyId::FromString("game.last_order");
    persistent_key.canonical_name = "game.last_order";
    persistent_key.value_type = TypeId::FromString("game.text_token");
    persistent_key.max_payload_bytes = 16;
    persistent_key.persistence = BlackboardPersistencePolicy::Persistent;
    if (!ai.RegisterBlackboardKey(persistent_key))
        return 3;
    BlackboardKeyDefinition session_key;
    session_key.key = BlackboardKeyId::FromString("game.frame_cache");
    session_key.canonical_name = "game.frame_cache";
    session_key.value_type = TypeId::FromString("game.cache_token");
    session_key.max_payload_bytes = 16;
    session_key.persistence = BlackboardPersistencePolicy::Session;
    if (!ai.RegisterBlackboardKey(session_key))
        return 4;
    if (!ai.Freeze())
        return 5;
    if (ai.RegisterGoal(AIGoalDefinition{}))
        return 6;
    if (!ai.RegisterAgent(guard, profile_id, {10}))
        return 7;

    AIContextSnapshot context;
    context.now = {5};
    context.materialized = true;
    context.runtime_projection_available = true;
    context.targets = {{target_b, AITargetSource::Perceived,
                        {FixedInput(score_key, 900'000, AIAccessFlag::PerceivedState)}},
                       {target_a, AITargetSource::Perceived,
                        {FixedInput(score_key, 400'000, AIAccessFlag::PerceivedState)}}};
    GameplayContext tick1;
    tick1.tick = {1};
    tick1.time = {5};
    auto early = ai.Think(guard, context, tick1);
    if (!early || !early.Value().deferred || early.Value().defer_reason != AIThinkDeferReason::NotDue)
        return 8;
    if (!ai.FindDueAgents({9}, 10).empty() || ai.FindDueAgents({10}, 10).size() != 1)
        return 9;

    context.now = {10};
    tick1.time = {10};
    auto thought = ai.Think(guard, context, tick1);
    if (!thought || !thought.Value().intent || thought.Value().intent->target != target_b)
        return 10;
    const auto intent_id = thought.Value().intent->id;
    GameplayContext t11;
    t11.tick = {2};
    t11.time = {11};
    if (ai.MarkIntentSucceeded(intent_id, t11))
        return 11;
    if (!ai.MarkIntentAccepted(intent_id, t11) || !ai.MarkIntentRunning(intent_id, t11))
        return 12;
    if (!ai.RequestReplan(guard, AIReplanReasonId::FromString("game.new_threat"), {11}, t11))
        return 13;
    GameplayContext t12;
    t12.tick = {3};
    t12.time = {12};
    if (!ai.MarkIntentSucceeded(intent_id, t12))
        return 14;
    const auto *after_success = ai.FindAgent(guard);
    if (!after_success || after_success->activity != AIAgentActivity::Idle || after_success->current_intent ||
        after_success->next_think_at.ticks != 12)
        return 15;

    context.now = {12};
    auto repeat_blocked = ai.Think(guard, context, t12);
    if (!repeat_blocked || repeat_blocked.Value().intent || ai.FindAgent(guard)->next_think_at.ticks != 13)
        return 16;

    if (!ai.SetBlackboard(guard, persistent_key.key, persistent_key.value_type, {std::byte{1}, std::byte{2}}, {12}, t12))
        return 17;
    if (!ai.SetBlackboard(guard, session_key.key, session_key.value_type, {std::byte{3}}, {12}, t12))
        return 18;
    if (!ai.FindBlackboard(guard, persistent_key.key) || !ai.FindBlackboard(guard, session_key.key))
        return 19;

    auto snapshot = ai.CaptureSnapshot();
    if (snapshot.agents.size() != 1 || snapshot.agents.front().blackboard.size() != 1 ||
        snapshot.agents.front().blackboard.front().key != persistent_key.key)
        return 20;

    AIService restored;
    if (!RegisterCoreDefinitions(restored, profile_id, goal_id, intent_type, score_key, AIMaterializationPolicy::AbstractCapable,
                                 GameplayDuration{5}) ||
        !restored.RegisterBlackboardKey(persistent_key) || !restored.RegisterBlackboardKey(session_key) ||
        !restored.Freeze())
        return 21;
    if (!restored.RestoreSnapshot(snapshot))
        return 22;
    if (!restored.FindAgent(guard) || !restored.FindBlackboard(guard, persistent_key.key) ||
        restored.FindBlackboard(guard, session_key.key))
        return 23;

    auto corrupted = snapshot;
    corrupted.intent_ids.scope = 0x9999;
    const auto revision_before_bad_restore = restored.CurrentRevision();
    if (restored.RestoreSnapshot(std::move(corrupted)) || !restored.FindAgent(guard) ||
        restored.CurrentRevision() != revision_before_bad_restore)
        return 24;

    AIService projection_required;
    if (!RegisterCoreDefinitions(projection_required, profile_id, goal_id, intent_type, score_key,
                                 AIMaterializationPolicy::RequiresRuntimeProjection) ||
        !projection_required.Freeze() || !projection_required.RegisterAgent(guard, profile_id))
        return 25;
    AIContextSnapshot abstract_context = context;
    abstract_context.now = {0};
    abstract_context.runtime_projection_available = false;
    auto materialization_deferred = projection_required.Think(guard, abstract_context, {});
    if (!materialization_deferred || !materialization_deferred.Value().deferred ||
        materialization_deferred.Value().defer_reason != AIThinkDeferReason::MaterializationUnavailable)
        return 26;

    AIService budgeted;
    if (!RegisterCoreDefinitions(budgeted, profile_id, goal_id, intent_type, score_key) || !budgeted.Freeze() ||
        !budgeted.RegisterAgent(guard, profile_id))
        return 27;
    budgeted.SetBudget({10, 10, 1, 10});
    AIContextSnapshot many_targets = context;
    many_targets.now = {0};
    GameplayContext budget_tick;
    budget_tick.tick = {100};
    auto budget_result = budgeted.Think(guard, many_targets, budget_tick);
    if (!budget_result || !budget_result.Value().deferred ||
        budget_result.Value().defer_reason != AIThinkDeferReason::BudgetExceeded || budgeted.FindAgent(guard)->current_intent)
        return 28;

    ThrowingEvaluator throwing;
    AIService guarded;
    const auto throwing_evaluator = AIConsiderationEvaluatorId::FromString("test.throwing");
    if (!guarded.RegisterConsiderationEvaluator(throwing_evaluator, throwing))
        return 29;
    AIGoalDefinition throwing_goal;
    throwing_goal.id = AIGoalId::FromString("game.throwing_goal");
    throwing_goal.canonical_name = "game.throwing_goal";
    throwing_goal.intent_type = intent_type;
    throwing_goal.target_policy = AITargetPolicy::Targetless;
    AIConsideration throwing_consideration;
    throwing_consideration.id = AIConsiderationId::FromString("game.throw");
    throwing_consideration.evaluator = throwing_evaluator;
    throwing_consideration.weight_micro = kFixedOne;
    throwing_goal.considerations = {throwing_consideration};
    if (!guarded.RegisterGoal(throwing_goal))
        return 30;
    AIProfile throwing_profile;
    throwing_profile.id = AIProfileId::FromString("game.throwing_profile");
    throwing_profile.canonical_name = "game.throwing_profile";
    throwing_profile.default_goals = {throwing_goal.id};
    if (!guarded.RegisterProfile(throwing_profile) || !guarded.Freeze() || !guarded.RegisterAgent(guard, throwing_profile.id))
        return 31;
    AIContextSnapshot throwing_context;
    throwing_context.now = {0};
    auto throwing_result = guarded.Think(guard, throwing_context, {});
    if (throwing_result || guarded.FindAgent(guard)->activity != AIAgentActivity::Idle ||
        guarded.FindAgent(guard)->current_intent)
        return 32;

    AIService lifecycle;
    if (!RegisterCoreDefinitions(lifecycle, profile_id, goal_id, intent_type, score_key) || !lifecycle.Freeze() ||
        !lifecycle.RegisterAgent(guard, profile_id))
        return 33;
    if (!lifecycle.DisableAgent(guard, {}) || !lifecycle.EnableAgent(guard, {}) || !lifecycle.SuspendAgent(guard, {}) ||
        !lifecycle.ResumeAgent(guard, {0}, {}))
        return 34;
    lifecycle.SetChangeJournalCapacity(3);
    if (!lifecycle.ScheduleThink(guard, {1}) || !lifecycle.ScheduleThink(guard, {2}) ||
        !lifecycle.ScheduleThink(guard, {3}) || !lifecycle.ScheduleThink(guard, {4}))
        return 35;
    auto batch = lifecycle.ReadChangesSince(ChangeCursor{});
    if (!batch.snapshot_required || batch.oldest_available_sequence == 0 || batch.latest_sequence < batch.oldest_available_sequence)
        return 36;

    AIService access_filtered;
    AIAccessPolicy known_only;
    known_only.id = AIAccessPolicyId::FromString("game.known_only");
    known_only.canonical_name = "game.known_only";
    known_only.flags = static_cast<std::uint32_t>(AIAccessFlag::SelfState) |
                       static_cast<std::uint32_t>(AIAccessFlag::KnownState);
    if (!access_filtered.RegisterAccessPolicy(known_only))
        return 37;
    AIGoalDefinition access_goal;
    access_goal.id = goal_id;
    access_goal.canonical_name = "game.attack_target";
    access_goal.intent_type = intent_type;
    access_goal.target_policy = AITargetPolicy::Required;
    AIConsideration access_consideration;
    access_consideration.id = AIConsiderationId::FromString("game.target_score");
    access_consideration.evaluator = AIService::InputEvaluatorId();
    access_consideration.input_key = score_key;
    access_consideration.scope = AIConsiderationScope::Target;
    access_goal.considerations = {access_consideration};
    if (!access_filtered.RegisterGoal(access_goal))
        return 38;
    AIProfile access_profile;
    access_profile.id = profile_id;
    access_profile.canonical_name = "game.guard";
    access_profile.default_goals = {goal_id};
    access_profile.access_policy = known_only.id;
    if (!access_filtered.RegisterProfile(access_profile) || !access_filtered.Freeze() ||
        !access_filtered.RegisterAgent(guard, profile_id))
        return 39;
    AIContextSnapshot perceived_only;
    perceived_only.now = {0};
    perceived_only.targets = {{target_a, AITargetSource::Perceived,
                               {FixedInput(score_key, 900'000, AIAccessFlag::PerceivedState)}}};
    auto filtered_out = access_filtered.Think(guard, perceived_only, {});
    if (!filtered_out || filtered_out.Value().intent)
        return 40;
    if (!access_filtered.ScheduleThink(guard, {1}))
        return 41;
    AIContextSnapshot known_context;
    known_context.now = {1};
    known_context.targets = {{target_b, AITargetSource::Known,
                              {FixedInput(score_key, 900'000, AIAccessFlag::KnownState)}}};
    auto known_selected = access_filtered.Think(guard, known_context, {});
    if (!known_selected || !known_selected.Value().intent || known_selected.Value().intent->target != target_b)
        return 42;

    AIService live_source;
    if (!RegisterCoreDefinitions(live_source, profile_id, goal_id, intent_type, score_key) || !live_source.Freeze() ||
        !live_source.RegisterAgent(guard, profile_id))
        return 43;
    AIContextSnapshot live_context = context;
    live_context.now = {0};
    auto live_think = live_source.Think(guard, live_context, {});
    if (!live_think || !live_think.Value().intent)
        return 44;
    auto live_snapshot = live_source.CaptureSnapshot();
    live_snapshot.intent_ids.next = 1;
    AIService transactional_restore;
    if (!RegisterCoreDefinitions(transactional_restore, profile_id, goal_id, intent_type, score_key) ||
        !transactional_restore.Freeze() || !transactional_restore.RegisterAgent(Ref("existing"), profile_id))
        return 45;
    if (transactional_restore.RestoreSnapshot(std::move(live_snapshot)) ||
        !transactional_restore.FindAgent(Ref("existing")))
        return 46;

    // Change sequence namespace survives restore even though the retained journal does not.
    AIService sequence_source;
    if (!RegisterCoreDefinitions(sequence_source, profile_id, goal_id, intent_type, score_key) ||
        !sequence_source.Freeze() || !sequence_source.RegisterAgent(guard, profile_id) ||
        !sequence_source.ScheduleThink(guard, {3}))
        return 47;
    const auto saved_change_cursor = sequence_source.ReadChangesSince(ChangeCursor{}).latest_cursor;
    auto sequence_snapshot = sequence_source.CaptureSnapshot();
    AIService sequence_restored;
    if (!RegisterCoreDefinitions(sequence_restored, profile_id, goal_id, intent_type, score_key) ||
        !sequence_restored.Freeze() || !sequence_restored.RestoreSnapshot(sequence_snapshot))
        return 48;
    if (!sequence_restored.ReadChangesSince(saved_change_cursor).snapshot_required)
        return 50;
    const auto restored_change_cursor = sequence_restored.LatestChangeCursor();
    if (!sequence_restored.RegisterAgent(Ref("sequence.other"), profile_id))
        return 49;
    auto resumed_changes = sequence_restored.ReadChangesSince(restored_change_cursor);
    if (resumed_changes.snapshot_required || resumed_changes.changes.empty() ||
        resumed_changes.changes.front().sequence <= restored_change_cursor.sequence)
        return 51;
    if (restored_change_cursor.sequence > 0 &&
        !sequence_restored.ReadChangesSince(restored_change_cursor.AtSequence(restored_change_cursor.sequence - 1)).snapshot_required)
        return 52;
    const auto restored_latest = resumed_changes.latest_cursor;
    if (restored_latest.sequence != std::numeric_limits<std::uint64_t>::max() &&
        !sequence_restored.ReadChangesSince(restored_latest.AtSequence(restored_latest.sequence + 1)).snapshot_required)
        return 56;

    sequence_snapshot.next_change_sequence = std::numeric_limits<std::uint64_t>::max();
    AIService exhausted_sequence;
    if (!RegisterCoreDefinitions(exhausted_sequence, profile_id, goal_id, intent_type, score_key) ||
        !exhausted_sequence.Freeze() || !exhausted_sequence.RestoreSnapshot(sequence_snapshot))
        return 53;
    if (!exhausted_sequence.RegisterAgent(Ref("sequence.max"), profile_id))
        return 54;
    auto max_batch = exhausted_sequence.ReadChangesSince(exhausted_sequence.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max() - 1));
    if (max_batch.snapshot_required || max_batch.changes.size() != 1 ||
        max_batch.changes.front().sequence != std::numeric_limits<std::uint64_t>::max())
        return 55;

    return 0;
}
