#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::abilities
{
struct AbilityDefinitionId
{
    TypeId value{};
    [[nodiscard]] static constexpr AbilityDefinitionId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const AbilityDefinitionId &) const noexcept = default;
};
struct AbilityInstanceId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const AbilityInstanceId &) const noexcept = default;
};
struct AbilityExecutionId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const AbilityExecutionId &) const noexcept = default;
};
struct CooldownGroupId
{
    TypeId value{};
    [[nodiscard]] static constexpr CooldownGroupId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const CooldownGroupId &) const noexcept = default;
};
struct AbilityResourceTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr AbilityResourceTypeId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const AbilityResourceTypeId &) const noexcept = default;
};
struct AbilityReservationId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const AbilityReservationId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        if constexpr (requires { id.value.Raw(); })
            return std::hash<TypeId>{}(id.value);
        else
            return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct RegisteredAbilityPayload
{
    TypeId type{};
    std::vector<std::byte> bytes;
    template <class T> [[nodiscard]] static RegisteredAbilityPayload FromTrivial(TypeId t, const T &v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredAbilityPayload p;
        p.type = t;
        p.bytes.resize(sizeof(T));
        std::memcpy(p.bytes.data(), &v, sizeof(T));
        return p;
    }
    template <class T> [[nodiscard]] std::optional<T> AsTrivial(TypeId t) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (type != t || bytes.size() != sizeof(T))
            return std::nullopt;
        T v{};
        std::memcpy(&v, bytes.data(), sizeof(T));
        return v;
    }
};

enum class AbilityTargetPolicy
{
    Self,
    SingleTarget,
    MultipleTargets,
    Point,
    Area,
    Direction,
    None
};
enum class AbilityTimingKind
{
    Instant,
    CastTime,
    Channel,
    DelayedExecution
};
enum class AbilityCostPolicy
{
    PayOnStart,
    ReserveThenCommit,
    PayOnExecute,
    PayPerChannelTick
};
enum class AbilityGrantPersistence
{
    Permanent,
    Temporary,
    SourceBound
};

struct AbilityCostDefinition
{
    AbilityResourceTypeId resource{};
    std::int64_t amount_micro = 0;
    AbilityCostPolicy policy = AbilityCostPolicy::ReserveThenCommit;
};
struct AbilityCooldownDefinition
{
    GameplayDuration duration{};
    CooldownGroupId group{};
    bool starts_on_begin = false;
};
struct AbilityTimingDefinition
{
    AbilityTimingKind kind = AbilityTimingKind::Instant;
    GameplayDuration cast_duration{};
    GameplayDuration channel_interval{};
    GameplayDuration max_channel_duration{};
};
struct AbilityOutputDefinition
{
    ActionTypeId action{};
    std::int64_t magnitude_micro = 0;
    RegisteredAbilityPayload payload;
};
struct AbilityDefinition
{
    AbilityDefinitionId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    AbilityTargetPolicy targeting = AbilityTargetPolicy::None;
    AbilityTimingDefinition timing{};
    AbilityCooldownDefinition cooldown{};
    std::vector<AbilityCostDefinition> costs;
    std::vector<AbilityOutputDefinition> outputs;
    bool requires_materialized_owner = false;
    bool requires_materialized_target = false;
};

struct AbilityTargetSet
{
    GameplayObjectRef primary{};
    std::vector<GameplayObjectRef> targets;
    std::optional<std::array<std::int64_t, 3>> point_micro{};
};

struct AbilityInstance
{
    AbilityInstanceId id{};
    AbilityDefinitionId definition{};
    GameplayObjectRef owner{};
    GameplayObjectRef source{};
    AbilityGrantPersistence persistence = AbilityGrantPersistence::Permanent;
    bool enabled = true;
    Revision revision{};
};

enum class AbilityExecutionState
{
    Preparing,
    Casting,
    Channeling,
    Executing,
    Completed,
    Interrupted,
    Failed
};
struct AbilityResourceReservation
{
    AbilityReservationId id{};
    AbilityResourceTypeId resource{};
    RegisteredAbilityPayload provider_token;
};
struct AbilityExecution
{
    AbilityExecutionId id{};
    AbilityInstanceId ability{};
    GameplayObjectRef owner{};
    AbilityTargetSet targets;
    AbilityExecutionState state = AbilityExecutionState::Preparing;
    GameplayTimePoint started_at{};
    GameplayTimePoint due_at{};
    GameplayTimePoint next_channel_at{};
    std::vector<AbilityResourceReservation> reservations;
    std::optional<ScheduleId> schedule{};
    GameplayContext context{};
    Revision revision{};
};

struct AbilityActivationRequest
{
    AbilityInstanceId ability{};
    AbilityTargetSet targets;
    GameplayTimePoint now{};
    GameplayContext context{};
};
enum class AbilityAvailability
{
    Available,
    Unavailable
};
struct AbilityAvailabilityResult
{
    AbilityAvailability availability = AbilityAvailability::Unavailable;
    TypeId reason{};
};

class IAbilityRequirementProvider
{
  public:
    virtual ~IAbilityRequirementProvider() = default;
    [[nodiscard]] virtual AbilityAvailabilityResult Check(const AbilityDefinition &definition,
                                                          const AbilityInstance &instance,
                                                          const AbilityTargetSet &targets,
                                                          GameplayTimePoint now) const = 0;
};
class IAbilityResourceProvider
{
  public:
    virtual ~IAbilityResourceProvider() = default;
    [[nodiscard]] virtual bool CanAfford(GameplayObjectRef owner, AbilityResourceTypeId type,
                                         std::int64_t amount_micro) const = 0;
    [[nodiscard]] virtual foundation::Result<AbilityResourceReservation> Reserve(GameplayObjectRef owner,
                                                                                 AbilityResourceTypeId type,
                                                                                 std::int64_t amount_micro,
                                                                                 GameplayContext context) = 0;
    [[nodiscard]] virtual foundation::Result<void> Commit(const AbilityResourceReservation &reservation,
                                                          GameplayContext context) = 0;
    [[nodiscard]] virtual foundation::Result<void> Release(const AbilityResourceReservation &reservation,
                                                           GameplayContext context) = 0;
};

struct AbilityOutput
{
    AbilityExecutionId execution{};
    ActionTypeId action{};
    GameplayObjectRef owner{};
    AbilityTargetSet targets;
    std::int64_t magnitude_micro = 0;
    RegisteredAbilityPayload payload;
    GameplayContext context{};
};

enum class AbilityChangeKind
{
    Granted,
    Revoked,
    Started,
    Executed,
    Interrupted,
    Failed,
    CooldownStarted,
    CooldownFinished
};
struct AbilityChange
{
    std::uint64_t sequence = 0;
    AbilityChangeKind kind = AbilityChangeKind::Granted;
    GameplayObjectRef owner{};
    AbilityInstanceId ability{};
    AbilityExecutionId execution{};
    GameplayTimePoint time{};
    GameplayContext context{};
};
struct AbilityCooldownState
{
    GameplayObjectRef owner{};
    CooldownGroupId group{};
    GameplayTimePoint ends_at{};
};
struct AbilitiesSnapshot
{
    std::vector<AbilityInstance> instances;
    std::vector<AbilityExecution> executions;
    std::vector<AbilityCooldownState> cooldowns;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot instance_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot execution_ids{};
};
struct AbilitiesDiagnostics
{
    std::uint64_t instances = 0, activation_attempts = 0, activations = 0, failed = 0, interrupts = 0, cooldowns = 0,
                  outputs = 0;
};

class AbilityService
{
  public:
    AbilityService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.abilities");
    }
    [[nodiscard]] foundation::Result<AbilityDefinitionId> RegisterDefinition(AbilityDefinition definition);
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    [[nodiscard]] const AbilityDefinition *FindDefinition(AbilityDefinitionId id) const noexcept;
    void SetRequirementProvider(const IAbilityRequirementProvider *provider) noexcept
    {
        requirements_ = provider;
    }
    void SetResourceProvider(IAbilityResourceProvider *provider) noexcept
    {
        resources_ = provider;
    }

    [[nodiscard]] foundation::Result<AbilityInstanceId> Grant(
        GameplayObjectRef owner, AbilityDefinitionId definition, GameplayObjectRef source = {},
        AbilityGrantPersistence persistence = AbilityGrantPersistence::Permanent, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Revoke(AbilityInstanceId instance, GameplayContext context = {});
    [[nodiscard]] std::uint64_t RevokeBySource(GameplayObjectRef owner, GameplayObjectRef source,
                                               GameplayContext context = {});
    [[nodiscard]] const AbilityInstance *FindInstance(AbilityInstanceId id) const noexcept;
    [[nodiscard]] std::vector<AbilityInstance> GetAbilities(GameplayObjectRef owner) const;

    [[nodiscard]] AbilityAvailabilityResult CanActivate(AbilityInstanceId ability, const AbilityTargetSet &targets,
                                                        GameplayTimePoint now) const;
    [[nodiscard]] foundation::Result<AbilityExecutionId> BeginActivation(AbilityActivationRequest request);
    [[nodiscard]] foundation::Result<std::vector<AbilityOutput>> CompleteExecution(AbilityExecutionId execution,
                                                                                   GameplayTimePoint now);
    [[nodiscard]] foundation::Result<std::vector<AbilityOutput>> ChannelTick(AbilityExecutionId execution,
                                                                             GameplayTimePoint now,
                                                                             std::uint64_t occurrences = 1);
    [[nodiscard]] foundation::Result<void> Interrupt(AbilityExecutionId execution, TypeId reason, GameplayTimePoint now,
                                                     GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> BindSchedule(AbilityExecutionId execution, ScheduleId schedule);
    [[nodiscard]] foundation::Result<void> NotifyScheduleDue(ScheduleId schedule, GameplayTimePoint now,
                                                             std::vector<AbilityOutput> &outputs);

    [[nodiscard]] GameplayDuration CooldownRemaining(GameplayObjectRef owner, CooldownGroupId group,
                                                     GameplayTimePoint now) const noexcept;
    [[nodiscard]] const AbilityExecution *FindExecution(AbilityExecutionId id) const noexcept;
    [[nodiscard]] std::vector<AbilityChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] AbilitiesSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(AbilitiesSnapshot snapshot);
    [[nodiscard]] AbilitiesDiagnostics GetDiagnostics() const noexcept;

  private:
    [[nodiscard]] foundation::Result<void> AcquireStartCosts(const AbilityDefinition &def, AbilityExecution &execution);
    [[nodiscard]] foundation::Result<void> AcquireExecuteCosts(const AbilityDefinition &def,
                                                               AbilityExecution &execution);
    [[nodiscard]] foundation::Result<void> CommitReservations(AbilityExecution &execution);
    void StartCooldown(const AbilityDefinition &def, GameplayObjectRef owner, GameplayTimePoint now,
                       GameplayContext context, AbilityInstanceId ability, AbilityExecutionId execution);
    [[nodiscard]] std::vector<AbilityOutput> BuildOutputs(const AbilityDefinition &def,
                                                          const AbilityExecution &execution) const;
    void Record(AbilityChange change);

    std::unordered_map<AbilityDefinitionId, AbilityDefinition, IdHash> definitions_;
    std::unordered_map<AbilityInstanceId, AbilityInstance, IdHash> instances_;
    std::unordered_map<AbilityExecutionId, AbilityExecution, IdHash> executions_;
    std::vector<AbilityCooldownState> cooldowns_;
    MonotonicIdGenerator<GameplayObjectId> instance_ids_;
    MonotonicIdGenerator<GameplayObjectId> execution_ids_;
    const IAbilityRequirementProvider *requirements_ = nullptr;
    IAbilityResourceProvider *resources_ = nullptr;
    bool frozen_ = false;
    std::vector<AbilityChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    AbilitiesDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::abilities
