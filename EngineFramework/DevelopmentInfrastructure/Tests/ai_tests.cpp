#include "Epidemic/GameFramework/AI/ai.h"
#include "allocation_fault_injection.h"

#include <limits>
#include <stdexcept>
#include <utility>

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


class FlakyEvaluator final : public IAIConsiderationEvaluator
{
  public:
    mutable std::uint32_t calls = 0;
    mutable bool fail_next = true;

    epidemic::foundation::Result<Fixed> Evaluate(const AIConsideration &, const AIContextSnapshot &,
                                                 const AITargetCandidate *) const override
    {
        ++calls;
        if (fail_next)
        {
            fail_next = false;
            return epidemic::foundation::Result<Fixed>::Failure(
                epidemic::foundation::Error::Create("test.flaky", "forced evaluator failure"));
        }
        return epidemic::foundation::Result<Fixed>::Success(kFixedOne);
    }
};

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

    // AI-01/09: invalid caller context must not consume the tick's agent budget.
    AIService invalid_budget;
    if (!RegisterCoreDefinitions(invalid_budget, profile_id, goal_id, intent_type, score_key) ||
        !invalid_budget.Freeze() || !invalid_budget.RegisterAgent(guard, profile_id))
        return 57;
    invalid_budget.SetBudget({1, 10, 10, 10});
    AIContextSnapshot duplicate_context;
    duplicate_context.now = {0};
    duplicate_context.targets = {
        {target_a, AITargetSource::Perceived, {FixedInput(score_key, 500'000, AIAccessFlag::PerceivedState)}},
        {target_a, AITargetSource::Perceived, {FixedInput(score_key, 600'000, AIAccessFlag::PerceivedState)}}};
    GameplayContext same_tick;
    same_tick.tick = {700};
    same_tick.time = {0};
    if (invalid_budget.Think(guard, duplicate_context, same_tick))
        return 58;
    AIContextSnapshot valid_after_invalid;
    valid_after_invalid.now = {0};
    valid_after_invalid.targets = {
        {target_a, AITargetSource::Perceived, {FixedInput(score_key, 700'000, AIAccessFlag::PerceivedState)}}};
    auto valid_same_tick = invalid_budget.Think(guard, valid_after_invalid, same_tick);
    if (!valid_same_tick || !valid_same_tick.Value().intent)
        return 59;

    // AI-02/09: evaluator failure must not commit staged budget counters.
    FlakyEvaluator flaky;
    AIService evaluator_budget;
    const auto flaky_id = AIConsiderationEvaluatorId::FromString("test.flaky");
    if (!evaluator_budget.RegisterConsiderationEvaluator(flaky_id, flaky))
        return 60;
    AIGoalDefinition flaky_goal;
    flaky_goal.id = AIGoalId::FromString("game.flaky_goal");
    flaky_goal.canonical_name = "game.flaky_goal";
    flaky_goal.intent_type = intent_type;
    flaky_goal.target_policy = AITargetPolicy::Targetless;
    AIConsideration flaky_consideration;
    flaky_consideration.id = AIConsiderationId::FromString("game.flaky_consideration");
    flaky_consideration.evaluator = flaky_id;
    flaky_consideration.scope = AIConsiderationScope::Context;
    flaky_consideration.weight_micro = kFixedOne;
    flaky_goal.considerations = {flaky_consideration};
    if (!evaluator_budget.RegisterGoal(flaky_goal))
        return 61;
    AIProfile flaky_profile;
    flaky_profile.id = AIProfileId::FromString("game.flaky_profile");
    flaky_profile.canonical_name = "game.flaky_profile";
    flaky_profile.default_goals = {flaky_goal.id};
    if (!evaluator_budget.RegisterProfile(flaky_profile) || !evaluator_budget.Freeze() ||
        !evaluator_budget.RegisterAgent(guard, flaky_profile.id))
        return 62;
    evaluator_budget.SetBudget({1, 1, 1, 1});
    AIContextSnapshot flaky_context;
    flaky_context.now = {0};
    GameplayContext flaky_tick;
    flaky_tick.tick = {701};
    flaky_tick.time = {0};
    if (evaluator_budget.Think(guard, flaky_context, flaky_tick))
        return 63;
    auto flaky_retry = evaluator_budget.Think(guard, flaky_context, flaky_tick);
    if (!flaky_retry || !flaky_retry.Value().intent || flaky.calls != 2)
        return 64;

    // AI-03/04/05/09: allocation failures in staged registry/index publication are controlled and atomic.
    AIService profile_atomic;
    AIGoalDefinition profile_goal;
    profile_goal.id = AIGoalId::FromString("game.profile_atomic_goal");
    profile_goal.canonical_name = "game.profile_atomic_goal";
    profile_goal.intent_type = intent_type;
    profile_goal.target_policy = AITargetPolicy::Targetless;
    if (!profile_atomic.RegisterGoal(profile_goal))
        return 65;
    AIProfile atomic_profile;
    atomic_profile.id = AIProfileId::FromString("game.profile_atomic");
    atomic_profile.canonical_name = "game.profile_atomic";
    atomic_profile.default_goals = {profile_goal.id};
    const auto profile_revision_before = profile_atomic.CurrentRevision();
    {
        epidemic::tests::allocation_fault::FailAfter fault(0);
        auto registered = profile_atomic.RegisterProfile(std::move(atomic_profile));
        if (registered)
            return 66;
    }
    if (profile_atomic.FindProfile(atomic_profile.id) || profile_atomic.CurrentRevision() != profile_revision_before)
        return 67;

    AIService agent_atomic;
    if (!RegisterCoreDefinitions(agent_atomic, profile_id, goal_id, intent_type, score_key) || !agent_atomic.Freeze())
        return 68;
    const auto agent_revision_before = agent_atomic.CurrentRevision();
    {
        epidemic::tests::allocation_fault::FailAfter fault(0);
        auto registered = agent_atomic.RegisterAgent(guard, profile_id, {10});
        if (registered)
            return 69;
    }
    if (agent_atomic.FindAgent(guard) || agent_atomic.CurrentRevision() != agent_revision_before ||
        !agent_atomic.FindDueAgents({100}, 10).empty())
        return 70;
    if (!agent_atomic.RegisterAgent(guard, profile_id, {10}))
        return 71;
    const auto schedule_revision_before = agent_atomic.CurrentRevision();
    const auto schedule_time_before = agent_atomic.FindAgent(guard)->next_think_at;
    {
        epidemic::tests::allocation_fault::FailAfter fault(0);
        auto scheduled = agent_atomic.ScheduleThink(guard, {20});
        if (scheduled)
            return 72;
    }
    if (agent_atomic.CurrentRevision() != schedule_revision_before ||
        agent_atomic.FindAgent(guard)->next_think_at != schedule_time_before ||
        agent_atomic.FindDueAgents({10}, 10).size() != 1)
        return 73;

    // AI-06/09: failure while staging the selected intent leaves agent/index/ID/revision unchanged.
    bool saw_decision_allocation_failure = false;
    for (long long fail_after = 0; fail_after < 32 && !saw_decision_allocation_failure; ++fail_after)
    {
        AIService decision_atomic;
        if (!RegisterCoreDefinitions(decision_atomic, profile_id, goal_id, intent_type, score_key) ||
            !decision_atomic.Freeze() || !decision_atomic.RegisterAgent(guard, profile_id))
            return 74;
        const auto before = decision_atomic.CaptureSnapshot();
        AIContextSnapshot decision_context;
        decision_context.now = {0};
        decision_context.targets = {
            {target_a, AITargetSource::Perceived, {FixedInput(score_key, 800'000, AIAccessFlag::PerceivedState)}}};
        GameplayContext decision_tick;
        decision_tick.tick = {702};
        epidemic::foundation::Result<AIThinkResult> result = epidemic::foundation::Result<AIThinkResult>::Failure(
            epidemic::foundation::Error::Create("test", "not run"));
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            result = decision_atomic.Think(guard, decision_context, decision_tick);
        }
        if (!result && result.GetError().HasCode("gameplay.ai.allocation_failed"))
        {
            saw_decision_allocation_failure = true;
            const auto after = decision_atomic.CaptureSnapshot();
            if (after.revision != before.revision || after.intent_ids.scope != before.intent_ids.scope ||
                after.intent_ids.next != before.intent_ids.next || after.agents.size() != 1 ||
                after.agents.front().current_intent || after.agents.front().activity != AIAgentActivity::Idle)
                return 75;
        }
    }
    if (!saw_decision_allocation_failure)
        return 76;

    // AI-07/09: revision exhaustion is a hard mutation boundary.
    AIService revision_source;
    if (!RegisterCoreDefinitions(revision_source, profile_id, goal_id, intent_type, score_key) ||
        !revision_source.Freeze() || !revision_source.RegisterAgent(guard, profile_id, {1}))
        return 77;
    auto exhausted_snapshot = revision_source.CaptureSnapshot();
    exhausted_snapshot.revision.value = std::numeric_limits<std::uint64_t>::max();
    exhausted_snapshot.agents.front().revision = exhausted_snapshot.revision;
    AIService revision_target;
    if (!RegisterCoreDefinitions(revision_target, profile_id, goal_id, intent_type, score_key) ||
        !revision_target.Freeze() || !revision_target.RestoreSnapshot(exhausted_snapshot))
        return 78;
    const auto exhausted_time = revision_target.FindAgent(guard)->next_think_at;
    if (revision_target.ScheduleThink(guard, {2}) ||
        revision_target.CurrentRevision().value != std::numeric_limits<std::uint64_t>::max() ||
        revision_target.FindAgent(guard)->next_think_at != exhausted_time)
        return 79;

    return 0;
}
