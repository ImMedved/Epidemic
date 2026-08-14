#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::conditions
{
struct ConditionTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr ConditionTypeId FromString(std::string_view name) noexcept { return ConditionTypeId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const ConditionTypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ConditionTypeId&) const noexcept = default;
};

struct ConditionInstanceId
{
    GameplayObjectId value{};
    [[nodiscard]] static constexpr ConditionInstanceId FromRaw(std::uint64_t high, std::uint64_t low) noexcept
    {
        return ConditionInstanceId{GameplayObjectId::FromRaw(high, low)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t High() const noexcept { return value.High(); }
    [[nodiscard]] constexpr std::uint64_t Low() const noexcept { return value.Low(); }
    [[nodiscard]] constexpr bool operator==(const ConditionInstanceId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ConditionInstanceId&) const noexcept = default;
};

struct ConditionTypeIdHash
{
    [[nodiscard]] std::size_t operator()(ConditionTypeId id) const noexcept { return std::hash<TypeId>{}(id.value); }
};

struct ConditionInstanceIdHash
{
    [[nodiscard]] std::size_t operator()(const ConditionInstanceId& id) const noexcept
    {
        return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct RegisteredConditionPayload
{
    TypeId type{};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool Empty() const noexcept { return bytes.empty(); }

    template <typename T>
    [[nodiscard]] static RegisteredConditionPayload FromTrivial(TypeId type_id, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredConditionPayload result;
        result.type = type_id;
        result.bytes.resize(sizeof(T));
        std::memcpy(result.bytes.data(), &value, sizeof(T));
        return result;
    }

    template <typename T> [[nodiscard]] std::optional<T> AsTrivial(TypeId expected_type) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (type != expected_type || bytes.size() != sizeof(T))
        {
            return std::nullopt;
        }
        T value{};
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }
};

enum class ConditionStackingPolicy
{
    Independent,
    UniquePerSubject,
    UniquePerSource,
    RefreshDuration,
    ExtendDuration,
    AddStacks,
    ReplaceIfStronger,
    ReplaceExisting,
};

enum class ConditionPersistencePolicy
{
    Transient,
    Session,
    Persistent,
};

enum class ConditionMaterializationPolicy
{
    Always,
    AbstractCapable,
    MaterializedOnly,
};

enum class ConditionDematerializationPolicy
{
    Pause,
    Remove,
    Aggregate,
    RejectDematerialization,
};

enum class PeriodicCatchUpPolicy
{
    FireEach,
    FireOnce,
    Aggregate,
    Skip,
};

struct ConditionDefinition
{
    ConditionTypeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    ConditionStackingPolicy stacking = ConditionStackingPolicy::Independent;
    std::uint32_t max_stacks = 1;
    GameplayDuration default_duration{};
    GameplayDuration periodic_interval{};
    ClockId clock{};
    ConditionPersistencePolicy persistence = ConditionPersistencePolicy::Session;
    ActionTypeId on_apply{};
    ActionTypeId on_periodic{};
    ActionTypeId on_remove{};
    TypeId payload_type{};
    std::size_t max_payload_bytes = 0;
    bool publish_fact = false;
    ConditionMaterializationPolicy materialization = ConditionMaterializationPolicy::AbstractCapable;
    ConditionDematerializationPolicy dematerialization = ConditionDematerializationPolicy::Aggregate;
    PeriodicCatchUpPolicy periodic_catch_up = PeriodicCatchUpPolicy::Aggregate;
};

struct ConditionInstance
{
    ConditionInstanceId id{};
    ConditionTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef source{};
    GameplayObjectRef instigator{};
    GameplayTimePoint applied_at{};
    std::optional<GameplayTimePoint> expires_at{};
    std::int64_t magnitude_micro = 0;
    std::uint32_t stacks = 1;
    RegisteredConditionPayload payload;
    std::optional<ScheduleId> expiration_schedule{};
    std::optional<ScheduleId> periodic_schedule{};
    bool paused_for_materialization = false;
    Revision revision{};
};

struct ApplyConditionRequest
{
    ConditionTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef source{};
    GameplayObjectRef instigator{};
    std::int64_t magnitude_micro = 0;
    std::optional<GameplayDuration> duration{};
    RegisteredConditionPayload payload;
    GameplayContext context{};
};

enum class ConditionApplyDisposition
{
    Added,
    Refreshed,
    Stacked,
    Replaced,
    Rejected,
    NoOp,
};

struct ApplyConditionResult
{
    ConditionApplyDisposition disposition = ConditionApplyDisposition::Rejected;
    ConditionInstanceId instance{};
    std::optional<ConditionInstanceId> replaced_instance{};
};

enum class ConditionRemovalReason
{
    Expired,
    Dispelled,
    RemovedByEffect,
    SubjectDestroyed,
    Replaced,
    SystemCleanup,
};

enum class ConditionChangeKind
{
    Added,
    Refreshed,
    StackChanged,
    Removed,
    Expired,
    PeriodicDue,
    ScheduleLinksChanged,
    MaterializationPauseChanged,
};

struct ConditionChange
{
    std::uint64_t sequence = 0;
    ConditionChangeKind kind = ConditionChangeKind::Added;
    ConditionInstanceId instance{};
    ConditionTypeId type{};
    GameplayObjectRef subject{};
    ConditionRemovalReason removal_reason = ConditionRemovalReason::SystemCleanup;
    std::uint64_t occurrence_count = 1;
    std::optional<ScheduleId> expiration_schedule{};
    std::optional<ScheduleId> periodic_schedule{};
    Revision revision{};
    GameplayContext context{};
    GameplayObjectRef source{};
    GameplayObjectRef instigator{};
    std::int64_t magnitude_micro = 0;
    std::uint32_t stacks = 0;
};

struct ConditionsSnapshot
{
    std::vector<ConditionInstance> instances;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot id_generator{};
    Revision revision{};
};

struct ConditionsDiagnostics
{
    std::uint64_t active_conditions = 0;
    std::uint64_t applied = 0;
    std::uint64_t expired = 0;
    std::uint64_t removed = 0;
    std::uint64_t periodic_triggers = 0;
    std::uint64_t stack_merges = 0;
    std::uint64_t paused_for_materialization = 0;
};

class ConditionService
{
  public:
    using PayloadValidator = std::function<bool(std::span<const std::byte>)>;

    ConditionService();

    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.conditions");
    }

    [[nodiscard]] foundation::Result<ConditionTypeId> RegisterCondition(
        ConditionDefinition definition,
        PayloadValidator validator = {});
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const ConditionDefinition* FindDefinition(ConditionTypeId id) const noexcept;

    [[nodiscard]] foundation::Result<ApplyConditionResult> Apply(ApplyConditionRequest request);
    [[nodiscard]] foundation::Result<void> Remove(
        ConditionInstanceId instance,
        ConditionRemovalReason reason,
        GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveByType(
        GameplayObjectRef subject,
        ConditionTypeId type,
        ConditionRemovalReason reason,
        GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveBySource(
        GameplayObjectRef subject,
        GameplayObjectRef source,
        ConditionRemovalReason reason,
        GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveByTag(
        GameplayObjectRef subject,
        TagId tag,
        const GameplayTagRegistry& tags,
        ConditionRemovalReason reason,
        GameplayContext context = {});
    [[nodiscard]] std::uint64_t RemoveSubject(
        GameplayObjectRef subject,
        ConditionRemovalReason reason = ConditionRemovalReason::SubjectDestroyed,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> SetScheduleLinks(
        ConditionInstanceId instance,
        std::optional<ScheduleId> expiration,
        std::optional<ScheduleId> periodic,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> HandleExpirationDue(
        ConditionInstanceId instance,
        GameplayTimePoint observed_at,
        GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> HandlePeriodicDue(
        ConditionInstanceId instance,
        std::uint64_t occurrence_count,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> NotifySubjectMaterialization(
        GameplayObjectRef subject,
        bool materialized,
        GameplayContext context = {});

    [[nodiscard]] const ConditionInstance* Find(ConditionInstanceId id) const noexcept;
    [[nodiscard]] std::vector<ConditionInstance> GetConditions(GameplayObjectRef subject) const;
    [[nodiscard]] std::vector<ConditionInstance> AllConditions() const;
    [[nodiscard]] bool HasCondition(GameplayObjectRef subject, ConditionTypeId type) const;
    [[nodiscard]] bool HasConditionTag(GameplayObjectRef subject, TagId tag, const GameplayTagRegistry& tags) const;
    [[nodiscard]] std::vector<ConditionInstance> FindByType(ConditionTypeId type) const;
    [[nodiscard]] std::vector<ConditionInstance> FindBySource(GameplayObjectRef source) const;

    [[nodiscard]] std::vector<ConditionChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_ - 1; }
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] ConditionsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ConditionsSnapshot snapshot);
    [[nodiscard]] ConditionsDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    struct DefinitionEntry
    {
        ConditionDefinition definition;
        PayloadValidator validator;
    };

    [[nodiscard]] foundation::Result<void> ValidatePayload(const DefinitionEntry& definition, const RegisteredConditionPayload& payload) const;
    [[nodiscard]] std::vector<ConditionInstanceId> MatchingInstances(const ApplyConditionRequest& request, const ConditionDefinition& definition) const;
    [[nodiscard]] foundation::Result<ConditionInstanceId> AddNew(const ApplyConditionRequest& request, const ConditionDefinition& definition);
    [[nodiscard]] foundation::Result<void> RemoveAtIndex(std::size_t index, ConditionRemovalReason reason, GameplayContext context);
    [[nodiscard]] std::optional<GameplayTimePoint> ComputeExpiration(GameplayTimePoint now, GameplayDuration duration) const noexcept;
    void IndexInstance(const ConditionInstance& instance);
    void UnindexInstance(const ConditionInstance& instance);
    void RecordChange(ConditionChange change);
    void BumpRevision(ConditionInstance& instance) noexcept;

    std::unordered_map<ConditionTypeId, DefinitionEntry, ConditionTypeIdHash> definitions_;
    std::vector<ConditionInstance> instances_;
    std::unordered_map<ConditionInstanceId, std::size_t, ConditionInstanceIdHash> id_to_index_;
    std::unordered_map<GameplayObjectRef, std::vector<ConditionInstanceId>> subject_index_;

    MonotonicIdGenerator<GameplayObjectId> ids_;
    Revision revision_{};
    bool frozen_ = false;

    std::vector<ConditionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;

    std::uint64_t applied_ = 0;
    std::uint64_t expired_ = 0;
    std::uint64_t removed_ = 0;
    std::uint64_t periodic_triggers_ = 0;
    std::uint64_t stack_merges_ = 0;
};
} // namespace epidemic::gameplay::conditions
