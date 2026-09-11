#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace epidemic::gameplay::ai
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] Fixed ClampUnit(Fixed value) noexcept
{
    return std::clamp<Fixed>(value, 0, kFixedOne);
}

[[nodiscard]] Fixed SaturatingAddFixed(Fixed a, Fixed b) noexcept
{
    if (b > 0 && a > std::numeric_limits<Fixed>::max() - b)
        return std::numeric_limits<Fixed>::max();
    if (b < 0 && a < std::numeric_limits<Fixed>::min() - b)
        return std::numeric_limits<Fixed>::min();
    return a + b;
}

[[nodiscard]] bool InputLess(const AIInputValue &a, const AIInputValue &b) noexcept
{
    return a.key < b.key;
}

[[nodiscard]] const AIInputValue *FindInput(const std::vector<AIInputValue> &values, AIInputKeyId key) noexcept
{
    auto it = std::lower_bound(values.begin(), values.end(), key,
                               [](const AIInputValue &value, AIInputKeyId wanted) { return value.key < wanted; });
    return it != values.end() && it->key == key ? &*it : nullptr;
}

class InputEvaluator final : public IAIConsiderationEvaluator
{
  public:
    [[nodiscard]] foundation::Result<Fixed> Evaluate(const AIConsideration &definition,
                                                     const AIContextSnapshot &context,
                                                     const AITargetCandidate *target) const override
    {
        if (!definition.input_key.IsValid())
            return foundation::Result<Fixed>::Failure(
                Error("gameplay.ai.input_key_invalid", "input evaluator requires a valid input key"));
        const AIInputValue *input = nullptr;
        if (definition.scope == AIConsiderationScope::Target)
        {
            if (target)
                input = FindInput(target->inputs, definition.input_key);
        }
        else
        {
            input = FindInput(context.inputs, definition.input_key);
        }
        if (!input)
            return foundation::Result<Fixed>::Success(0);
        switch (input->kind)
        {
        case AIInputValueKind::Fixed:
            return foundation::Result<Fixed>::Success(ClampUnit(input->fixed_value));
        case AIInputValueKind::Boolean:
            return foundation::Result<Fixed>::Success(input->bool_value ? kFixedOne : 0);
        default:
            return foundation::Result<Fixed>::Failure(Error(
                "gameplay.ai.input_type_unsupported", "builtin input evaluator supports fixed and boolean values"));
        }
    }
};

const InputEvaluator kInputEvaluator{};

[[nodiscard]] AIAccessFlag AccessForTargetSource(AITargetSource source) noexcept
{
    switch (source)
    {
    case AITargetSource::Perceived:
        return AIAccessFlag::PerceivedState;
    case AITargetSource::Known:
    case AITargetSource::LastKnown:
        return AIAccessFlag::KnownState;
    case AITargetSource::Public:
        return AIAccessFlag::PublicFacts;
    case AITargetSource::Group:
        return AIAccessFlag::GroupKnowledge;
    }
    return AIAccessFlag::SelfState;
}

[[nodiscard]] TypeId ReplanReasonType(AIReplanReasonId reason) noexcept
{
    return reason.value;
}

[[nodiscard]] bool IsLiveIntentStatus(AIIntentStatus status) noexcept
{
    return status == AIIntentStatus::Issued || status == AIIntentStatus::Accepted || status == AIIntentStatus::Running;
}
} // namespace

AIService::AIService()
{
    changes_.reserve(kMaxChangeJournalCapacity);
    auto basic = AIAccessPolicy{
        AIAccessPolicyId::FromString("framework.ai.access.default"), "framework.ai.access.default",
        static_cast<std::uint32_t>(AIAccessFlag::SelfState) | static_cast<std::uint32_t>(AIAccessFlag::PerceivedState) |
            static_cast<std::uint32_t>(AIAccessFlag::KnownState)};
    policies_.emplace(basic.id, basic);
    evaluators_.emplace(InputEvaluatorId(), &kInputEvaluator);
}

foundation::Result<void> AIService::RegisterAccessPolicy(AIAccessPolicy policy)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!policy.id.IsValid() || policy.canonical_name.empty() || policies_.contains(policy.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_policy", "invalid or duplicate access policy"));
    policies_.emplace(policy.id, std::move(policy));
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::RegisterConsiderationEvaluator(AIConsiderationEvaluatorId id,
                                                                   const IAIConsiderationEvaluator &evaluator)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!id.IsValid() || evaluators_.contains(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_evaluator", "invalid or duplicate consideration evaluator"));
    evaluators_.emplace(id, &evaluator);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::RegisterGoal(AIGoalDefinition goal)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!goal.id.IsValid() || goal.canonical_name.empty() || !goal.intent_type.IsValid() || goals_.contains(goal.id))
        return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_goal", "invalid or duplicate goal"));
    std::sort(goal.considerations.begin(), goal.considerations.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    goals_.emplace(goal.id, std::move(goal));
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::RegisterProfile(AIProfile profile)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!profile.id.IsValid() || profile.canonical_name.empty() || profiles_.contains(profile.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_profile", "invalid or duplicate ai profile"));
    if (!CanBump())
        return foundation::Result<void>::Failure(Error("gameplay.ai.revision_exhausted", "ai revision exhausted"));
    if (!profile.access_policy.IsValid())
        profile.access_policy = AIAccessPolicyId::FromString("framework.ai.access.default");
    std::sort(profile.default_goals.begin(), profile.default_goals.end());
    const Revision next{revision_.value + 1};
    profile.revision = next;
    try
    {
        auto staged = profiles_;
        staged.emplace(profile.id, std::move(profile));
        profiles_.swap(staged);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.allocation_failed", "failed to publish ai profile"));
    }
    revision_ = next;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::RegisterBlackboardKey(BlackboardKeyDefinition definition)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!definition.key.IsValid() || definition.canonical_name.empty() || !definition.value_type.IsValid() ||
        definition.max_payload_bytes == 0 || blackboard_keys_.contains(definition.key))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_blackboard_key", "invalid or duplicate blackboard key definition"));
    blackboard_keys_.emplace(definition.key, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::Freeze()
{
    if (frozen_)
        return foundation::Result<void>::Success();
    for (const auto &[id, goal] : goals_)
    {
        (void)id;
        AIConsiderationId previous{};
        for (const auto &consideration : goal.considerations)
        {
            if (!consideration.id.IsValid() || !consideration.evaluator.IsValid() ||
                !evaluators_.contains(consideration.evaluator))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.invalid_consideration", "goal contains an invalid consideration"));
            if (previous.IsValid() && previous == consideration.id)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.duplicate_consideration", "goal contains duplicate consideration ids"));
            previous = consideration.id;
            constexpr Fixed kMaxAbsWeight = 1'000'000'000;
            if (consideration.weight_micro < -kMaxAbsWeight || consideration.weight_micro > kMaxAbsWeight)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.invalid_weight", "consideration weight is outside the supported fixed range"));
            if (consideration.evaluator == InputEvaluatorId() && !consideration.input_key.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.invalid_input_key", "builtin input consideration requires an input key"));
            if (goal.target_policy == AITargetPolicy::Targetless && consideration.scope == AIConsiderationScope::Target)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.invalid_target_policy", "targetless goal contains a target consideration"));
        }
    }
    for (const auto &[id, profile] : profiles_)
    {
        (void)id;
        if (!policies_.contains(profile.access_policy))
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.unknown_policy", "profile references unknown policy"));
        const auto *access_policy = FindAccessPolicy(profile.access_policy);
        if (access_policy && (access_policy->flags & static_cast<std::uint32_t>(AIAccessFlag::OmniscientDebug)) != 0 &&
            !profile.debug_profile)
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.debug_access_forbidden", "omniscient access is restricted to debug profiles"));
        if (profile.think_interval.ticks < 0 || profile.goal_repeat_cooldown.ticks < 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.invalid_profile_timing", "ai profile timing values must be non-negative"));
        AIGoalId previous{};
        for (auto goal : profile.default_goals)
        {
            if (!goals_.contains(goal))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.unknown_goal", "profile references unknown goal"));
            if (previous.IsValid() && previous == goal)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.duplicate_goal", "profile contains duplicate goals"));
            previous = goal;
        }
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}

const AIAccessPolicy *AIService::FindAccessPolicy(AIAccessPolicyId id) const noexcept
{
    auto it = policies_.find(id);
    return it == policies_.end() ? nullptr : &it->second;
}
const AIGoalDefinition *AIService::FindGoal(AIGoalId id) const noexcept
{
    auto it = goals_.find(id);
    return it == goals_.end() ? nullptr : &it->second;
}
const AIProfile *AIService::FindProfile(AIProfileId id) const noexcept
{
    auto it = profiles_.find(id);
    return it == profiles_.end() ? nullptr : &it->second;
}
const BlackboardKeyDefinition *AIService::FindBlackboardKey(BlackboardKeyId id) const noexcept
{
    auto it = blackboard_keys_.find(id);
    return it == blackboard_keys_.end() ? nullptr : &it->second;
}

foundation::Result<void> AIService::RequireFrozen() const
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.registry_not_frozen", "ai definitions must be frozen"));
    return foundation::Result<void>::Success();
}

bool AIService::CanBump() const noexcept
{
    return revision_.value != std::numeric_limits<std::uint64_t>::max();
}

bool AIService::Bump() noexcept
{
    if (!CanBump())
        return false;
    ++revision_.value;
    return true;
}

bool AIService::CanRecord(std::size_t count) const noexcept
{
    if (count == 0)
        return true;
    if (next_change_sequence_ == 0)
        return false;
    return count <= std::numeric_limits<std::uint64_t>::max() - next_change_sequence_ + 1;
}

foundation::Result<void> AIService::RegisterAgent(GameplayObjectRef subject, AIProfileId profile,
                                                  GameplayTimePoint next_think)
{
    if (auto frozen = RequireFrozen(); !frozen)
        return frozen;
    if (!subject.IsValid() || !profiles_.contains(profile) || agents_.contains(subject))
        return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_agent", "invalid or duplicate ai agent"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    const Revision next{revision_.value + 1};
    AIAgentState state;
    state.subject = subject;
    state.profile = profile;
    state.activity = AIAgentActivity::Idle;
    state.next_think_at = next_think;
    state.revision = next;
    bool agent_inserted = false;
    try
    {
        const auto [agent_it, inserted] = agents_.emplace(subject, std::move(state));
        if (!inserted)
            return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_agent", "duplicate ai agent"));
        agent_inserted = true;
        const auto [due_it, due_inserted] = due_agents_.insert(DueKey{agent_it->second.next_think_at, subject});
        (void)due_it;
        if (!due_inserted)
        {
            agents_.erase(agent_it);
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.schedule_collision", "duplicate ai schedule entry"));
        }
    }
    catch (const std::bad_alloc &)
    {
        if (agent_inserted)
            agents_.erase(subject);
        return foundation::Result<void>::Failure(Error("gameplay.ai.allocation_failed", "failed to publish ai agent"));
    }
    revision_ = next;
    Record({0, AIChangeKind::AgentRegistered, subject, {}, {}, {}, {}, {}, next});
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::UnregisterAgent(GameplayObjectRef subject, GameplayContext context)
{
    auto it = agents_.find(subject);
    if (it == agents_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    RemoveDueIndex(it->second);
    if (it->second.current_intent)
        intent_to_agent_.erase(it->second.current_intent->id);
    agents_.erase(it);
    (void)Bump();
    Record({0, AIChangeKind::AgentUnregistered, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}

const AIAgentState *AIService::FindAgent(GameplayObjectRef subject) const noexcept
{
    auto it = agents_.find(subject);
    return it == agents_.end() ? nullptr : &it->second;
}
AIAgentState *AIService::FindMutableAgent(GameplayObjectRef subject) noexcept
{
    auto it = agents_.find(subject);
    return it == agents_.end() ? nullptr : &it->second;
}

void AIService::RemoveDueIndex(const AIAgentState &agent) noexcept
{
    if (agent.activity == AIAgentActivity::Idle)
        due_agents_.erase(DueKey{agent.next_think_at, agent.subject});
}
void AIService::AddDueIndex(const AIAgentState &agent)
{
    if (agent.activity == AIAgentActivity::Idle)
        due_agents_.insert(DueKey{agent.next_think_at, agent.subject});
}
foundation::Result<void> AIService::SetNextThink(AIAgentState &agent, GameplayTimePoint when)
{
    if (agent.next_think_at == when)
        return foundation::Result<void>::Success();
    if (agent.activity == AIAgentActivity::Idle)
    {
        try
        {
            const auto [it, inserted] = due_agents_.insert(DueKey{when, agent.subject});
            (void)it;
            if (!inserted)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.schedule_collision", "ai due index already contains the requested key"));
        }
        catch (const std::bad_alloc &)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.allocation_failed", "failed to update ai due index"));
        }
        due_agents_.erase(DueKey{agent.next_think_at, agent.subject});
    }
    agent.next_think_at = when;
    return foundation::Result<void>::Success();
}GameplayTimePoint AIService::NextThinkAfter(const AIProfile &profile, GameplayTimePoint now) const noexcept
{
    return SaturatingAdd(now, profile.think_interval);
}

foundation::Result<void> AIService::DisableAgent(GameplayObjectRef subject, GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (agent->current_intent)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.intent_active", "cancel the current intent before disabling the agent"));
    if (agent->activity == AIAgentActivity::Disabled)
        return foundation::Result<void>::Success();
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    RemoveDueIndex(*agent);
    (void)Bump();
    agent->activity = AIAgentActivity::Disabled;
    agent->revision = revision_;
    Record({0, AIChangeKind::AgentDisabled, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::EnableAgent(GameplayObjectRef subject, GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (agent->activity != AIAgentActivity::Disabled)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_not_disabled", "agent is not disabled"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    const auto when = agent->next_think_at < context.time ? context.time : agent->next_think_at;
    std::set<DueKey> staged_due;
    try
    {
        staged_due = due_agents_;
        staged_due.insert(DueKey{when, subject});
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.ai.allocation_failed", "failed to enable ai agent"));
    }
    due_agents_.swap(staged_due);
    (void)Bump();
    agent->activity = AIAgentActivity::Idle;
    agent->next_think_at = when;
    agent->revision = revision_;
    Record({0, AIChangeKind::AgentEnabled, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::SuspendAgent(GameplayObjectRef subject, GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (agent->current_intent)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.intent_active", "cancel the current intent before suspending the agent"));
    if (agent->activity == AIAgentActivity::Suspended)
        return foundation::Result<void>::Success();
    if (agent->activity == AIAgentActivity::Disabled)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.agent_disabled", "disabled agent cannot be suspended"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    RemoveDueIndex(*agent);
    (void)Bump();
    agent->activity = AIAgentActivity::Suspended;
    agent->revision = revision_;
    Record({0, AIChangeKind::AgentSuspended, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::ResumeAgent(GameplayObjectRef subject, GameplayTimePoint when,
                                                GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (agent->activity != AIAgentActivity::Suspended)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_not_suspended", "agent is not suspended"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    std::set<DueKey> staged_due;
    try
    {
        staged_due = due_agents_;
        staged_due.insert(DueKey{when, subject});
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.ai.allocation_failed", "failed to resume ai agent"));
    }
    due_agents_.swap(staged_due);
    (void)Bump();
    agent->activity = AIAgentActivity::Idle;
    agent->next_think_at = when;
    agent->revision = revision_;
    Record({0, AIChangeKind::AgentResumed, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::ScheduleThink(GameplayObjectRef subject, GameplayTimePoint when,
                                                  GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    auto moved = SetNextThink(*agent, when);
    if (!moved)
        return moved;
    (void)Bump();
    agent->revision = revision_;
    Record({0, AIChangeKind::ThinkScheduled, subject, {}, {}, {}, {}, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::RequestReplan(GameplayObjectRef subject, AIReplanReasonId reason,
                                                  GameplayTimePoint when, GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    if (!reason.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_replan_reason", "invalid replan reason"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    if (!agent->current_intent && agent->activity == AIAgentActivity::Idle && when < agent->next_think_at)
    {
        auto moved = SetNextThink(*agent, when);
        if (!moved)
            return moved;
    }
    (void)Bump();
    if (agent->current_intent)
    {
        agent->pending_replan = true;
        agent->pending_replan_reason = reason;
        agent->pending_replan_at = when;
    }
    agent->revision = revision_;
    if (diagnostics_.replans_requested != std::numeric_limits<std::uint64_t>::max())
        ++diagnostics_.replans_requested;
    Record({0, AIChangeKind::ReplanRequested, subject, agent->active_goal ? agent->active_goal->id : AIGoalId{},
            agent->current_intent ? agent->current_intent->id : AIIntentId{},
            agent->current_intent ? agent->current_intent->type : AIIntentTypeId{}, ReplanReasonType(reason), context,
            revision_});
    return foundation::Result<void>::Success();
}

bool AIService::AccessAllows(const AIProfile &profile, AIAccessFlag flag) const noexcept
{
    auto *policy = FindAccessPolicy(profile.access_policy);
    return policy && (policy->flags & static_cast<std::uint32_t>(flag)) != 0;
}
bool AIService::TargetSourceAllowed(const AIProfile &profile, AITargetSource source) const noexcept
{
    return AccessAllows(profile, AccessForTargetSource(source));
}

foundation::Result<Fixed> AIService::ScoreGoal(const AIProfile &profile, const AIGoalDefinition &goal,
                                               const AIContextSnapshot &context, const AITargetCandidate *target)
{
    (void)profile;
    Fixed score = goal.base_priority_micro;
    for (const auto &consideration : goal.considerations)
    {
        auto evaluator_it = evaluators_.find(consideration.evaluator);
        if (evaluator_it == evaluators_.end() || evaluator_it->second == nullptr)
            return foundation::Result<Fixed>::Failure(
                Error("gameplay.ai.evaluator_missing", "consideration evaluator is unavailable"));
        try
        {
            auto evaluated = evaluator_it->second->Evaluate(consideration, context, target);
            if (!evaluated)
                return foundation::Result<Fixed>::Failure(evaluated.GetError());
            if (evaluated.Value() < 0 || evaluated.Value() > kFixedOne)
            {
                return foundation::Result<Fixed>::Failure(Error(
                    "gameplay.ai.evaluator_range", "consideration evaluator returned a value outside [0, 1000000]"));
            }
            const auto contribution = (evaluated.Value() * consideration.weight_micro) / kFixedOne;
            score = SaturatingAddFixed(score, contribution);
        }
        catch (const std::exception &)
        {
            return foundation::Result<Fixed>::Failure(
                Error("gameplay.ai.evaluator_exception", "consideration evaluator threw an exception"));
        }
        catch (...)
        {
            return foundation::Result<Fixed>::Failure(
                Error("gameplay.ai.evaluator_exception", "consideration evaluator threw an unknown exception"));
        }
    }
    return foundation::Result<Fixed>::Success(score);
}

foundation::Result<AIThinkResult> AIService::Think(GameplayObjectRef subject, const AIContextSnapshot &source_context,
                                                   GameplayContext gameplay_context)
{
    if (auto frozen = RequireFrozen(); !frozen)
        return foundation::Result<AIThinkResult>::Failure(frozen.GetError());
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<AIThinkResult>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    auto *profile = FindProfile(agent->profile);
    if (!profile)
        return foundation::Result<AIThinkResult>::Failure(Error("gameplay.ai.profile_missing", "profile missing"));
    auto deferred = [&](AIThinkDeferReason reason) {
        return foundation::Result<AIThinkResult>::Success(AIThinkResult{subject, {}, {}, true, reason, revision_});
    };
    if (agent->activity == AIAgentActivity::Disabled)
        return deferred(AIThinkDeferReason::Disabled);
    if (agent->activity == AIAgentActivity::Suspended)
        return deferred(AIThinkDeferReason::Suspended);
    if (agent->current_intent || agent->activity == AIAgentActivity::ExecutingIntent)
        return deferred(AIThinkDeferReason::ActiveIntent);
    if (source_context.now < agent->next_think_at)
        return deferred(AIThinkDeferReason::NotDue);
    if (profile->materialization_policy == AIMaterializationPolicy::RequiresMaterialized &&
        !source_context.materialized)
        return deferred(AIThinkDeferReason::MaterializationUnavailable);
    if (profile->materialization_policy == AIMaterializationPolicy::RequiresRuntimeProjection &&
        !source_context.runtime_projection_available)
        return deferred(AIThinkDeferReason::MaterializationUnavailable);

    AIContextSnapshot context;
    try
    {
        context.now = source_context.now;
        context.materialized = source_context.materialized;
        context.runtime_projection_available = source_context.runtime_projection_available;
        for (const auto &input : source_context.inputs)
            if (input.key.IsValid() && AccessAllows(*profile, input.access))
                context.inputs.push_back(input);
        std::sort(context.inputs.begin(), context.inputs.end(), InputLess);
        for (std::size_t i = 1; i < context.inputs.size(); ++i)
            if (context.inputs[i - 1].key == context.inputs[i].key)
                return foundation::Result<AIThinkResult>::Failure(
                    Error("gameplay.ai.invalid_context", "ai context contains duplicate global input keys"));
        for (auto candidate : source_context.targets)
        {
            if (!candidate.target.IsValid() || !TargetSourceAllowed(*profile, candidate.source))
                continue;
            candidate.inputs.erase(std::remove_if(candidate.inputs.begin(), candidate.inputs.end(),
                                                  [&](const AIInputValue &input) {
                                                      return !input.key.IsValid() ||
                                                             !AccessAllows(*profile, input.access);
                                                  }),
                                   candidate.inputs.end());
            std::sort(candidate.inputs.begin(), candidate.inputs.end(), InputLess);
            for (std::size_t i = 1; i < candidate.inputs.size(); ++i)
                if (candidate.inputs[i - 1].key == candidate.inputs[i].key)
                    return foundation::Result<AIThinkResult>::Failure(
                        Error("gameplay.ai.invalid_context", "ai target candidate contains duplicate input keys"));
            context.targets.push_back(std::move(candidate));
        }
        std::sort(context.targets.begin(), context.targets.end(),
                  [](const AITargetCandidate &a, const AITargetCandidate &b) {
                      if (a.target != b.target)
                          return a.target < b.target;
                      return a.source < b.source;
                  });
        for (std::size_t i = 1; i < context.targets.size(); ++i)
            if (context.targets[i - 1].target == context.targets[i].target &&
                context.targets[i - 1].source == context.targets[i].source)
                return foundation::Result<AIThinkResult>::Failure(
                    Error("gameplay.ai.invalid_context", "ai context contains duplicate target candidates"));
    }
    catch (const std::exception &e)
    {
        return foundation::Result<AIThinkResult>::Failure(
            foundation::Error::Create("gameplay.ai.context_exception", "failed to canonicalize ai context", e.what()));
    }
    catch (...)
    {
        return foundation::Result<AIThinkResult>::Failure(
            Error("gameplay.ai.context_exception", "failed to canonicalize ai context"));
    }

    AITickBudgetState staged_budget = tick_budget_;
    if (!gameplay_context.tick.IsValid() || staged_budget.tick != gameplay_context.tick)
    {
        staged_budget = {};
        staged_budget.tick = gameplay_context.tick;
    }
    auto staged_diagnostics = diagnostics_;
    if (staged_budget.agents_thought >= budget_.max_agents_thinking_per_tick)
    {
        if (!CanRecord())
            return foundation::Result<AIThinkResult>::Failure(
                Error("gameplay.ai.journal_exhausted", "ai journal exhausted"));
        if (staged_diagnostics.budget_exhaustions != std::numeric_limits<std::uint64_t>::max())
            ++staged_diagnostics.budget_exhaustions;
        tick_budget_ = staged_budget;
        diagnostics_ = staged_diagnostics;
        Record({0, AIChangeKind::BudgetExceeded, subject, {}, {}, {}, {}, gameplay_context, revision_});
        return deferred(AIThinkDeferReason::BudgetExceeded);
    }
    ++staged_budget.agents_thought;
    if (staged_diagnostics.agents_thinking != std::numeric_limits<std::uint64_t>::max())
        ++staged_diagnostics.agents_thinking;
    Fixed best_score = std::numeric_limits<Fixed>::min();
    AIGoalId best_goal{};
    GameplayObjectRef best_target{};
    AITargetSource best_source = AITargetSource::Perceived;
    const AIGoalDefinition *best_definition = nullptr;
    bool budget_exhausted = false;
    auto evaluate_pair = [&](const AIGoalDefinition &goal,
                             const AITargetCandidate *target) -> foundation::Result<void> {
        if (staged_budget.target_evaluations >= budget_.max_target_evaluations_per_tick)
        {
            budget_exhausted = true;
            return foundation::Result<void>::Success();
        }
        ++staged_budget.target_evaluations;
        if (staged_diagnostics.target_evaluations != std::numeric_limits<std::uint64_t>::max())
            ++staged_diagnostics.target_evaluations;
        auto score = ScoreGoal(*profile, goal, context, target);
        if (!score)
            return foundation::Result<void>::Failure(score.GetError());
        const auto target_ref = target ? target->target : GameplayObjectRef{};
        const auto target_source = target ? target->source : AITargetSource::Perceived;
        bool better = !best_definition || score.Value() > best_score;
        if (!better && score.Value() == best_score)
        {
            if (goal.id < best_goal)
                better = true;
            else if (goal.id == best_goal)
            {
                if (target_ref < best_target)
                    better = true;
                else if (target_ref == best_target && target && target_source < best_source)
                    better = true;
            }
        }
        if (better)
        {
            best_score = score.Value();
            best_goal = goal.id;
            best_target = target_ref;
            best_source = target_source;
            best_definition = &goal;
        }
        return foundation::Result<void>::Success();
    };
    for (auto goal_id : profile->default_goals)
    {
        if (staged_budget.goals_evaluated >= budget_.max_goals_evaluated)
        {
            budget_exhausted = true;
            break;
        }
        auto *goal = FindGoal(goal_id);
        if (!goal)
            continue;
        if (agent->previous_goal == goal_id && profile->goal_repeat_cooldown.ticks > 0 &&
            context.now < SaturatingAdd(agent->previous_goal_terminal_at, profile->goal_repeat_cooldown))
            continue;
        ++staged_budget.goals_evaluated;
        if (staged_diagnostics.goals_evaluated != std::numeric_limits<std::uint64_t>::max())
            ++staged_diagnostics.goals_evaluated;
        if (goal->target_policy != AITargetPolicy::Required)
        {
            auto e = evaluate_pair(*goal, nullptr);
            if (!e)
                return foundation::Result<AIThinkResult>::Failure(e.GetError());
            if (budget_exhausted)
                break;
        }
        if (goal->target_policy != AITargetPolicy::Targetless)
        {
            for (const auto &candidate : context.targets)
            {
                auto e = evaluate_pair(*goal, &candidate);
                if (!e)
                    return foundation::Result<AIThinkResult>::Failure(e.GetError());
                if (budget_exhausted)
                    break;
            }
        }
        if (budget_exhausted)
            break;
    }
    if (budget_exhausted || (best_definition && staged_budget.intents_issued >= budget_.max_intents_issued))
    {
        if (!CanRecord())
            return foundation::Result<AIThinkResult>::Failure(
                Error("gameplay.ai.journal_exhausted", "ai journal exhausted"));
        if (staged_diagnostics.budget_exhaustions != std::numeric_limits<std::uint64_t>::max())
            ++staged_diagnostics.budget_exhaustions;
        tick_budget_ = staged_budget;
        diagnostics_ = staged_diagnostics;
        Record({0, AIChangeKind::BudgetExceeded, subject, best_goal, {}, {}, {}, gameplay_context, revision_});
        return deferred(AIThinkDeferReason::BudgetExceeded);
    }
    if (!CanBump() || !CanRecord(best_definition ? 4 : 2))
        return foundation::Result<AIThinkResult>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or change journal exhausted"));
    const Revision next_revision{revision_.value + 1};
    auto staged_intent_ids = intent_ids_;
    AIAgentState staged_agent;
    AIThinkResult result;
    result.subject = subject;
    result.revision = next_revision;
    AIIntentId intent_id{};
    DueKey next_due{};
    bool due_key_changed = false;
    try
    {
        staged_agent = *agent;
        if (best_definition)
        {
            intent_id = AIIntentId{staged_intent_ids.Next()};
            if (!intent_id.IsValid())
                return foundation::Result<AIThinkResult>::Failure(
                    Error("gameplay.ai.intent_id_exhausted", "intent id generator exhausted"));
            AIGoalInstance goal{best_goal, best_score, next_revision};
            AIIntent intent{intent_id,
                            subject,
                            best_definition->intent_type,
                            best_score,
                            AIIntentStatus::Issued,
                            best_target,
                            best_definition->payload,
                            gameplay_context,
                            next_revision};
            staged_agent.active_goal = goal;
            staged_agent.current_intent = intent;
            staged_agent.activity = AIAgentActivity::ExecutingIntent;
            staged_agent.pending_replan = false;
            staged_agent.pending_replan_reason = {};
            staged_agent.pending_replan_at = {};
            staged_agent.revision = next_revision;
            ++staged_budget.intents_issued;
            if (staged_diagnostics.intents_issued != std::numeric_limits<std::uint64_t>::max())
                ++staged_diagnostics.intents_issued;
            result.goal = goal;
            result.intent = intent;

            const auto [_, inserted] = intent_to_agent_.emplace(intent.id, subject);
            if (!inserted)
                return foundation::Result<AIThinkResult>::Failure(
                    Error("gameplay.ai.intent_id_collision", "generated intent id already exists"));
        }
        else
        {
            staged_agent.activity = AIAgentActivity::Idle;
            staged_agent.next_think_at = NextThinkAfter(*profile, context.now);
            staged_agent.revision = next_revision;
            next_due = DueKey{staged_agent.next_think_at, subject};
            const DueKey previous_due{agent->next_think_at, subject};
            due_key_changed = next_due.when != previous_due.when;
            if (due_key_changed)
            {
                const auto [_, inserted] = due_agents_.insert(next_due);
                if (!inserted)
                    return foundation::Result<AIThinkResult>::Failure(
                        Error("gameplay.ai.due_index_collision", "next think index entry already exists"));
            }
        }
    }
    catch (...)
    {
        return foundation::Result<AIThinkResult>::Failure(
            Error("gameplay.ai.allocation_failed", "failed to publish ai decision state"));
    }

    const DueKey previous_due{agent->next_think_at, subject};
    if (best_definition || due_key_changed)
        due_agents_.erase(previous_due);
    using std::swap;
    swap(*agent, staged_agent);
    if (best_definition)
        (void)intent_ids_.Restore(staged_intent_ids.GetSnapshot());
    revision_ = next_revision;
    tick_budget_ = staged_budget;
    diagnostics_ = staged_diagnostics;
    Record({0, AIChangeKind::ThinkStarted, subject, {}, {}, {}, {}, gameplay_context, revision_});
    if (best_definition)
    {
        Record({0, AIChangeKind::GoalSelected, subject, best_goal, {}, {}, {}, gameplay_context, revision_});
        Record({0,
                AIChangeKind::IntentIssued,
                subject,
                best_goal,
                intent_id,
                best_definition->intent_type,
                {},
                gameplay_context,
                revision_});
    }
    Record({0,
            AIChangeKind::ThinkCompleted,
            subject,
            best_goal,
            intent_id,
            best_definition ? best_definition->intent_type : AIIntentTypeId{},
            {},
            gameplay_context,
            revision_});
    return foundation::Result<AIThinkResult>::Success(std::move(result));
}

AIAgentState *AIService::FindAgentByIntent(AIIntentId id) noexcept
{
    auto it = intent_to_agent_.find(id);
    return it == intent_to_agent_.end() ? nullptr : FindMutableAgent(it->second);
}

foundation::Result<void> AIService::MarkIntentAccepted(AIIntentId id, GameplayContext context)
{
    auto *agent = FindAgentByIntent(id);
    if (!agent || !agent->current_intent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    if (agent->current_intent->status != AIIntentStatus::Issued)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_intent_transition", "intent must be issued"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    (void)Bump();
    agent->current_intent->status = AIIntentStatus::Accepted;
    agent->current_intent->revision = revision_;
    agent->revision = revision_;
    Record({0,
            AIChangeKind::IntentAccepted,
            agent->subject,
            agent->active_goal ? agent->active_goal->id : AIGoalId{},
            id,
            agent->current_intent->type,
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::MarkIntentRunning(AIIntentId id, GameplayContext context)
{
    auto *agent = FindAgentByIntent(id);
    if (!agent || !agent->current_intent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    if (agent->current_intent->status != AIIntentStatus::Accepted)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_intent_transition", "intent must be accepted"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    (void)Bump();
    agent->current_intent->status = AIIntentStatus::Running;
    agent->current_intent->revision = revision_;
    agent->revision = revision_;
    Record({0,
            AIChangeKind::IntentRunning,
            agent->subject,
            agent->active_goal ? agent->active_goal->id : AIGoalId{},
            id,
            agent->current_intent->type,
            {},
            context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::FinalizeIntent(AIIntentId id, AIIntentStatus final_status, TypeId reason,
                                                   GameplayContext context)
{
    auto *agent = FindAgentByIntent(id);
    if (!agent || !agent->current_intent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    const auto current_status = agent->current_intent->status;
    if (!IsLiveIntentStatus(current_status))
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_terminal", "intent is already terminal"));
    if (final_status == AIIntentStatus::Succeeded && current_status != AIIntentStatus::Running)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_intent_transition", "successful intent must be running"));
    if (final_status != AIIntentStatus::Succeeded && final_status != AIIntentStatus::Failed &&
        final_status != AIIntentStatus::Cancelled && final_status != AIIntentStatus::TimedOut)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_intent_transition", "invalid terminal state"));
    auto *profile = FindProfile(agent->profile);
    if (!profile)
        return foundation::Result<void>::Failure(Error("gameplay.ai.profile_missing", "profile missing"));
    if (!CanBump() || !CanRecord(2))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    const auto goal = agent->active_goal ? agent->active_goal->id : AIGoalId{};
    const auto type = agent->current_intent->type;
    GameplayTimePoint next = NextThinkAfter(*profile, context.time);
    if (agent->pending_replan)
        next = agent->pending_replan_at < context.time ? context.time : agent->pending_replan_at;
    auto staged_due = due_agents_;
    staged_due.insert(DueKey{next, agent->subject});
    (void)Bump();
    intent_to_agent_.erase(id);
    agent->current_intent.reset();
    agent->active_goal.reset();
    agent->activity = AIAgentActivity::Idle;
    agent->previous_goal = goal;
    agent->previous_goal_terminal_at = context.time;
    agent->pending_replan = false;
    agent->pending_replan_reason = {};
    agent->pending_replan_at = {};
    agent->next_think_at = next;
    agent->revision = revision_;
    due_agents_.swap(staged_due);
    AIChangeKind intent_kind = AIChangeKind::IntentFailed, goal_kind = AIChangeKind::GoalFailed;
    if (final_status == AIIntentStatus::Succeeded)
    {
        intent_kind = AIChangeKind::IntentSucceeded;
        goal_kind = AIChangeKind::GoalCompleted;
    }
    else if (final_status == AIIntentStatus::Cancelled)
        intent_kind = AIChangeKind::IntentCancelled;
    else if (final_status == AIIntentStatus::TimedOut)
        intent_kind = AIChangeKind::IntentTimedOut;
    else if (diagnostics_.intents_failed != std::numeric_limits<std::uint64_t>::max())
        ++diagnostics_.intents_failed;
    Record({0, intent_kind, agent->subject, goal, id, type, reason, context, revision_});
    Record({0, goal_kind, agent->subject, goal, id, type, reason, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> AIService::MarkIntentSucceeded(AIIntentId id, GameplayContext context)
{
    return FinalizeIntent(id, AIIntentStatus::Succeeded, {}, context);
}
foundation::Result<void> AIService::MarkIntentFailed(AIIntentId id, TypeId reason, GameplayContext context)
{
    return FinalizeIntent(id, AIIntentStatus::Failed, reason, context);
}
foundation::Result<void> AIService::CancelIntent(AIIntentId id, TypeId reason, GameplayContext context)
{
    return FinalizeIntent(id, AIIntentStatus::Cancelled, reason, context);
}
foundation::Result<void> AIService::TimeoutIntent(AIIntentId id, TypeId reason, GameplayContext context)
{
    return FinalizeIntent(id, AIIntentStatus::TimedOut, reason, context);
}

foundation::Result<void> AIService::SetBlackboard(GameplayObjectRef subject, BlackboardKeyId key, TypeId value_type,
                                                  std::vector<std::byte> value, GameplayTimePoint now,
                                                  GameplayContext context)
{
    if (auto frozen = RequireFrozen(); !frozen)
        return frozen;
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    auto *definition = FindBlackboardKey(key);
    if (!definition || definition->value_type != value_type || value.size() > definition->max_payload_bytes)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_blackboard_value", "blackboard value does not match key definition"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    const Revision next{revision_.value + 1};
    auto staged = agent->blackboard;
    auto it =
        std::lower_bound(staged.begin(), staged.end(), key,
                         [](const AIBlackboardEntry &entry, BlackboardKeyId wanted) { return entry.key < wanted; });
    AIBlackboardEntry entry{key, value_type, std::move(value), now, next};
    if (it != staged.end() && it->key == key)
        *it = std::move(entry);
    else
        staged.insert(it, std::move(entry));
    agent->blackboard.swap(staged);
    revision_ = next;
    agent->revision = revision_;
    Record({0, AIChangeKind::BlackboardChanged, subject, {}, {}, {}, key.value, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::RemoveBlackboard(GameplayObjectRef subject, BlackboardKeyId key,
                                                     GameplayContext context)
{
    auto *agent = FindMutableAgent(subject);
    if (!agent)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    auto it =
        std::lower_bound(agent->blackboard.begin(), agent->blackboard.end(), key,
                         [](const AIBlackboardEntry &entry, BlackboardKeyId wanted) { return entry.key < wanted; });
    if (it == agent->blackboard.end() || it->key != key)
        return foundation::Result<void>::Failure(Error("gameplay.ai.blackboard_missing", "blackboard entry missing"));
    if (!CanBump() || !CanRecord())
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.revision_exhausted", "ai revision or journal exhausted"));
    agent->blackboard.erase(it);
    (void)Bump();
    agent->revision = revision_;
    Record({0, AIChangeKind::BlackboardRemoved, subject, {}, {}, {}, key.value, context, revision_});
    return foundation::Result<void>::Success();
}

const AIBlackboardEntry *AIService::FindBlackboard(GameplayObjectRef subject, BlackboardKeyId key) const noexcept
{
    auto *agent = FindAgent(subject);
    if (!agent)
        return nullptr;
    auto it =
        std::lower_bound(agent->blackboard.begin(), agent->blackboard.end(), key,
                         [](const AIBlackboardEntry &entry, BlackboardKeyId wanted) { return entry.key < wanted; });
    return it != agent->blackboard.end() && it->key == key ? &*it : nullptr;
}

std::vector<AIAgentState> AIService::FindAgentsByActivity(AIAgentActivity activity) const
{
    std::vector<AIAgentState> out;
    for (const auto &[ref, agent] : agents_)
    {
        (void)ref;
        if (agent.activity == activity)
            out.push_back(agent);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.subject < b.subject; });
    return out;
}

std::vector<AIAgentState> AIService::FindAgentsWithGoal(AIGoalId goal) const
{
    std::vector<AIAgentState> out;
    for (const auto &[ref, agent] : agents_)
    {
        (void)ref;
        if (agent.active_goal && agent.active_goal->id == goal)
            out.push_back(agent);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.subject < b.subject; });
    return out;
}

std::vector<GameplayObjectRef> AIService::FindDueAgents(GameplayTimePoint now, std::size_t limit) const
{
    std::vector<GameplayObjectRef> out;
    out.reserve(std::min(limit, due_agents_.size()));
    for (auto it = due_agents_.begin(); it != due_agents_.end() && it->when <= now && out.size() < limit; ++it)
        out.push_back(it->subject);
    return out;
}

AIChangeBatch AIService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    AIChangeBatch batch;
    batch.latest_sequence =
        next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max() : next_change_sequence_ - 1;
    batch.oldest_available_sequence =
        changes_.empty()
            ? (next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max() : next_change_sequence_)
            : changes_.front().sequence;
    if (sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
    {
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    }
    return batch;
}

AISnapshot AIService::CaptureSnapshot() const
{
    AISnapshot snapshot;
    for (const auto &[ref, agent] : agents_)
    {
        (void)ref;
        auto copy = agent;
        copy.blackboard.erase(std::remove_if(copy.blackboard.begin(), copy.blackboard.end(),
                                             [&](const AIBlackboardEntry &entry) {
                                                 auto *definition = FindBlackboardKey(entry.key);
                                                 return !definition || definition->persistence !=
                                                                           BlackboardPersistencePolicy::Persistent;
                                             }),
                              copy.blackboard.end());
        snapshot.agents.push_back(std::move(copy));
    }
    std::sort(snapshot.agents.begin(), snapshot.agents.end(),
              [](const auto &a, const auto &b) { return a.subject < b.subject; });
    snapshot.intent_ids = intent_ids_.GetSnapshot();
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.revision = revision_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> AIService::RestoreSnapshot(AISnapshot snapshot)
{
    const auto next_journal_epoch =
        CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    if (auto frozen = RequireFrozen(); !frozen)
        return frozen;
    constexpr std::uint64_t kIntentScope = 0x2200;
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.intent_ids) ||
        snapshot.intent_ids.scope != kIntentScope)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.restore_invalid_generator", "invalid intent generator scope"));

    std::unordered_map<GameplayObjectRef, AIAgentState, RefHash> new_agents;
    std::unordered_map<AIIntentId, GameplayObjectRef, IdHash> new_intent_index;
    std::set<DueKey> new_due;
    std::uint64_t max_intent_low = 0;
    for (auto &agent : snapshot.agents)
    {
        if (!agent.subject.IsValid() || !profiles_.contains(agent.profile) ||
            agent.revision.value > snapshot.revision.value || new_agents.contains(agent.subject))
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.restore_invalid", "invalid or duplicate agent"));
        if (agent.activity == AIAgentActivity::Thinking)
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.restore_invalid", "thinking is transient state"));
        const bool executing = agent.activity == AIAgentActivity::ExecutingIntent;
        if (executing != static_cast<bool>(agent.current_intent) || executing != static_cast<bool>(agent.active_goal))
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.restore_invalid", "agent activity is inconsistent with intent/goal"));
        if (!executing && (agent.current_intent || agent.active_goal || agent.pending_replan))
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.restore_invalid", "inactive agent contains live decision state"));
        if (agent.pending_replan && !agent.pending_replan_reason.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.restore_invalid", "pending replan is missing a reason"));
        if (agent.active_goal &&
            (!goals_.contains(agent.active_goal->id) || agent.active_goal->revision.value > snapshot.revision.value))
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid active goal"));
        if (agent.previous_goal.IsValid() && !goals_.contains(agent.previous_goal))
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid previous goal"));
        if (agent.current_intent)
        {
            auto &intent = *agent.current_intent;
            if (!intent.id.IsValid() || intent.id.value.High() != kIntentScope || intent.actor != agent.subject ||
                !intent.type.IsValid() || !IsLiveIntentStatus(intent.status) ||
                intent.revision.value > snapshot.revision.value || new_intent_index.contains(intent.id))
                return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid live intent"));
            const auto *goal_definition = agent.active_goal ? FindGoal(agent.active_goal->id) : nullptr;
            if (!goal_definition || goal_definition->intent_type != intent.type ||
                (goal_definition->target_policy == AITargetPolicy::Required && !intent.target.IsValid()) ||
                (goal_definition->target_policy == AITargetPolicy::Targetless && intent.target.IsValid()))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.restore_invalid", "live intent does not match active goal definition"));
            max_intent_low = std::max(max_intent_low, intent.id.value.Low());
            new_intent_index.emplace(intent.id, agent.subject);
        }
        BlackboardKeyId previous_key{};
        for (const auto &entry : agent.blackboard)
        {
            auto *definition = FindBlackboardKey(entry.key);
            if (!definition || definition->persistence != BlackboardPersistencePolicy::Persistent ||
                definition->value_type != entry.value_type || entry.value.size() > definition->max_payload_bytes ||
                entry.revision.value > snapshot.revision.value)
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.restore_invalid", "invalid blackboard entry"));
            if (previous_key.IsValid() && !(previous_key < entry.key))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ai.restore_invalid", "blackboard entries are not canonical"));
            previous_key = entry.key;
        }
        if (agent.activity == AIAgentActivity::Idle)
            new_due.insert(DueKey{agent.next_think_at, agent.subject});
        new_agents.emplace(agent.subject, std::move(agent));
    }
    if (snapshot.intent_ids.next != 0 && snapshot.intent_ids.next <= max_intent_low)
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.restore_invalid_generator", "intent generator is behind restored intents"));

    agents_ = std::move(new_agents);
    intent_to_agent_ = std::move(new_intent_index);
    due_agents_ = std::move(new_due);
    (void)intent_ids_.Restore(snapshot.intent_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = snapshot.next_change_sequence;
    tick_budget_ = {};
    diagnostics_ = {};
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

AIDiagnostics AIService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.profiles = profiles_.size();
    diagnostics.agents = agents_.size();
    return diagnostics;
}

void AIService::EnsureBudgetEpoch(GameplayTickId tick) noexcept
{
    if (!tick.IsValid() || tick_budget_.tick != tick)
    {
        tick_budget_ = {};
        tick_budget_.tick = tick;
    }
}

void AIService::Record(AIChange change) noexcept
{
    change.sequence = next_change_sequence_;
    if (changes_.size() == change_journal_capacity_)
        changes_.erase(changes_.begin());
    changes_.push_back(std::move(change));
    next_change_sequence_ =
        next_change_sequence_ == std::numeric_limits<std::uint64_t>::max() ? 0 : next_change_sequence_ + 1;
}
} // namespace epidemic::gameplay::ai
