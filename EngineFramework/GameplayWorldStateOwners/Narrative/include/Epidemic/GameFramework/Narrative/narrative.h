#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace epidemic::gameplay::narrative
{
struct NarrativeThreadId
{
    GameplayObjectId value{};
    static constexpr NarrativeThreadId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeThreadId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeThreadId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeThreadId &) const noexcept = default;
};
struct NarrativeArcId
{
    GameplayObjectId value{};
    static constexpr NarrativeArcId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeArcId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeArcId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeArcId &) const noexcept = default;
};
struct NarrativeBeatId
{
    GameplayObjectId value{};
    static constexpr NarrativeBeatId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeBeatId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeBeatId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeBeatId &) const noexcept = default;
};
struct NarrativeObjectiveId
{
    GameplayObjectId value{};
    static constexpr NarrativeObjectiveId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeObjectiveId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeObjectiveId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeObjectiveId &) const noexcept = default;
};
struct NarrativeConditionId
{
    GameplayObjectId value{};
    static constexpr NarrativeConditionId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeConditionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConditionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConditionId &) const noexcept = default;
};
struct NarrativeConsequenceId
{
    GameplayObjectId value{};
    static constexpr NarrativeConsequenceId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeConsequenceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConsequenceId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConsequenceId &) const noexcept = default;
};
struct NarrativeConsequenceExecutionId
{
    GameplayObjectId value{};
    static constexpr NarrativeConsequenceExecutionId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeConsequenceExecutionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConsequenceExecutionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConsequenceExecutionId &) const noexcept = default;
};
struct JournalEntryId
{
    GameplayObjectId value{};
    static constexpr JournalEntryId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr JournalEntryId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const JournalEntryId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const JournalEntryId &) const noexcept = default;
};
struct ClueId
{
    GameplayObjectId value{};
    static constexpr ClueId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr ClueId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ClueId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ClueId &) const noexcept = default;
};
struct RumorId
{
    GameplayObjectId value{};
    static constexpr RumorId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr RumorId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const RumorId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RumorId &) const noexcept = default;
};
struct StoryletId
{
    GameplayObjectId value{};
    static constexpr StoryletId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr StoryletId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const StoryletId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StoryletId &) const noexcept = default;
};
struct NarrativeGateId
{
    GameplayObjectId value{};
    static constexpr NarrativeGateId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeGateId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeGateId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeGateId &) const noexcept = default;
};
struct NarrativeFlagId
{
    GameplayObjectId value{};
    static constexpr NarrativeFlagId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeFlagId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeFlagId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeFlagId &) const noexcept = default;
};
struct NarrativeVariableId
{
    GameplayObjectId value{};
    static constexpr NarrativeVariableId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeVariableId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeVariableId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeVariableId &) const noexcept = default;
};
struct NarrativeSceneId
{
    GameplayObjectId value{};
    static constexpr NarrativeSceneId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeSceneId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeSceneId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeSceneId &) const noexcept = default;
};
struct NarrativeChoiceId
{
    GameplayObjectId value{};
    static constexpr NarrativeChoiceId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeChoiceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeChoiceId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeChoiceId &) const noexcept = default;
};
struct NarrativeChoiceOptionId
{
    GameplayObjectId value{};
    static constexpr NarrativeChoiceOptionId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr NarrativeChoiceOptionId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeChoiceOptionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeChoiceOptionId &) const noexcept = default;
};
struct NarrativeThreadTypeId
{
    TypeId value{};
    static constexpr NarrativeThreadTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeThreadTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeThreadTypeId &) const noexcept = default;
};
struct NarrativeEventTypeId
{
    TypeId value{};
    static constexpr NarrativeEventTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeEventTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeEventTypeId &) const noexcept = default;
};
struct NarrativeConditionTypeId
{
    TypeId value{};
    static constexpr NarrativeConditionTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConditionTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConditionTypeId &) const noexcept = default;
};
struct NarrativeConsequenceTypeId
{
    TypeId value{};
    static constexpr NarrativeConsequenceTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConsequenceTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConsequenceTypeId &) const noexcept = default;
};
struct JournalEntryTypeId
{
    TypeId value{};
    static constexpr JournalEntryTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const JournalEntryTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const JournalEntryTypeId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class NarrativeRuntimeState
{
    Hidden,
    Available,
    Discovered,
    Active,
    Suspended,
    Completed,
    Failed,
    Expired,
    Locked
};
enum class NarrativeObjectiveRuntimeState
{
    Hidden,
    Available,
    Active,
    Completed,
    Failed,
    Cancelled,
    OptionalMissed
};
enum class ConditionEvaluationState
{
    Satisfied,
    Unsatisfied,
    Unknown,
    Unavailable,
    Partial
};
enum class ConsequenceExecutionState
{
    Pending,
    Applied,
    Deferred,
    FailedRetryable,
    FailedPermanent,
    Unsupported,
    AlreadyApplied
};
enum class JournalVisibilityState
{
    Hidden,
    Discovered,
    Updated,
    Resolved,
    Archived,
    Suppressed
};
enum class RumorState
{
    Active,
    Spread,
    Discredited,
    Expired,
    Confirmed,
    Suppressed
};
enum class NarrativeChoiceState
{
    Open,
    Resolved,
    Cancelled,
    Expired
};
enum class NarrativeGateState
{
    Closed,
    Open,
    Locked,
    Disabled
};
enum class NarrativeChangeKind
{
    ThreadStarted,
    ThreadCompleted,
    ThreadFailed,
    ThreadSuspended,
    ThreadResumed,
    ObjectiveActivated,
    ObjectiveCompleted,
    ObjectiveFailed,
    ObjectiveProgressChanged,
    JournalEntryAdded,
    ClueDiscovered,
    RumorCreated,
    RumorExpired,
    ChoiceCreated,
    ChoiceResolved,
    StoryletActivated,
    ConsequencePlanned,
    ConsequenceApplied,
    ConsequenceFailed,
    EventProcessed,
    EventDeduplicated,
    FlagChanged,
    VariableChanged
};

using NarrativeValue = std::variant<bool, std::int64_t, GameplayObjectRef, GameplayTimePoint, TypeId>;
struct NarrativeReason
{
    TypeId type{};
    std::vector<std::byte> payload;
};
struct NarrativeEvent
{
    NarrativeEventTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef instigator{};
    GameplayObjectRef target{};
    GameplayObjectRef area{};
    GameplayTimePoint time{};
    GameplayTagSet tags{};
    std::vector<std::byte> payload;
    CorrelationId correlation{};
};
struct NarrativeEventKey
{
    NarrativeEventTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef instigator{};
    GameplayObjectRef target{};
    GameplayObjectRef area{};
    GameplayTimePoint time{};
    CorrelationId correlation{};
    [[nodiscard]] constexpr bool operator==(const NarrativeEventKey &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeEventKey &) const noexcept = default;
};
struct NarrativeConsequenceKey
{
    NarrativeThreadId thread{};
    NarrativeBeatId beat{};
    NarrativeObjectiveId objective{};
    NarrativeConsequenceId consequence{};
    CorrelationId correlation{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return consequence.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const NarrativeConsequenceKey &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const NarrativeConsequenceKey &) const noexcept = default;
};
struct NarrativeEventKeyHash
{
    [[nodiscard]] std::size_t operator()(const NarrativeEventKey &key) const noexcept
    {
        auto h = std::hash<TypeId>{}(key.type.value);
        Combine(h, std::hash<GameplayObjectRef>{}(key.subject));
        Combine(h, std::hash<GameplayObjectRef>{}(key.instigator));
        Combine(h, std::hash<GameplayObjectRef>{}(key.target));
        Combine(h, std::hash<GameplayObjectRef>{}(key.area));
        Combine(h, std::hash<std::int64_t>{}(key.time.ticks));
        Combine(h, std::hash<CorrelationId>{}(key.correlation));
        return h;
    }

  private:
    static void Combine(std::size_t &h, std::size_t v) noexcept
    {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
    }
};
struct NarrativeConsequenceKeyHash
{
    [[nodiscard]] std::size_t operator()(const NarrativeConsequenceKey &key) const noexcept
    {
        auto h = std::hash<GameplayObjectId>{}(key.thread.value);
        Combine(h, std::hash<GameplayObjectId>{}(key.beat.value));
        Combine(h, std::hash<GameplayObjectId>{}(key.objective.value));
        Combine(h, std::hash<GameplayObjectId>{}(key.consequence.value));
        Combine(h, std::hash<CorrelationId>{}(key.correlation));
        return h;
    }

  private:
    static void Combine(std::size_t &h, std::size_t v) noexcept
    {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
    }
};
struct NarrativeEvaluationContext
{
    NarrativeEvent event{};
    GameplayObjectRef viewer{};
    GameplayTimePoint now{};
};
struct NarrativeExecutionContext
{
    GameplayObjectRef default_owner{};
    GameplayTimePoint now{};
    GameplayContext gameplay{};
};
struct NarrativeConditionResult
{
    NarrativeConditionId condition{};
    ConditionEvaluationState state = ConditionEvaluationState::Unavailable;
    std::int64_t score = 0;
    std::vector<NarrativeReason> reasons;
    Revision dependencies_revision{};
};
struct NarrativeConsequenceResult
{
    ConsequenceExecutionState state = ConsequenceExecutionState::Unsupported;
    Revision revision{};
};

struct NarrativeConditionDefinition
{
    NarrativeConditionId id{};
    NarrativeConditionTypeId type{};
    GameplayTagSet tags{};
    std::vector<NarrativeConditionId> all_of;
    std::vector<NarrativeConditionId> any_of;
    std::vector<std::byte> payload;
    Revision revision{};
};
struct NarrativeConsequenceDefinition
{
    NarrativeConsequenceId id{};
    NarrativeConsequenceTypeId type{};
    GameplayTagSet tags{};
    std::int32_t priority = 0;
    bool require_success_before_advance = false;
    std::vector<std::byte> payload;
    Revision revision{};
};
struct NarrativeBeatDefinition
{
    NarrativeBeatId id{};
    NarrativeThreadId thread{};
    GameplayTagSet tags{};
    std::vector<NarrativeConditionId> activation_conditions;
    std::vector<NarrativeConditionId> completion_conditions;
    std::vector<NarrativeConsequenceId> consequences;
    std::int32_t order = 0;
    Revision revision{};
};
struct NarrativeObjectiveDefinition
{
    NarrativeObjectiveId id{};
    NarrativeThreadId thread{};
    GameplayTagSet tags{};
    std::vector<NarrativeConditionId> start_conditions;
    std::vector<NarrativeConditionId> completion_conditions;
    std::vector<NarrativeConditionId> failure_conditions;
    bool player_visible = false;
    Revision revision{};
};
struct NarrativeThreadDefinition
{
    NarrativeThreadId id{};
    GameplayTagSet tags{};
    NarrativeThreadTypeId type{};
    std::vector<NarrativeBeatId> beats;
    std::vector<NarrativeObjectiveId> root_objectives;
    NarrativeRuntimeState initial_state = NarrativeRuntimeState::Hidden;
    std::int32_t priority = 0;
    Revision revision{};
};
struct NarrativeArcDefinition
{
    NarrativeArcId id{};
    GameplayTagSet tags{};
    std::vector<NarrativeThreadId> threads;
    Revision revision{};
};
struct StoryletDefinition
{
    StoryletId id{};
    GameplayTagSet tags{};
    std::vector<NarrativeConditionId> availability_conditions;
    std::vector<NarrativeConsequenceId> consequences;
    std::int32_t priority = 0;
    std::uint32_t max_activations = 1;
    GameplayDuration cooldown{};
    Revision revision{};
};
struct NarrativeChoiceOption
{
    NarrativeChoiceOptionId id{};
    GameplayTagSet tags{};
    std::vector<NarrativeConditionId> availability_conditions;
    std::vector<NarrativeConsequenceId> consequences;
    std::vector<std::byte> payload;
};

struct NarrativeThreadState
{
    NarrativeThreadId thread{};
    NarrativeRuntimeState state = NarrativeRuntimeState::Hidden;
    GameplayObjectRef owner_subject{};
    GameplayObjectRef scope{};
    GameplayTimePoint started_at{};
    GameplayTimePoint updated_at{};
    Revision revision{};
};
struct NarrativeObjectiveState
{
    NarrativeObjectiveId objective{};
    NarrativeObjectiveRuntimeState state = NarrativeObjectiveRuntimeState::Hidden;
    GameplayTimePoint started_at{};
    GameplayTimePoint completed_at{};
    std::int64_t progress = 0;
    Revision revision{};
};
struct NarrativeConsequenceExecution
{
    NarrativeConsequenceExecutionId id{};
    NarrativeThreadId thread{};
    NarrativeBeatId beat{};
    NarrativeObjectiveId objective{};
    NarrativeConsequenceId consequence{};
    ConsequenceExecutionState state = ConsequenceExecutionState::Pending;
    GameplayTimePoint planned_at{};
    GameplayTimePoint applied_at{};
    CorrelationId correlation{};
    std::uint32_t attempts = 0;
    NarrativeConsequenceKey idempotency_key{};
    Revision revision{};
};
struct JournalEntry
{
    JournalEntryId id{};
    GameplayObjectRef owner{};
    NarrativeThreadId thread{};
    NarrativeObjectiveId objective{};
    JournalEntryTypeId type{};
    JournalVisibilityState visibility = JournalVisibilityState::Hidden;
    GameplayTimePoint discovered_at{};
    GameplayTagSet tags{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct ClueRecord
{
    ClueId id{};
    GameplayObjectRef owner{};
    TypeId type{};
    TypeId topic{};
    GameplayObjectRef area{};
    std::int64_t confidence = 0;
    GameplayTimePoint discovered_at{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct RumorRecord
{
    RumorId id{};
    GameplayObjectRef owner_or_scope{};
    TypeId topic{};
    RumorState state = RumorState::Active;
    std::int64_t confidence = 0;
    GameplayTimePoint created_at{};
    GameplayTimePoint expires_at{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct NarrativeFlag
{
    NarrativeFlagId id{};
    GameplayObjectRef scope{};
    bool value = false;
    GameplayTimePoint changed_at{};
    Revision revision{};
};
struct NarrativeVariable
{
    NarrativeVariableId id{};
    GameplayObjectRef scope{};
    NarrativeValue value{};
    Revision revision{};
};
struct NarrativeChoice
{
    NarrativeChoiceId id{};
    NarrativeThreadId thread{};
    GameplayObjectRef actor{};
    std::vector<NarrativeChoiceOption> options;
    NarrativeChoiceState state = NarrativeChoiceState::Open;
    std::optional<NarrativeChoiceOptionId> selected_option;
    Revision revision{};
};
struct StoryletRuntimeState
{
    StoryletId id{};
    std::uint32_t activations = 0;
    GameplayTimePoint last_activated_at{};
    Revision revision{};
};
struct NarrativeChange
{
    std::uint64_t sequence = 0;
    NarrativeChangeKind kind = NarrativeChangeKind::EventProcessed;
    NarrativeThreadId thread{};
    NarrativeObjectiveId objective{};
    NarrativeBeatId beat{};
    NarrativeConsequenceExecutionId consequence_execution{};
    JournalEntryId journal{};
    ClueId clue{};
    RumorId rumor{};
    NarrativeChoiceId choice{};
    GameplayObjectRef subject{};
    GameplayContext context{};
    Revision revision{};
};

struct NarrativeSnapshot
{
    std::vector<NarrativeThreadDefinition> thread_definitions;
    std::vector<NarrativeArcDefinition> arc_definitions;
    std::vector<NarrativeBeatDefinition> beat_definitions;
    std::vector<NarrativeObjectiveDefinition> objective_definitions;
    std::vector<NarrativeConditionDefinition> condition_definitions;
    std::vector<NarrativeConsequenceDefinition> consequence_definitions;
    std::vector<StoryletDefinition> storylet_definitions;
    std::vector<NarrativeThreadState> thread_states;
    std::vector<NarrativeObjectiveState> objective_states;
    std::vector<NarrativeConsequenceExecution> consequences;
    std::vector<JournalEntry> journal_entries;
    std::vector<ClueRecord> clues;
    std::vector<RumorRecord> rumors;
    std::vector<NarrativeFlag> flags;
    std::vector<NarrativeVariable> variables;
    std::vector<NarrativeChoice> choices;
    std::vector<StoryletRuntimeState> storylet_runtime;
    std::vector<NarrativeEventKey> processed_event_keys;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot journal_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot clue_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot rumor_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot choice_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot consequence_ids{};
    Revision revision{};
    bool frozen = false;
};
struct NarrativeDiagnostics
{
    std::uint64_t threads = 0, active_threads = 0, completed_threads = 0, objectives = 0, active_objectives = 0,
                  completed_objectives = 0, journal_entries = 0, clues = 0, rumors = 0, storylets = 0,
                  events_processed = 0, deduplicated_events = 0, condition_evaluations = 0, consequences_planned = 0,
                  consequences_applied = 0, consequences_failed = 0, storylets_evaluated = 0, storylets_activated = 0,
                  budget_exhaustions = 0, idempotency_hits = 0;
};

class INarrativeConditionResolver
{
  public:
    virtual ~INarrativeConditionResolver() = default;
    [[nodiscard]] virtual NarrativeConditionResult Evaluate(const NarrativeConditionDefinition &definition,
                                                            const NarrativeEvaluationContext &context) const = 0;
};
class INarrativeConsequenceHandler
{
  public:
    virtual ~INarrativeConsequenceHandler() = default;
    [[nodiscard]] virtual NarrativeConsequenceResult Execute(const NarrativeConsequenceDefinition &definition,
                                                             const NarrativeConsequenceExecution &execution,
                                                             const NarrativeExecutionContext &context) const = 0;
};
struct NarrativeProcessBudget
{
    std::size_t max_condition_evaluations = 1024;
    std::size_t max_storylets_evaluated = 256;
    std::size_t max_storylets_activated = 16;
    std::size_t max_consequences_planned = 256;
};

class NarrativeService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.narrative");
    }
    [[nodiscard]] foundation::Result<void> RegisterThreadDefinition(NarrativeThreadDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterArcDefinition(NarrativeArcDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterBeatDefinition(NarrativeBeatDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterObjectiveDefinition(NarrativeObjectiveDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterConditionDefinition(NarrativeConditionDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterConsequenceDefinition(NarrativeConsequenceDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterStoryletDefinition(StoryletDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterConditionResolver(NarrativeConditionTypeId type,
                                                                     const INarrativeConditionResolver &resolver);
    [[nodiscard]] foundation::Result<void> RegisterConsequenceHandler(NarrativeConsequenceTypeId type,
                                                                      const INarrativeConsequenceHandler &handler);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] foundation::Result<void> StartThread(NarrativeThreadId thread, GameplayObjectRef owner,
                                                       GameplayObjectRef scope, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SuspendThread(NarrativeThreadId thread, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteThread(NarrativeThreadId thread, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> FailThread(NarrativeThreadId thread, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ActivateObjective(NarrativeObjectiveId objective,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteObjective(NarrativeObjectiveId objective,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> FailObjective(NarrativeObjectiveId objective, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> UpdateObjectiveProgress(NarrativeObjectiveId objective,
                                                                   std::int64_t progress, GameplayContext context = {});
    [[nodiscard]] NarrativeConditionResult EvaluateCondition(NarrativeConditionId condition,
                                                             NarrativeEvaluationContext context) const;
    [[nodiscard]] foundation::Result<void> ProcessNarrativeEvent(NarrativeEvent event,
                                                                 NarrativeProcessBudget budget = {});
    [[nodiscard]] foundation::Result<JournalEntryId> AddJournalEntry(JournalEntry entry, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ClueId> DiscoverClue(ClueRecord clue, GameplayContext context = {});
    [[nodiscard]] foundation::Result<RumorId> CreateRumor(RumorRecord rumor, GameplayContext context = {});
    [[nodiscard]] std::vector<RumorId> ExpireRumors(GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<NarrativeChoiceId> CreateChoice(NarrativeChoice choice,
                                                                     GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ResolveChoice(NarrativeChoiceId choice, NarrativeChoiceOptionId option,
                                                         GameplayContext context = {});
    [[nodiscard]] std::vector<NarrativeConsequenceExecutionId> ExecutePendingConsequences(
        NarrativeExecutionContext context, std::size_t budget = 256);
    [[nodiscard]] foundation::Result<void> SetFlag(NarrativeFlag flag, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetVariable(NarrativeVariable variable, GameplayContext context = {});
    [[nodiscard]] const NarrativeThreadState *GetThreadState(NarrativeThreadId thread) const noexcept;
    [[nodiscard]] const NarrativeObjectiveState *GetObjectiveState(NarrativeObjectiveId objective) const noexcept;
    [[nodiscard]] const JournalEntry *GetJournalEntry(JournalEntryId id) const noexcept;
    [[nodiscard]] const ClueRecord *GetClue(ClueId id) const noexcept;
    [[nodiscard]] const RumorRecord *GetRumor(RumorId id) const noexcept;
    [[nodiscard]] const NarrativeChoice *GetChoice(NarrativeChoiceId id) const noexcept;
    [[nodiscard]] std::vector<JournalEntry> GetJournal(GameplayObjectRef owner) const;
    [[nodiscard]] std::vector<ClueRecord> FindClues(GameplayObjectRef owner, TypeId topic = {}) const;
    [[nodiscard]] std::vector<RumorRecord> FindRumors(GameplayObjectRef scope, TypeId topic = {}) const;
    [[nodiscard]] std::vector<NarrativeConsequenceExecution> FindConsequences(ConsequenceExecutionState state) const;
    [[nodiscard]] std::vector<NarrativeChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] NarrativeSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(NarrativeSnapshot snapshot);
    [[nodiscard]] NarrativeDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(NarrativeChange change);
    [[nodiscard]] bool AllSatisfied(const std::vector<NarrativeConditionId> &conditions,
                                    const NarrativeEvaluationContext &context) const;
    [[nodiscard]] bool AnySatisfied(const std::vector<NarrativeConditionId> &conditions,
                                    const NarrativeEvaluationContext &context) const;
    [[nodiscard]] foundation::Result<void> PlanConsequences(NarrativeThreadId thread, NarrativeBeatId beat,
                                                            NarrativeObjectiveId objective,
                                                            const std::vector<NarrativeConsequenceId> &consequences,
                                                            CorrelationId correlation, GameplayTimePoint now);
    [[nodiscard]] NarrativeEventKey MakeEventKey(const NarrativeEvent &event) const noexcept;
    [[nodiscard]] NarrativeConsequenceKey MakeConsequenceKey(NarrativeThreadId thread, NarrativeBeatId beat,
                                                             NarrativeObjectiveId objective,
                                                             NarrativeConsequenceId consequence,
                                                             CorrelationId correlation) const noexcept;
    [[nodiscard]] bool IsBuiltinAddJournal(NarrativeConsequenceTypeId type) const noexcept;
    [[nodiscard]] bool IsBuiltinCreateRumor(NarrativeConsequenceTypeId type) const noexcept;
    [[nodiscard]] NarrativeThreadState &EnsureThreadState(NarrativeThreadId thread);
    [[nodiscard]] NarrativeObjectiveState &EnsureObjectiveState(NarrativeObjectiveId objective);
    Revision revision_{};
    bool frozen_ = false;
    MonotonicIdGenerator<GameplayObjectId> journal_ids_{0x3300}, clue_ids_{0x3301}, rumor_ids_{0x3302},
        choice_ids_{0x3303}, consequence_ids_{0x3304};
    std::unordered_map<NarrativeThreadId, NarrativeThreadDefinition, IdHash> thread_defs_;
    std::unordered_map<NarrativeArcId, NarrativeArcDefinition, IdHash> arc_defs_;
    std::unordered_map<NarrativeBeatId, NarrativeBeatDefinition, IdHash> beat_defs_;
    std::unordered_map<NarrativeObjectiveId, NarrativeObjectiveDefinition, IdHash> objective_defs_;
    std::unordered_map<NarrativeConditionId, NarrativeConditionDefinition, IdHash> condition_defs_;
    std::unordered_map<NarrativeConsequenceId, NarrativeConsequenceDefinition, IdHash> consequence_defs_;
    std::unordered_map<StoryletId, StoryletDefinition, IdHash> storylet_defs_;
    std::unordered_map<NarrativeConditionTypeId, const INarrativeConditionResolver *, IdHash> condition_resolvers_;
    std::unordered_map<NarrativeConsequenceTypeId, const INarrativeConsequenceHandler *, IdHash> consequence_handlers_;
    std::unordered_map<NarrativeThreadId, NarrativeThreadState, IdHash> threads_;
    std::unordered_map<NarrativeObjectiveId, NarrativeObjectiveState, IdHash> objectives_;
    std::unordered_map<NarrativeConsequenceExecutionId, NarrativeConsequenceExecution, IdHash> consequences_;
    std::unordered_map<JournalEntryId, JournalEntry, IdHash> journal_;
    std::unordered_map<ClueId, ClueRecord, IdHash> clues_;
    std::unordered_map<RumorId, RumorRecord, IdHash> rumors_;
    std::unordered_map<NarrativeFlagId, NarrativeFlag, IdHash> flags_;
    std::unordered_map<NarrativeVariableId, NarrativeVariable, IdHash> variables_;
    std::unordered_map<NarrativeChoiceId, NarrativeChoice, IdHash> choices_;
    std::unordered_map<StoryletId, StoryletRuntimeState, IdHash> storylet_runtime_;
    std::unordered_map<NarrativeConsequenceKey, NarrativeConsequenceExecutionId, NarrativeConsequenceKeyHash>
        consequence_idempotency_;
    std::unordered_set<NarrativeEventKey, NarrativeEventKeyHash> processed_event_keys_;
    std::vector<NarrativeBeatId> activated_beats_;
    std::vector<NarrativeChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    mutable NarrativeDiagnostics diagnostics_{};
    mutable std::size_t evaluation_counter_ = 0;
};
} // namespace epidemic::gameplay::narrative
