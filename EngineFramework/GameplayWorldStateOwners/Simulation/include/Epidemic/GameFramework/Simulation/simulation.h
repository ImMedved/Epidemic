#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::simulation
{
using Fixed = std::int64_t;
struct SimulationRegionId
{
    TypeId value{};
    static constexpr SimulationRegionId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SimulationRegionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SimulationRegionId &) const noexcept = default;
};
struct SimulationLayerId
{
    TypeId value{};
    static constexpr SimulationLayerId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SimulationLayerId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SimulationLayerId &) const noexcept = default;
};
struct SimulationTaskId
{
    GameplayObjectId value{};
    static constexpr SimulationTaskId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SimulationTaskId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SimulationTaskId &) const noexcept = default;
};
struct SimulationPolicyId
{
    TypeId value{};
    static constexpr SimulationPolicyId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SimulationPolicyId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SimulationPolicyId &) const noexcept = default;
};
struct SimulationSummaryId
{
    GameplayObjectId value{};
    static constexpr SimulationSummaryId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SimulationSummaryId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SimulationSummaryId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class SimulationDetailLevel
{
    Disabled,
    Dormant,
    Abstract,
    Coarse,
    Detailed,
    Materialized
};
enum class SimulationTaskState
{
    Pending,
    Running,
    Completed,
    Failed,
    Skipped,
    Deferred
};
enum class SimulationIntervalState
{
    Pending,
    Completed
};
enum class SimulationMaterializationPolicy
{
    AbstractCapable,
    RequiresDetailed,
    RequiresMaterialized
};
enum class SimulationChangeKind
{
    RegionRegistered,
    LayerRegistered,
    IntervalStarted,
    TaskPrepared,
    TaskCommitted,
    TaskDeferred,
    TaskFailed,
    BudgetExceeded,
    SummaryGenerated
};
struct SimulationBudget
{
    // Work units processed by one SimulateInterval call. A work unit is one layer task examined
    // for prepare/commit. Exhaustion pauses the durable interval execution and never advances the
    // region cursor.
    std::uint32_t max_tasks = 1024;
    std::uint32_t max_layer_commits = 1024;
    std::int64_t max_interval_ticks = 86'400;
};
struct SimulationRetentionPolicy
{
    std::size_t max_completed_summaries = 1024;
    std::size_t max_changes = 4096;
};
struct SimulationRegion
{
    SimulationRegionId id{};
    std::string canonical_name;
    GameplayObjectRef area{};
    SimulationDetailLevel detail_level = SimulationDetailLevel::Abstract;
    SimulationPolicyId policy{};
    GameplayTimePoint last_simulated_at{};
    Revision revision{};
};
struct SimulationLayerDefinition
{
    SimulationLayerId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    std::int32_t order = 0;
    SimulationMaterializationPolicy materialization_policy = SimulationMaterializationPolicy::AbstractCapable;
    Revision revision{};
};
struct SimulationTask
{
    SimulationTaskId id{};
    SimulationRegionId region{};
    SimulationLayerId layer{};
    GameplayTimePoint from{};
    GameplayTimePoint to{};
    SimulationDetailLevel detail = SimulationDetailLevel::Abstract;
    SimulationTaskState state = SimulationTaskState::Pending;
    Revision revision{};
    GameplayObjectRef area{};
};
struct SimulationPreparedOperation
{
    GameplayObjectId operation{};
    Revision expected_revision{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return operation.IsValid(); }
};
struct SimulationLayerSummary
{
    SimulationLayerId layer{};
    SimulationTaskState state = SimulationTaskState::Completed;
    std::uint64_t operations = 0;
    Revision revision{};
    std::vector<SimulationPreparedOperation> prepared_operations;
};
struct SimulationLayerExecution
{
    SimulationTask task{};
    SimulationLayerSummary prepared_summary{};
    bool prepared = false;
};
struct SimulationIntervalExecution
{
    // The eventual summary id is allocated when the interval starts and serves as a stable
    // interval operation identity for save/load and retry.
    SimulationSummaryId id{};
    SimulationRegionId region{};
    GameplayTimePoint from{};
    GameplayTimePoint to{};
    SimulationDetailLevel detail = SimulationDetailLevel::Abstract;
    SimulationIntervalState state = SimulationIntervalState::Pending;
    std::vector<SimulationLayerExecution> layers;
    Revision revision{};
};
struct SimulationSummary
{
    SimulationSummaryId id{};
    SimulationRegionId region{};
    GameplayTimePoint from{};
    GameplayTimePoint to{};
    std::vector<SimulationLayerSummary> layers;
    Revision revision{};
};
struct SimulationChange
{
    std::uint64_t sequence = 0;
    SimulationChangeKind kind = SimulationChangeKind::TaskPrepared;
    SimulationRegionId region{};
    SimulationLayerId layer{};
    SimulationTaskId task{};
    GameplayTimePoint time{};
    GameplayContext context{};
    Revision revision{};
};
struct SimulationChangeBatch
{
    std::vector<SimulationChange> changes;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    bool snapshot_required = false;
};
struct SimulationSnapshot
{
    std::vector<SimulationRegion> regions;
    std::vector<SimulationIntervalExecution> active_intervals;
    std::vector<SimulationSummary> summaries;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot task_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot summary_ids{};
    Revision revision{};
};
struct SimulationDiagnostics
{
    std::uint64_t regions = 0;
    std::uint64_t layers = 0;
    std::uint64_t active_intervals = 0;
    std::uint64_t tasks_prepared = 0;
    std::uint64_t tasks_committed = 0;
    std::uint64_t tasks_deferred = 0;
    std::uint64_t budget_exhaustions = 0;
    std::uint64_t summaries = 0;
};

class ISimulationLayerExecutor
{
  public:
    virtual ~ISimulationLayerExecutor() = default;
    [[nodiscard]] virtual SimulationLayerId Layer() const noexcept = 0;

    // Prepare must not commit gameplay state. Commit may be called again with the same stable
    // SimulationTaskId after a recoverable failure or save/load boundary, therefore every
    // executor must make Commit idempotent for a task id and the prepared summary it produced.
    [[nodiscard]] virtual foundation::Result<SimulationLayerSummary> Prepare(const SimulationTask &task) = 0;
    [[nodiscard]] virtual foundation::Result<void> Commit(const SimulationTask &task,
                                                          const SimulationLayerSummary &summary) = 0;
};

class SimulationService
{
  public:
    SimulationService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.simulation");
    }

    // Regions are runtime world state and may be created after definitions are frozen.
    [[nodiscard]] foundation::Result<SimulationRegionId> RegisterRegion(SimulationRegion region);
    // The executor is a non-owning bootstrap dependency and must outlive the service's use of
    // the frozen layer definition. Executor exceptions are contained at the Simulation boundary.
    [[nodiscard]] foundation::Result<SimulationLayerId> RegisterLayer(SimulationLayerDefinition layer,
                                                                      ISimulationLayerExecutor *executor);
    void FreezeDefinitions() noexcept
    {
        definitions_frozen_ = true;
    }
    void Freeze() noexcept
    {
        FreezeDefinitions();
    }
    [[nodiscard]] bool DefinitionsFrozen() const noexcept
    {
        return definitions_frozen_;
    }

    [[nodiscard]] const SimulationRegion *FindRegion(SimulationRegionId id) const noexcept;
    [[nodiscard]] const SimulationLayerDefinition *FindLayer(SimulationLayerId id) const noexcept;
    [[nodiscard]] const SimulationIntervalExecution *FindActiveInterval(SimulationRegionId region) const noexcept;
    void SetBudget(SimulationBudget budget) noexcept
    {
        budget_ = budget;
    }
    void SetRetentionPolicy(SimulationRetentionPolicy policy) noexcept;

    // Mutating intervals form a strict chain per region. A new interval requires
    // from == region.last_simulated_at and to > from. If a call stops on a budget or layer
    // failure, repeating the exact same interval resumes its persisted execution. Already
    // committed layer tasks are never committed again by SimulationService.
    [[nodiscard]] foundation::Result<SimulationSummaryId> SimulateInterval(SimulationRegionId region,
                                                                           GameplayTimePoint from, GameplayTimePoint to,
                                                                           GameplayContext context = {});
    [[nodiscard]] std::vector<SimulationSummary> FindSummaries(SimulationRegionId region) const;
    [[nodiscard]] SimulationChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::vector<SimulationChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] SimulationSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(SimulationSnapshot snapshot);
    [[nodiscard]] SimulationDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(SimulationChange change);
    void TrimRetention() noexcept;
    [[nodiscard]] bool DetailAllows(const SimulationLayerDefinition &layer,
                                    SimulationDetailLevel detail) const noexcept;
    [[nodiscard]] foundation::Result<void> CreateIntervalExecution(SimulationRegion &region, GameplayTimePoint from,
                                                                   GameplayTimePoint to, GameplayContext context);
    [[nodiscard]] foundation::Result<SimulationSummaryId> ContinueIntervalExecution(SimulationRegion &region,
                                                                                    GameplayContext context);

    bool definitions_frozen_ = false;
    Revision revision_{};
    std::unordered_map<SimulationRegionId, SimulationRegion, IdHash> regions_;
    std::unordered_map<SimulationLayerId, SimulationLayerDefinition, IdHash> layers_;
    std::unordered_map<SimulationLayerId, ISimulationLayerExecutor *, IdHash> executors_;
    std::unordered_map<SimulationRegionId, SimulationIntervalExecution, IdHash> active_intervals_;
    std::deque<SimulationSummary> summaries_;
    MonotonicIdGenerator<GameplayObjectId> task_ids_;
    MonotonicIdGenerator<GameplayObjectId> summary_ids_;
    SimulationBudget budget_{};
    SimulationRetentionPolicy retention_{};
    std::deque<SimulationChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    SimulationDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::simulation
