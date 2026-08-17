#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace epidemic::core::tasks
{
class ITaskScheduler;
}

namespace epidemic::gameplay::effects
{
struct EffectTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr EffectTypeId FromString(std::string_view name) noexcept { return EffectTypeId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const EffectTypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EffectTypeId&) const noexcept = default;
};

struct EffectDefinitionId
{
    TypeId value{};
    [[nodiscard]] static constexpr EffectDefinitionId FromString(std::string_view name) noexcept { return EffectDefinitionId{TypeId::FromString(name)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr bool operator==(const EffectDefinitionId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EffectDefinitionId&) const noexcept = default;
};

struct EffectExecutionId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EffectExecutionId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EffectExecutionId&) const noexcept = default;
};

struct DeferredEffectId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr bool operator==(const DeferredEffectId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const DeferredEffectId&) const noexcept = default;
};

struct EffectTypeIdHash
{
    [[nodiscard]] std::size_t operator()(EffectTypeId id) const noexcept { return std::hash<TypeId>{}(id.value); }
};
struct EffectDefinitionIdHash
{
    [[nodiscard]] std::size_t operator()(EffectDefinitionId id) const noexcept { return std::hash<TypeId>{}(id.value); }
};
struct DeferredEffectIdHash
{
    [[nodiscard]] std::size_t operator()(const DeferredEffectId& id) const noexcept { return std::hash<GameplayObjectId>{}(id.value); }
};

struct RegisteredEffectPayload
{
    TypeId type{};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool Empty() const noexcept { return bytes.empty(); }

    template <typename T>
    [[nodiscard]] static RegisteredEffectPayload FromTrivial(TypeId type_id, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredEffectPayload result;
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

enum class EffectTargetSelector
{
    AllTargets,
    FirstTarget,
};

struct EffectStepDefinition
{
    EffectTypeId type{};
    EffectTargetSelector selector = EffectTargetSelector::AllTargets;
    std::int64_t magnitude_micro = 0;
    RegisteredEffectPayload payload;
};

enum class EffectExecutionPolicy
{
    BestEffort,
    RequireAllPrepared,
};

struct EffectDefinition
{
    EffectDefinitionId id{};
    std::string canonical_name;
    std::vector<EffectStepDefinition> steps;
    EffectExecutionPolicy policy = EffectExecutionPolicy::BestEffort;
};

struct EffectRequest
{
    EffectDefinitionId definition{};
    GameplayObjectRef source{};
    GameplayObjectRef instigator{};
    std::vector<GameplayObjectRef> targets;
    std::int64_t scale_micro = 1'000'000;
    GameplayContext context{};
};

struct EffectOperation
{
    EffectExecutionId execution{};
    std::uint32_t wave = 0;
    std::uint32_t step_index = 0;
    std::uint64_t local_sequence = 0;
    EffectTypeId type{};
    GameplayObjectRef target{};
    std::int64_t magnitude_micro = 0;
    RegisteredEffectPayload payload;
    GameplayContext context{};
};

struct EffectHandlerCapabilities
{
    bool abstract_capable = true;
    bool requires_materialized = false;
    bool requires_runtime_projection = false;
    bool thread_safe_prepare = false;
};

enum class EffectPrepareDisposition
{
    Accepted,
    Rejected,
    NoOp,
    Unavailable,
    InvalidTarget,
    Unsupported,
};

struct EffectPrepareResult
{
    EffectPrepareDisposition disposition = EffectPrepareDisposition::Accepted;
    RegisteredEffectPayload commit_token;
};

enum class EffectCommitDisposition
{
    Applied,
    NoOp,
};

struct EffectCommitResult
{
    EffectCommitDisposition disposition = EffectCommitDisposition::Applied;
    std::vector<EffectOperation> derived_effects;
};

class IEffectHandler
{
  public:
    virtual ~IEffectHandler() = default;
    [[nodiscard]] virtual EffectTypeId Type() const noexcept = 0;
    [[nodiscard]] virtual EffectHandlerCapabilities Capabilities() const noexcept = 0;
    [[nodiscard]] virtual foundation::Result<EffectPrepareResult> Prepare(const EffectOperation& operation) const = 0;
    [[nodiscard]] virtual foundation::Result<EffectCommitResult> Commit(
        const EffectOperation& operation,
        const RegisteredEffectPayload& commit_token) = 0;
};

struct EffectTargetState
{
    bool known = true;
    bool materialized = false;
    bool runtime_projection = false;
};

class IEffectTargetStateProvider
{
  public:
    virtual ~IEffectTargetStateProvider() = default;
    [[nodiscard]] virtual EffectTargetState Resolve(GameplayObjectRef target) const = 0;
};

struct EffectExecutionBudget
{
    std::uint64_t max_effects = 100000;
    std::uint32_t max_waves = 64;
    std::uint32_t max_derived_per_parent = 256;
};

enum class EffectOperationDisposition
{
    Applied,
    Rejected,
    NoOp,
    Unavailable,
    InvalidTarget,
    Unsupported,
    BudgetExceeded,
    Failed,
};

struct EffectOperationResult
{
    EffectOperation operation;
    EffectOperationDisposition disposition = EffectOperationDisposition::Failed;
};

enum class EffectBatchDisposition
{
    Succeeded,
    PartiallyApplied,
    Rejected,
    Failed,
};

struct EffectExecutionResult
{
    EffectExecutionId execution{};
    EffectBatchDisposition disposition = EffectBatchDisposition::Failed;
    std::vector<EffectOperationResult> operations;
    std::uint32_t waves = 0;
};

enum class EffectChangeKind
{
    ExecutionStarted,
    Applied,
    Rejected,
    Failed,
    ExecutionCompleted,
    BudgetExceeded,
    DeferredCreated,
    DeferredCancelled,
    DeferredExecuted,
};

struct EffectChange
{
    std::uint64_t sequence = 0;
    EffectChangeKind kind = EffectChangeKind::ExecutionStarted;
    EffectExecutionId execution{};
    EffectTypeId type{};
    GameplayObjectRef target{};
    DeferredEffectId deferred{};
    EffectOperationDisposition disposition = EffectOperationDisposition::Applied;
    GameplayContext context{};
    std::optional<ScheduleId> schedule{};
};

enum class DeferredEffectPersistence
{
    Session,
    Persistent,
};

struct DeferredEffectRecord
{
    DeferredEffectId id{};
    EffectRequest request;
    ClockId clock{};
    GameplayTimePoint due{};
    std::optional<ScheduleId> schedule{};
    DeferredEffectPersistence persistence = DeferredEffectPersistence::Session;
};

struct EffectsSnapshot
{
    std::vector<DeferredEffectRecord> deferred;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot execution_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot deferred_ids{};
};

struct EffectsDiagnostics
{
    std::uint64_t executions = 0;
    std::uint64_t operations = 0;
    std::uint64_t targets = 0;
    std::uint64_t derived_effects = 0;
    std::uint64_t waves = 0;
    std::uint64_t rejections = 0;
    std::uint64_t budget_exhaustions = 0;
    std::uint64_t deferred_effects = 0;
};

class EffectService
{
  public:
    using PayloadValidator = std::function<bool(std::span<const std::byte>)>;

    EffectService();

    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.effects");
    }

    [[nodiscard]] foundation::Result<EffectTypeId> RegisterHandler(
        std::string_view canonical_name,
        std::shared_ptr<IEffectHandler> handler,
        TypeId payload_type = {},
        std::size_t max_payload_bytes = 0,
        PayloadValidator validator = {});
    [[nodiscard]] foundation::Result<EffectDefinitionId> RegisterDefinition(EffectDefinition definition);
    void SetTargetStateProvider(const IEffectTargetStateProvider* provider) noexcept { target_state_provider_ = provider; }
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const EffectDefinition* FindDefinition(EffectDefinitionId id) const noexcept;

    [[nodiscard]] foundation::Result<EffectExecutionResult> Execute(
        EffectRequest request,
        EffectExecutionBudget budget = {},
        core::tasks::ITaskScheduler* scheduler = nullptr);

    [[nodiscard]] foundation::Result<DeferredEffectId> Defer(
        EffectRequest request,
        ClockId clock,
        GameplayTimePoint due,
        DeferredEffectPersistence persistence = DeferredEffectPersistence::Session);
    [[nodiscard]] foundation::Result<void> BindDeferredSchedule(DeferredEffectId id, ScheduleId schedule);
    [[nodiscard]] foundation::Result<void> CancelDeferred(DeferredEffectId id, GameplayContext context = {});
    [[nodiscard]] std::uint64_t CancelDeferredTargeting(GameplayObjectRef target, GameplayContext context = {});
    [[nodiscard]] const DeferredEffectRecord* FindDeferred(DeferredEffectId id) const noexcept;
    [[nodiscard]] std::vector<DeferredEffectRecord> UnscheduledDeferred() const;
    [[nodiscard]] foundation::Result<EffectRequest> TakeDeferredBySchedule(ScheduleId schedule, GameplayContext context = {});

    [[nodiscard]] std::vector<EffectChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_ - 1; }
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] EffectsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EffectsSnapshot snapshot);
    [[nodiscard]] EffectsDiagnostics GetDiagnostics() const noexcept;

  private:
    struct HandlerEntry
    {
        std::string canonical_name;
        std::shared_ptr<IEffectHandler> handler;
        TypeId payload_type{};
        std::size_t max_payload_bytes = 0;
        PayloadValidator validator;
    };

    struct PreparedOperation
    {
        EffectOperation operation;
        EffectPrepareResult prepared;
        EffectOperationDisposition preflight_disposition = EffectOperationDisposition::Failed;
        bool commit = false;
    };

    [[nodiscard]] foundation::Result<void> ValidatePayload(const HandlerEntry& entry, const RegisteredEffectPayload& payload) const;
    [[nodiscard]] foundation::Result<std::vector<EffectOperation>> ExpandRequest(
        const EffectRequest& request,
        EffectExecutionId execution) const;
    [[nodiscard]] EffectOperationDisposition ValidateCapabilities(const HandlerEntry& entry, GameplayObjectRef target) const;
    [[nodiscard]] foundation::Result<std::vector<PreparedOperation>> PrepareWave(
        std::vector<EffectOperation> operations,
        EffectExecutionPolicy policy,
        core::tasks::ITaskScheduler* scheduler);
    [[nodiscard]] EffectOperationDisposition MapPrepareDisposition(EffectPrepareDisposition disposition) const noexcept;
    void RecordChange(EffectChange change);

    std::unordered_map<EffectTypeId, HandlerEntry, EffectTypeIdHash> handlers_;
    std::unordered_map<EffectDefinitionId, EffectDefinition, EffectDefinitionIdHash> definitions_;
    const IEffectTargetStateProvider* target_state_provider_ = nullptr;
    bool frozen_ = false;

    MonotonicIdGenerator<GameplayObjectId> execution_ids_;
    MonotonicIdGenerator<GameplayObjectId> deferred_ids_;
    std::unordered_map<DeferredEffectId, DeferredEffectRecord, DeferredEffectIdHash> deferred_;

    std::vector<EffectChange> changes_;
    std::uint64_t next_change_sequence_ = 1;

    std::uint64_t executions_ = 0;
    std::uint64_t operations_ = 0;
    std::uint64_t targets_ = 0;
    std::uint64_t derived_effects_ = 0;
    std::uint64_t waves_ = 0;
    std::uint64_t rejections_ = 0;
    std::uint64_t budget_exhaustions_ = 0;
};
} // namespace epidemic::gameplay::effects

