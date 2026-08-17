#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/Foundation/error.h"
#include <iterator>

namespace epidemic::gameplay::ai
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
AIService::AIService()
{
    auto basic = AIAccessPolicy{
        AIAccessPolicyId::FromString("framework.ai.access.default"), "framework.ai.access.default",
        static_cast<std::uint32_t>(AIAccessFlag::SelfState) | static_cast<std::uint32_t>(AIAccessFlag::PerceivedState) |
            static_cast<std::uint32_t>(AIAccessFlag::KnownState)};
    policies_.emplace(basic.id, basic);
}
foundation::Result<void> AIService::RegisterAccessPolicy(AIAccessPolicy p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!p.id.IsValid() || p.canonical_name.empty() || policies_.contains(p.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_policy", "invalid or duplicate access policy"));
    policies_.emplace(p.id, std::move(p));
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::RegisterGoal(AIGoalDefinition g)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!g.id.IsValid() || g.canonical_name.empty() || !g.intent_type.IsValid() || goals_.contains(g.id))
        return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_goal", "invalid or duplicate goal"));
    std::sort(g.considerations.begin(), g.considerations.end(), [](auto &a, auto &b) { return a.id < b.id; });
    goals_.emplace(g.id, std::move(g));
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::RegisterProfile(AIProfile p)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.ai.registry_frozen", "ai registry frozen"));
    if (!p.id.IsValid() || p.canonical_name.empty() || profiles_.contains(p.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.ai.invalid_profile", "invalid or duplicate ai profile"));
    if (!p.access_policy.IsValid())
        p.access_policy = AIAccessPolicyId::FromString("framework.ai.access.default");
    if (!policies_.contains(p.access_policy))
        return foundation::Result<void>::Failure(Error("gameplay.ai.unknown_policy", "unknown ai access policy"));
    std::sort(p.default_goals.begin(), p.default_goals.end());
    for (auto g : p.default_goals)
    {
        if (!goals_.contains(g))
            return foundation::Result<void>::Failure(
                Error("gameplay.ai.unknown_goal", "profile references unknown goal"));
    }
    Bump();
    p.revision = revision_;
    profiles_.emplace(p.id, std::move(p));
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
foundation::Result<void> AIService::RegisterAgent(GameplayObjectRef subject, AIProfileId profile,
                                                  GameplayTimePoint next)
{
    if (!subject.IsValid() || !profiles_.contains(profile) || agents_.contains(subject))
        return foundation::Result<void>::Failure(Error("gameplay.ai.invalid_agent", "invalid or duplicate ai agent"));
    Bump();
    agents_.emplace(subject, AIAgentState{subject, profile, AIAgentActivity::Idle, {}, {}, next, {}, revision_});
    Record({0, AIChangeKind::AgentRegistered, subject, {}, {}, {}, {}, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::UnregisterAgent(GameplayObjectRef subject, GameplayContext context)
{
    auto it = agents_.find(subject);
    if (it == agents_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    agents_.erase(it);
    Bump();
    Record({0, AIChangeKind::AgentUnregistered, subject, {}, {}, {}, context, revision_});
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
foundation::Result<void> AIService::ScheduleThink(GameplayObjectRef s, GameplayTimePoint when, GameplayContext c)
{
    auto *a = FindMutableAgent(s);
    if (!a)
        return foundation::Result<void>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    Bump();
    a->next_think_at = when;
    a->revision = revision_;
    Record({0, AIChangeKind::ThinkScheduled, s, {}, {}, {}, c, revision_});
    return foundation::Result<void>::Success();
}
bool AIService::AccessAllows(const AIProfile &p, AIAccessFlag flag) const noexcept
{
    auto *policy = FindAccessPolicy(p.access_policy);
    return policy && (policy->flags & static_cast<std::uint32_t>(flag)) != 0;
}
bool AIService::TopicAvailable(TypeId topic, const AIContextSnapshot &c) const noexcept
{
    if (!topic.IsValid())
        return true;
    return std::binary_search(c.known_topics.begin(), c.known_topics.end(), topic);
}
Fixed AIService::ScoreGoal(const AIGoalDefinition &g, const AIContextSnapshot &c) const noexcept
{
    Fixed score = g.base_priority_micro;
    if (!TopicAvailable(g.required_topic, c))
        return -1;
    for (const auto &con : g.considerations)
    {
        score += (con.value_micro * con.weight_micro) / 1'000'000;
    }
    return score;
}
foundation::Result<AIThinkResult> AIService::Think(GameplayObjectRef s, const AIContextSnapshot &ctx,
                                                   GameplayContext gc)
{
    EnsureBudgetEpoch(gc.tick);
    auto *agent = FindMutableAgent(s);
    if (!agent)
        return foundation::Result<AIThinkResult>::Failure(Error("gameplay.ai.agent_missing", "agent missing"));
    auto *profile = FindProfile(agent->profile);
    if (!profile)
        return foundation::Result<AIThinkResult>::Failure(Error("gameplay.ai.profile_missing", "profile missing"));
    if (tick_budget_.agents_thought >= budget_.max_agents_thinking_per_tick)
    {
        ++diagnostics_.budget_exhaustions;
        Record({0, AIChangeKind::BudgetExceeded, s, {}, {}, {}, gc, revision_});
        return foundation::Result<AIThinkResult>::Success(AIThinkResult{s, {}, {}, true, revision_});
    }
    ++tick_budget_.agents_thought;
    ++diagnostics_.agents_thinking;
    Bump();
    agent->activity = AIAgentActivity::Thinking;
    agent->revision = revision_;
    Record({0, AIChangeKind::ThinkStarted, s, {}, {}, {}, gc, revision_});
    std::vector<TypeId> sorted_topics = ctx.known_topics;
    std::sort(sorted_topics.begin(), sorted_topics.end());
    AIContextSnapshot sorted_ctx = ctx;
    sorted_ctx.known_topics = std::move(sorted_topics);
    Fixed best = -1;
    AIGoalId best_goal{};
    const AIGoalDefinition *best_def = nullptr;
    std::uint32_t evaluated = 0;
    for (auto gid : profile->default_goals)
    {
        if (tick_budget_.goals_evaluated >= budget_.max_goals_evaluated)
        {
            ++diagnostics_.budget_exhaustions;
            break;
        }
        auto *g = FindGoal(gid);
        if (!g)
            continue;
        ++evaluated;
        ++tick_budget_.goals_evaluated;
        ++diagnostics_.goals_evaluated;
        if (g->required_topic.IsValid() && !AccessAllows(*profile, AIAccessFlag::KnownState))
            continue;
        auto score = ScoreGoal(*g, sorted_ctx);
        if (score > best || (score == best && best_goal.IsValid() && g->id < best_goal))
        {
            best = score;
            best_goal = g->id;
            best_def = g;
        }
    }
    AIThinkResult result{s, {}, {}, false, revision_};
    if (best_def && best >= 0 && tick_budget_.intents_issued < budget_.max_intents_issued)
    {
        Bump();
        AIGoalInstance goal{best_goal, best, revision_};
        AIIntent intent{AIIntentId{intent_ids_.Next()},
                        s,
                        best_def->intent_type,
                        best,
                        AIIntentStatus::Issued,
                        ctx.perceived_targets.empty() ? GameplayObjectRef{} : ctx.perceived_targets.front(),
                        best_def->payload,
                        gc,
                        revision_};
        agent->active_goal = goal;
        agent->current_intent = intent;
        agent->activity = AIAgentActivity::ExecutingIntent;
        agent->revision = revision_;
        ++tick_budget_.intents_issued;
        ++diagnostics_.intents_issued;
        Record({0, AIChangeKind::GoalSelected, s, best_goal, {}, {}, gc, revision_});
        Record({0, AIChangeKind::IntentIssued, s, best_goal, intent.id, intent.type, gc, revision_});
        result.goal = goal;
        result.intent = intent;
    }
    else
    {
        agent->activity = AIAgentActivity::Idle;
        agent->revision = revision_;
    }
    Record({0, AIChangeKind::ThinkCompleted, s, best_goal, result.intent ? result.intent->id : AIIntentId{},
            result.intent ? result.intent->type : AIIntentTypeId{}, gc, revision_});
    result.revision = revision_;
    return foundation::Result<AIThinkResult>::Success(result);
}
AIAgentState *AIService::FindAgentByIntent(AIIntentId id) noexcept
{
    for (auto &[ref, a] : agents_)
    {
        (void)ref;
        if (a.current_intent && a.current_intent->id == id)
            return &a;
    }
    return nullptr;
}
foundation::Result<void> AIService::MarkIntentAccepted(AIIntentId id, GameplayContext c)
{
    auto *a = FindAgentByIntent(id);
    if (!a)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    Bump();
    a->current_intent->status = AIIntentStatus::Accepted;
    a->current_intent->revision = revision_;
    a->revision = revision_;
    Record({0, AIChangeKind::IntentAccepted, a->subject, a->active_goal ? a->active_goal->id : AIGoalId{}, id,
            a->current_intent->type, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::MarkIntentSucceeded(AIIntentId id, GameplayContext c)
{
    auto *a = FindAgentByIntent(id);
    if (!a)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    Bump();
    auto goal = a->active_goal ? a->active_goal->id : AIGoalId{};
    auto type = a->current_intent ? a->current_intent->type : AIIntentTypeId{};
    a->current_intent.reset();
    a->active_goal.reset();
    a->activity = AIAgentActivity::Idle;
    a->revision = revision_;
    Record({0, AIChangeKind::IntentSucceeded, a->subject, goal, id, type, c, revision_});
    Record({0, AIChangeKind::GoalCompleted, a->subject, goal, id, type, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> AIService::MarkIntentFailed(AIIntentId id, GameplayContext c)
{
    auto *a = FindAgentByIntent(id);
    if (!a)
        return foundation::Result<void>::Failure(Error("gameplay.ai.intent_missing", "intent missing"));
    Bump();
    auto goal = a->active_goal ? a->active_goal->id : AIGoalId{};
    auto type = a->current_intent ? a->current_intent->type : AIIntentTypeId{};
    a->current_intent->status = AIIntentStatus::Failed;
    a->activity = AIAgentActivity::Idle;
    a->revision = revision_;
    ++diagnostics_.intents_failed;
    Record({0, AIChangeKind::IntentFailed, a->subject, goal, id, type, c, revision_});
    Record({0, AIChangeKind::GoalFailed, a->subject, goal, id, type, c, revision_});
    return foundation::Result<void>::Success();
}
std::vector<AIAgentState> AIService::FindAgentsByActivity(AIAgentActivity act) const
{
    std::vector<AIAgentState> out;
    for (const auto &[r, a] : agents_)
    {
        (void)r;
        if (a.activity == act)
            out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    return out;
}
std::vector<AIAgentState> AIService::FindAgentsWithGoal(AIGoalId goal) const
{
    std::vector<AIAgentState> out;
    for (const auto &[r, a] : agents_)
    {
        (void)r;
        if (a.active_goal && a.active_goal->id == goal)
            out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    return out;
}
std::vector<AIChange> AIService::ChangesSince(std::uint64_t seq) const
{
    std::vector<AIChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
AISnapshot AIService::CaptureSnapshot() const
{
    AISnapshot s;
    for (const auto &[id, p] : profiles_)
    {
        (void)id;
        s.profiles.push_back(p);
    }
    for (const auto &[id, g] : goals_)
    {
        (void)id;
        s.goals.push_back(g);
    }
    for (const auto &[id, p] : policies_)
    {
        (void)id;
        s.policies.push_back(p);
    }
    for (const auto &[r, a] : agents_)
    {
        (void)r;
        s.agents.push_back(a);
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.goals.begin(), s.goals.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.policies.begin(), s.policies.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.agents.begin(), s.agents.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    s.intent_ids = intent_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> AIService::RestoreSnapshot(AISnapshot s)
{
    profiles_.clear();
    goals_.clear();
    policies_.clear();
    agents_.clear();
    for (auto &p : s.policies)
    {
        if (!p.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid policy"));
        policies_[p.id] = p;
    }
    for (auto &g : s.goals)
    {
        if (!g.id.IsValid() || !g.intent_type.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid goal"));
        goals_[g.id] = g;
    }
    for (auto &p : s.profiles)
    {
        if (!p.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid profile"));
        profiles_[p.id] = p;
    }
    for (auto &a : s.agents)
    {
        if (!a.subject.IsValid() || !profiles_.contains(a.profile))
            return foundation::Result<void>::Failure(Error("gameplay.ai.restore_invalid", "invalid agent"));
        agents_[a.subject] = a;
    }
    intent_ids_.Restore(s.intent_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
AIDiagnostics AIService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.profiles = profiles_.size();
    d.agents = agents_.size();
    return d;
}
void AIService::EnsureBudgetEpoch(GameplayTickId tick) noexcept
{
    if (tick_budget_.tick != tick)
    {
        tick_budget_ = {};
        tick_budget_.tick = tick;
    }
}
void AIService::Record(AIChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::ai
