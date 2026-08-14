#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::ai
{
struct AIProfileId{TypeId value{}; static constexpr AIProfileId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIProfileId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIProfileId&) const noexcept=default;};
struct AIGoalId{TypeId value{}; static constexpr AIGoalId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIGoalId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIGoalId&) const noexcept=default;};
struct AIIntentTypeId{TypeId value{}; static constexpr AIIntentTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIIntentTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIIntentTypeId&) const noexcept=default;};
struct AIIntentId{GameplayObjectId value{}; static constexpr AIIntentId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr AIIntentId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIIntentId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIIntentId&) const noexcept=default;};
struct AIPlanId{GameplayObjectId value{}; static constexpr AIPlanId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIPlanId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIPlanId&) const noexcept=default;};
struct AIBehaviorId{TypeId value{}; static constexpr AIBehaviorId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIBehaviorId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIBehaviorId&) const noexcept=default;};
struct AIConsiderationId{TypeId value{}; static constexpr AIConsiderationId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIConsiderationId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIConsiderationId&) const noexcept=default;};
struct AIAccessPolicyId{TypeId value{}; static constexpr AIAccessPolicyId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AIAccessPolicyId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AIAccessPolicyId&) const noexcept=default;};
struct BlackboardKeyId{TypeId value{}; static constexpr BlackboardKeyId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const BlackboardKeyId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const BlackboardKeyId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};
struct RefHash{[[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept{return std::hash<GameplayObjectRef>{}(r);}};
using Fixed=std::int64_t;

enum class AIAgentActivity{Inactive,Idle,Thinking,ExecutingIntent,Suspended,Disabled};
enum class AIMaterializationPolicy{AbstractCapable,RequiresMaterialized,DisabledWhenAbstract};
enum class AIIntentStatus{Issued,Accepted,Running,Succeeded,Failed,Cancelled,TimedOut};
enum class AIChangeKind{AgentRegistered,AgentUnregistered,ThinkScheduled,ThinkStarted,ThinkCompleted,GoalSelected,GoalCompleted,GoalFailed,IntentIssued,IntentAccepted,IntentSucceeded,IntentFailed,IntentCancelled,BudgetExceeded};
enum class AIAccessFlag:std::uint32_t{SelfState=1u<<0,PerceivedState=1u<<1,KnownState=1u<<2,PublicFacts=1u<<3,GroupKnowledge=1u<<4,OmniscientDebug=1u<<31};
[[nodiscard]] constexpr std::uint32_t operator|(AIAccessFlag a,AIAccessFlag b) noexcept{return static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b);} 

struct AIConsideration{AIConsiderationId id{};TypeId type{};Fixed weight_micro=1'000'000;Fixed value_micro=0;std::vector<std::byte> payload;};
struct AIGoalDefinition{AIGoalId id{};std::string canonical_name;GameplayTagSet tags;Fixed base_priority_micro=0;AIIntentTypeId intent_type{};TypeId required_topic{};std::vector<AIConsideration> considerations;std::vector<std::byte> payload;};
struct AIAccessPolicy{AIAccessPolicyId id{};std::string canonical_name;std::uint32_t flags=static_cast<std::uint32_t>(AIAccessFlag::SelfState)|static_cast<std::uint32_t>(AIAccessFlag::PerceivedState)|static_cast<std::uint32_t>(AIAccessFlag::KnownState);};
struct AIProfile{AIProfileId id{};std::string canonical_name;GameplayTagSet tags;std::vector<AIGoalId> default_goals;AIAccessPolicyId access_policy{};AIMaterializationPolicy materialization_policy=AIMaterializationPolicy::AbstractCapable;std::vector<std::byte> payload;Revision revision{};};
struct AIBlackboardEntry{BlackboardKeyId key{};std::vector<std::byte> value;GameplayTimePoint updated_at{};bool persistent=false;Revision revision{};};
struct AIIntent{AIIntentId id{};GameplayObjectRef actor{};AIIntentTypeId type{};Fixed priority_micro=0;AIIntentStatus status=AIIntentStatus::Issued;GameplayObjectRef target{};std::vector<std::byte> payload;GameplayContext context{};Revision revision{};};
struct AIGoalInstance{AIGoalId id{};Fixed score_micro=0;Revision revision{};};
struct AIAgentState{GameplayObjectRef subject{};AIProfileId profile{};AIAgentActivity activity=AIAgentActivity::Idle;std::optional<AIGoalInstance> active_goal{};std::optional<AIIntent> current_intent{};GameplayTimePoint next_think_at{};std::vector<AIBlackboardEntry> blackboard;Revision revision{};};
struct AIContextSnapshot{std::vector<TypeId> known_topics;std::vector<GameplayObjectRef> perceived_targets;GameplayTimePoint now{};std::vector<std::byte> payload;};
struct AIThinkResult{GameplayObjectRef subject{};std::optional<AIGoalInstance> goal{};std::optional<AIIntent> intent{};bool deferred=false;Revision revision{};};
struct AIBudget{std::uint32_t max_agents_thinking_per_tick=256;std::uint32_t max_goals_evaluated=4096;std::uint32_t max_intents_issued=512;};
struct AIChange{std::uint64_t sequence=0;AIChangeKind kind=AIChangeKind::AgentRegistered;GameplayObjectRef subject{};AIGoalId goal{};AIIntentId intent{};AIIntentTypeId intent_type{};GameplayContext context{};Revision revision{};};
struct AISnapshot{std::vector<AIProfile> profiles;std::vector<AIGoalDefinition> goals;std::vector<AIAccessPolicy> policies;std::vector<AIAgentState> agents;MonotonicIdGenerator<GameplayObjectId>::Snapshot intent_ids{};Revision revision{};};
struct AIDiagnostics{std::uint64_t profiles=0,agents=0,agents_thinking=0,goals_evaluated=0,intents_issued=0,intents_failed=0,budget_exhaustions=0;};

class AIService
{
public:
    AIService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.ai");}
    [[nodiscard]] foundation::Result<void> RegisterAccessPolicy(AIAccessPolicy policy);
    [[nodiscard]] foundation::Result<void> RegisterGoal(AIGoalDefinition goal);
    [[nodiscard]] foundation::Result<void> RegisterProfile(AIProfile profile);
    void Freeze() noexcept{frozen_=true;}
    [[nodiscard]] bool IsFrozen() const noexcept{return frozen_;}
    [[nodiscard]] const AIAccessPolicy* FindAccessPolicy(AIAccessPolicyId id) const noexcept;
    [[nodiscard]] const AIGoalDefinition* FindGoal(AIGoalId id) const noexcept;
    [[nodiscard]] const AIProfile* FindProfile(AIProfileId id) const noexcept;

    [[nodiscard]] foundation::Result<void> RegisterAgent(GameplayObjectRef subject,AIProfileId profile,GameplayTimePoint next_think={});
    [[nodiscard]] foundation::Result<void> UnregisterAgent(GameplayObjectRef subject,GameplayContext context={});
    [[nodiscard]] const AIAgentState* FindAgent(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] foundation::Result<void> ScheduleThink(GameplayObjectRef subject,GameplayTimePoint when,GameplayContext context={});
    [[nodiscard]] foundation::Result<AIThinkResult> Think(GameplayObjectRef subject,const AIContextSnapshot& context,GameplayContext gameplay_context={});
    [[nodiscard]] foundation::Result<void> MarkIntentAccepted(AIIntentId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> MarkIntentSucceeded(AIIntentId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> MarkIntentFailed(AIIntentId id,GameplayContext context={});
    void SetBudget(AIBudget budget) noexcept{budget_=budget;}

    [[nodiscard]] std::vector<AIAgentState> FindAgentsByActivity(AIAgentActivity activity) const;
    [[nodiscard]] std::vector<AIAgentState> FindAgentsWithGoal(AIGoalId goal) const;
    [[nodiscard]] std::vector<AIChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] AISnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(AISnapshot snapshot);
    [[nodiscard]] AIDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(AIChange change);
    [[nodiscard]] AIAgentState* FindMutableAgent(GameplayObjectRef subject) noexcept;
    [[nodiscard]] AIAgentState* FindAgentByIntent(AIIntentId id) noexcept;
    [[nodiscard]] bool AccessAllows(const AIProfile& profile,AIAccessFlag flag) const noexcept;
    [[nodiscard]] bool TopicAvailable(TypeId topic,const AIContextSnapshot& context) const noexcept;
    [[nodiscard]] Fixed ScoreGoal(const AIGoalDefinition& goal,const AIContextSnapshot& context) const noexcept;

    bool frozen_=false;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> intent_ids_{0x2200};
    std::unordered_map<AIAccessPolicyId,AIAccessPolicy,IdHash> policies_;
    std::unordered_map<AIGoalId,AIGoalDefinition,IdHash> goals_;
    std::unordered_map<AIProfileId,AIProfile,IdHash> profiles_;
    std::unordered_map<GameplayObjectRef,AIAgentState,RefHash> agents_;
    std::vector<AIChange> changes_;
    std::uint64_t next_change_sequence_=1;
    AIBudget budget_{};
    AIDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::ai
