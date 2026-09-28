#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::ai
{
struct AIProfileId
{
    TypeId value{};
    static constexpr AIProfileId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIProfileId &) const noexcept = default;
};
struct AIGoalId
{
    TypeId value{};
    static constexpr AIGoalId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIGoalId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIGoalId &) const noexcept = default;
};
struct AIIntentTypeId
{
    TypeId value{};
    static constexpr AIIntentTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIIntentTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIIntentTypeId &) const noexcept = default;
};
struct AIIntentId
{
    GameplayObjectId value{};
    static constexpr AIIntentId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; }
    static constexpr AIIntentId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIIntentId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIIntentId &) const noexcept = default;
};
struct AIConsiderationId
{
    TypeId value{};
    static constexpr AIConsiderationId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIConsiderationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIConsiderationId &) const noexcept = default;
};
struct AIConsiderationEvaluatorId
{
    TypeId value{};
    static constexpr AIConsiderationEvaluatorId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIConsiderationEvaluatorId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIConsiderationEvaluatorId &) const noexcept = default;
};
struct AIInputKeyId
{
    TypeId value{};
    static constexpr AIInputKeyId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    static constexpr AIInputKeyId FromType(TypeId value) noexcept { return {value}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIInputKeyId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIInputKeyId &) const noexcept = default;
};
struct AIAccessPolicyId
{
    TypeId value{};
    static constexpr AIAccessPolicyId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIAccessPolicyId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIAccessPolicyId &) const noexcept = default;
};
struct BlackboardKeyId
{
    TypeId value{};
    static constexpr BlackboardKeyId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const BlackboardKeyId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const BlackboardKeyId &) const noexcept = default;
};
struct AIReplanReasonId
{
    TypeId value{};
    static constexpr AIReplanReasonId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const AIReplanReasonId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const AIReplanReasonId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};
struct RefHash
{
    [[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept
    {
        return std::hash<GameplayObjectRef>{}(r);
    }
};
using Fixed = std::int64_t;
inline constexpr Fixed kFixedOne = 1'000'000;

enum class AIAgentActivity
{
    Idle,
    Thinking,
    ExecutingIntent,
    Suspended,
    Disabled
};
enum class AIMaterializationPolicy
{
    AbstractCapable,
    RequiresMaterialized,
    RequiresRuntimeProjection
};
enum class AIIntentStatus
{
    Issued,
    Accepted,
    Running,
    Succeeded,
    Failed,
    Cancelled,
    TimedOut
};
enum class AITargetPolicy
{
    Targetless,
    Optional,
    Required
};
enum class AIConsiderationScope
{
    Context,
    Target
};
enum class AITargetSource
{
    Perceived = 0,
    Known = 1,
    Public = 2,
    Group = 3,
    LastKnown = 4,
    CurrentPerception = Perceived,
    Knowledge = Known
};
enum class AIInputValueKind
{
    Fixed,
    Boolean,
    Type,
    Object,
    Payload
};
enum class BlackboardPersistencePolicy
{
    Session,
    Persistent
};
enum class AIThinkDeferReason
{
    None,
    NotDue,
    Suspended,
    Disabled,
    MaterializationUnavailable,
    ActiveIntent,
    BudgetExceeded
};
enum class AIChangeKind
{
    AgentRegistered,
    AgentUnregistered,
    AgentEnabled,
    AgentDisabled,
    AgentSuspended,
    AgentResumed,
    ThinkScheduled,
    ThinkStarted,
    ThinkCompleted,
    GoalSelected,
    GoalCompleted,
    GoalFailed,
    IntentIssued,
    IntentAccepted,
    IntentRunning,
    IntentSucceeded,
    IntentFailed,
    IntentCancelled,
    IntentTimedOut,
    ReplanRequested,
    BlackboardChanged,
    BlackboardRemoved,
    BudgetExceeded
};
enum class AIAccessFlag : std::uint32_t
{
    SelfState = 1u << 0,
    PerceivedState = 1u << 1,
    KnownState = 1u << 2,
    PublicFacts = 1u << 3,
    GroupKnowledge = 1u << 4,
    OmniscientDebug = 1u << 31
};
[[nodiscard]] constexpr std::uint32_t operator|(AIAccessFlag a, AIAccessFlag b) noexcept
{
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}

struct AIInputValue
{
    AIInputKeyId key{};
    AIInputValueKind kind = AIInputValueKind::Fixed;
    AIAccessFlag access = AIAccessFlag::SelfState;
    Fixed fixed_value = 0;
    bool bool_value = false;
    TypeId type_value{};
    GameplayObjectRef object_value{};
    std::vector<std::byte> payload;
};
struct AITargetCandidate
{
    GameplayObjectRef target{};
    AITargetSource source = AITargetSource::Perceived;
    std::vector<AIInputValue> inputs;
};
struct AIConsideration
{
    AIConsiderationId id{};
    AIConsiderationEvaluatorId evaluator{};
    AIInputKeyId input_key{};
    AIConsiderationScope scope = AIConsiderationScope::Context;
    Fixed weight_micro = kFixedOne;
    std::vector<std::byte> payload;
};
struct AIGoalDefinition
{
    AIGoalId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    Fixed base_priority_micro = 0;
    AIIntentTypeId intent_type{};
    AITargetPolicy target_policy = AITargetPolicy::Targetless;
    std::vector<AIConsideration> considerations;
    std::vector<std::byte> payload;
};
struct AIAccessPolicy
{
    AIAccessPolicyId id{};
    std::string canonical_name;
    std::uint32_t flags = static_cast<std::uint32_t>(AIAccessFlag::SelfState) |
                          static_cast<std::uint32_t>(AIAccessFlag::PerceivedState) |
                          static_cast<std::uint32_t>(AIAccessFlag::KnownState);
};
struct AIProfile
{
    AIProfileId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    std::vector<AIGoalId> default_goals;
    AIAccessPolicyId access_policy{};
    AIMaterializationPolicy materialization_policy = AIMaterializationPolicy::AbstractCapable;
    GameplayDuration think_interval{1};
    GameplayDuration goal_repeat_cooldown{};
    bool debug_profile = false;
    std::vector<std::byte> payload;
    Revision revision{};
};
struct BlackboardKeyDefinition
{
    BlackboardKeyId key{};
    std::string canonical_name;
    TypeId value_type{};
    std::size_t max_payload_bytes = 4096;
    BlackboardPersistencePolicy persistence = BlackboardPersistencePolicy::Session;
};
struct AIBlackboardEntry
{
    BlackboardKeyId key{};
    TypeId value_type{};
    std::vector<std::byte> value;
    GameplayTimePoint updated_at{};
    Revision revision{};
};
struct AIIntent
{
    AIIntentId id{};
    GameplayObjectRef actor{};
    AIIntentTypeId type{};
    Fixed priority_micro = 0;
    AIIntentStatus status = AIIntentStatus::Issued;
    GameplayObjectRef target{};
    std::vector<std::byte> payload;
    GameplayContext context{};
    Revision revision{};
};
struct AIGoalInstance
{
    AIGoalId id{};
    Fixed score_micro = 0;
    Revision revision{};
};
struct AIAgentState
{
    GameplayObjectRef subject{};
    AIProfileId profile{};
    AIAgentActivity activity = AIAgentActivity::Idle;
    std::optional<AIGoalInstance> active_goal{};
    std::optional<AIIntent> current_intent{};
    GameplayTimePoint next_think_at{};
    std::vector<AIBlackboardEntry> blackboard;
    bool pending_replan = false;
    AIReplanReasonId pending_replan_reason{};
    GameplayTimePoint pending_replan_at{};
    AIGoalId previous_goal{};
    GameplayTimePoint previous_goal_terminal_at{};
    Revision revision{};
};
struct AIContextSnapshot
{
    std::vector<AIInputValue> inputs;
    std::vector<AITargetCandidate> targets;
    GameplayTimePoint now{};
    bool materialized = false;
    bool runtime_projection_available = false;
};
struct AIThinkResult
{
    GameplayObjectRef subject{};
    std::optional<AIGoalInstance> goal{};
    std::optional<AIIntent> intent{};
    bool deferred = false;
    AIThinkDeferReason defer_reason = AIThinkDeferReason::None;
    Revision revision{};
};
struct AIBudget
{
    std::uint32_t max_agents_thinking_per_tick = 256;
    std::uint32_t max_goals_evaluated = 4096;
    std::uint32_t max_target_evaluations_per_tick = 8192;
    std::uint32_t max_intents_issued = 512;
};
struct AITickBudgetState
{
    GameplayTickId tick{};
    std::uint32_t agents_thought = 0;
    std::uint32_t goals_evaluated = 0;
    std::uint32_t target_evaluations = 0;
    std::uint32_t intents_issued = 0;
};
struct AIChange
{
    std::uint64_t sequence = 0;
    AIChangeKind kind = AIChangeKind::AgentRegistered;
    GameplayObjectRef subject{};
    AIGoalId goal{};
    AIIntentId intent{};
    AIIntentTypeId intent_type{};
    TypeId reason{};
    GameplayContext context{};
    Revision revision{};
};
struct AIChangeBatch
{
    std::vector<AIChange> changes;
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};
struct AISnapshot
{
    std::vector<AIAgentState> agents;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot intent_ids{};
    std::uint64_t next_change_sequence = 1;
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct AIDiagnostics
{
    std::uint64_t profiles = 0;
    std::uint64_t agents = 0;
    std::uint64_t agents_thinking = 0;
    std::uint64_t goals_evaluated = 0;
    std::uint64_t target_evaluations = 0;
    std::uint64_t intents_issued = 0;
    std::uint64_t intents_failed = 0;
    std::uint64_t replans_requested = 0;
    std::uint64_t evaluator_failures = 0;
    std::uint64_t budget_exhaustions = 0;
};

class IAIConsiderationEvaluator
{
  public:
    virtual ~IAIConsiderationEvaluator() = default;
    [[nodiscard]] virtual foundation::Result<Fixed> Evaluate(const AIConsideration &definition,
                                                              const AIContextSnapshot &context,
                                                              const AITargetCandidate *target) const = 0;
};

class AIService
{
  public:
    AIService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.ai");
    }
    [[nodiscard]] static constexpr AIConsiderationEvaluatorId InputEvaluatorId() noexcept
    {
        return AIConsiderationEvaluatorId::FromString("framework.ai.evaluator.input");
    }

    [[nodiscard]] foundation::Result<void> RegisterAccessPolicy(AIAccessPolicy policy);
    [[nodiscard]] foundation::Result<void> RegisterConsiderationEvaluator(AIConsiderationEvaluatorId id,
                                                                          const IAIConsiderationEvaluator &evaluator);
    [[nodiscard]] foundation::Result<void> RegisterGoal(AIGoalDefinition goal);
    [[nodiscard]] foundation::Result<void> RegisterProfile(AIProfile profile);
    [[nodiscard]] foundation::Result<void> RegisterBlackboardKey(BlackboardKeyDefinition definition);
    [[nodiscard]] foundation::Result<void> Freeze();
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] const AIAccessPolicy *FindAccessPolicy(AIAccessPolicyId id) const noexcept;
    [[nodiscard]] const AIGoalDefinition *FindGoal(AIGoalId id) const noexcept;
    [[nodiscard]] const AIProfile *FindProfile(AIProfileId id) const noexcept;
    [[nodiscard]] const BlackboardKeyDefinition *FindBlackboardKey(BlackboardKeyId id) const noexcept;

    [[nodiscard]] foundation::Result<void> RegisterAgent(GameplayObjectRef subject, AIProfileId profile,
                                                         GameplayTimePoint next_think = {});
    [[nodiscard]] foundation::Result<void> UnregisterAgent(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] const AIAgentState *FindAgent(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<void> DisableAgent(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> EnableAgent(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SuspendAgent(GameplayObjectRef subject, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ResumeAgent(GameplayObjectRef subject, GameplayTimePoint when,
                                                       GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ScheduleThink(GameplayObjectRef subject, GameplayTimePoint when,
                                                         GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RequestReplan(GameplayObjectRef subject, AIReplanReasonId reason,
                                                         GameplayTimePoint when, GameplayContext context = {});
    [[nodiscard]] foundation::Result<AIThinkResult> Think(GameplayObjectRef subject, const AIContextSnapshot &context,
                                                          GameplayContext gameplay_context = {});

    [[nodiscard]] foundation::Result<void> MarkIntentAccepted(AIIntentId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MarkIntentRunning(AIIntentId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MarkIntentSucceeded(AIIntentId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MarkIntentFailed(AIIntentId id, TypeId reason = {},
                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CancelIntent(AIIntentId id, TypeId reason = {},
                                                        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> TimeoutIntent(AIIntentId id, TypeId reason = {},
                                                         GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> SetBlackboard(GameplayObjectRef subject, BlackboardKeyId key,
                                                         TypeId value_type, std::vector<std::byte> value,
                                                         GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveBlackboard(GameplayObjectRef subject, BlackboardKeyId key,
                                                            GameplayContext context = {});
    [[nodiscard]] const AIBlackboardEntry *FindBlackboard(GameplayObjectRef subject, BlackboardKeyId key) const noexcept;

    void SetBudget(AIBudget budget) noexcept { budget_ = budget; }
    void SetChangeJournalCapacity(std::size_t capacity) noexcept
    {
        change_journal_capacity_ = std::clamp<std::size_t>(capacity == 0 ? 1 : capacity, 1, kMaxChangeJournalCapacity);
        if (changes_.size() > change_journal_capacity_)
            changes_.erase(changes_.begin(), changes_.begin() + static_cast<std::ptrdiff_t>(changes_.size() - change_journal_capacity_));
    }

    [[nodiscard]] std::vector<AIAgentState> FindAgentsByActivity(AIAgentActivity activity) const;
    [[nodiscard]] std::vector<AIAgentState> FindAgentsWithGoal(AIGoalId goal) const;
    [[nodiscard]] std::vector<GameplayObjectRef> FindDueAgents(GameplayTimePoint now, std::size_t limit) const;
    private:
        [[nodiscard]] AIChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] AIChangeBatch ReadChangesSince(ChangeCursor cursor) const
    {
        auto batch = ReadChangesSinceSequence(cursor.sequence);
        batch.oldest_available_cursor = {journal_epoch_, batch.oldest_available_sequence};
        batch.latest_cursor = {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                    : next_change_sequence_ - 1};
        if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
        {
            batch.changes.clear();
            batch.snapshot_required = true;
        }
        return batch;
    }
    [[nodiscard]] ChangeCursor LatestChangeCursor() const noexcept
    {
        return {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                          : next_change_sequence_ - 1};
    }
    [[nodiscard]] AISnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(AISnapshot snapshot);
    [[nodiscard]] AIDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    struct DueKey
    {
        GameplayTimePoint when{};
        GameplayObjectRef subject{};
        [[nodiscard]] bool operator<(const DueKey &other) const noexcept
        {
            if (when != other.when)
                return when < other.when;
            return subject < other.subject;
        }
    };

    [[nodiscard]] foundation::Result<void> RequireFrozen() const;
    [[nodiscard]] bool CanBump() const noexcept;
    [[nodiscard]] bool Bump() noexcept;
    [[nodiscard]] bool CanRecord(std::size_t count = 1) const noexcept;
    void Record(AIChange change) noexcept;
    void EnsureBudgetEpoch(GameplayTickId tick) noexcept;
    [[nodiscard]] AIAgentState *FindMutableAgent(GameplayObjectRef subject) noexcept;
    [[nodiscard]] AIAgentState *FindAgentByIntent(AIIntentId id) noexcept;
    [[nodiscard]] bool AccessAllows(const AIProfile &profile, AIAccessFlag flag) const noexcept;
    [[nodiscard]] bool TargetSourceAllowed(const AIProfile &profile, AITargetSource source) const noexcept;
    [[nodiscard]] foundation::Result<Fixed> ScoreGoal(const AIProfile &profile, const AIGoalDefinition &goal,
                                                      const AIContextSnapshot &context,
                                                      const AITargetCandidate *target);
    [[nodiscard]] foundation::Result<void> FinalizeIntent(AIIntentId id, AIIntentStatus final_status, TypeId reason,
                                                         GameplayContext context);
    void RemoveDueIndex(const AIAgentState &agent) noexcept;
    void AddDueIndex(const AIAgentState &agent);
    [[nodiscard]] foundation::Result<void> SetNextThink(AIAgentState &agent, GameplayTimePoint when);
    [[nodiscard]] GameplayTimePoint NextThinkAfter(const AIProfile &profile, GameplayTimePoint now) const noexcept;

    bool frozen_ = false;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> intent_ids_{0x2200};
    std::unordered_map<AIAccessPolicyId, AIAccessPolicy, IdHash> policies_;
    std::unordered_map<AIConsiderationEvaluatorId, const IAIConsiderationEvaluator *, IdHash> evaluators_;
    std::unordered_map<AIGoalId, AIGoalDefinition, IdHash> goals_;
    std::unordered_map<AIProfileId, AIProfile, IdHash> profiles_;
    std::unordered_map<BlackboardKeyId, BlackboardKeyDefinition, IdHash> blackboard_keys_;
    std::unordered_map<GameplayObjectRef, AIAgentState, RefHash> agents_;
    std::unordered_map<AIIntentId, GameplayObjectRef, IdHash> intent_to_agent_;
    std::set<DueKey> due_agents_;
    static constexpr std::size_t kMaxChangeJournalCapacity = 16384;
    std::vector<AIChange> changes_;
    std::size_t change_journal_capacity_ = 4096;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    AIBudget budget_{};
    AITickBudgetState tick_budget_{};
    AIDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::ai
