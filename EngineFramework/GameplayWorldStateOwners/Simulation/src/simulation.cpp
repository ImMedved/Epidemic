#include "Epidemic/GameFramework/Simulation/simulation.h"
#include "simulation_test_seam.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::simulation
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}


[[nodiscard]] bool IsValidSimulationDetailLevel(SimulationDetailLevel detail) noexcept
{
    switch (detail)
    {
    case SimulationDetailLevel::Disabled:
    case SimulationDetailLevel::Dormant:
    case SimulationDetailLevel::Abstract:
    case SimulationDetailLevel::Coarse:
    case SimulationDetailLevel::Detailed:
    case SimulationDetailLevel::Materialized:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidSimulationTaskState(SimulationTaskState state) noexcept
{
    switch (state)
    {
    case SimulationTaskState::Pending:
    case SimulationTaskState::Running:
    case SimulationTaskState::Completed:
    case SimulationTaskState::Failed:
    case SimulationTaskState::Skipped:
    case SimulationTaskState::Deferred:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidSimulationIntervalState(SimulationIntervalState state) noexcept
{
    switch (state)
    {
    case SimulationIntervalState::Pending:
    case SimulationIntervalState::Completed:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValidSimulationMaterializationPolicy(SimulationMaterializationPolicy policy) noexcept
{
    switch (policy)
    {
    case SimulationMaterializationPolicy::AbstractCapable:
    case SimulationMaterializationPolicy::RequiresDetailed:
    case SimulationMaterializationPolicy::RequiresMaterialized:
        return true;
    }
    return false;
}

[[nodiscard]] bool SameInterval(const SimulationSummary &summary, SimulationRegionId region, GameplayTimePoint from,
                                GameplayTimePoint to) noexcept
{
    return summary.region == region && summary.from == from && summary.to == to;
}

[[nodiscard]] bool RevisionWithin(Revision record, Revision snapshot) noexcept
{
    return record.value <= snapshot.value;
}

[[nodiscard]] std::uint64_t MaxTaskLow(const std::vector<SimulationIntervalExecution> &intervals) noexcept
{
    std::uint64_t value = 0;
    for (const auto &interval : intervals)
        for (const auto &layer : interval.layers)
            value = std::max(value, layer.task.id.value.Low());
    return value;
}

[[nodiscard]] std::uint64_t MaxSummaryLow(const std::vector<SimulationIntervalExecution> &intervals,
                                          const std::vector<SimulationSummary> &summaries) noexcept
{
    std::uint64_t value = 0;
    for (const auto &interval : intervals)
        value = std::max(value, interval.id.value.Low());
    for (const auto &summary : summaries)
        value = std::max(value, summary.id.value.Low());
    return value;
}


foundation::Result<std::uint64_t> StageJournalChange(std::uint64_t next_sequence, SimulationChange change,
                                                     std::list<SimulationChange> &staged)
{
    if (next_sequence == 0)
        return foundation::Result<std::uint64_t>::Failure(
            Error("gameplay.simulation.journal_exhausted", "simulation change journal sequence exhausted"));
    if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::JournalPublication))
        return foundation::Result<std::uint64_t>::Failure(
            Error("gameplay.simulation.allocation_failed", "simulation journal staging failed"));
    change.sequence = next_sequence;
    try
    {
        staged.push_back(std::move(change));
    }
    catch (...)
    {
        return foundation::Result<std::uint64_t>::Failure(
            Error("gameplay.simulation.allocation_failed", "simulation journal staging failed"));
    }
    return foundation::Result<std::uint64_t>::Success(
        next_sequence == std::numeric_limits<std::uint64_t>::max() ? 0 : next_sequence + 1);
}

void CommitJournalChange(std::list<SimulationChange> &live, std::uint64_t &next_sequence,
                         std::list<SimulationChange> &staged, std::uint64_t staged_next,
                         std::size_t max_changes) noexcept
{
    live.splice(live.end(), staged);
    next_sequence = staged_next;
    if (max_changes == 0)
        live.clear();
    else
        while (live.size() > max_changes)
            live.pop_front();
}
} // namespace

SimulationService::SimulationService() : task_ids_(0x30322001), summary_ids_(0x30322002)
{
}

foundation::Result<SimulationRegionId> SimulationService::RegisterRegion(SimulationRegion region)
{
    if (region.canonical_name.empty() || !IsValidSimulationDetailLevel(region.detail_level))
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.invalid_region", "region name required"));
    const auto canonical = SimulationRegionId::FromString(region.canonical_name);
    if (!region.id.IsValid())
        region.id = canonical;
    if (region.id != canonical || regions_.contains(region.id))
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.invalid_region", "invalid or duplicate region"));
    if (revision_.value == std::numeric_limits<std::uint64_t>::max())
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.revision_exhausted", "simulation revision exhausted"));

    const Revision next_revision{revision_.value + 1};
    region.revision = next_revision;
    const auto id = region.id;
    std::unordered_map<SimulationRegionId, SimulationRegion, IdHash> staged_regions;
    std::list<SimulationChange> staged_change;
    try
    {
        staged_regions = regions_;
        if (!staged_regions.emplace(id, std::move(region)).second)
            return foundation::Result<SimulationRegionId>::Failure(
                Error("gameplay.simulation.invalid_region", "invalid or duplicate region"));
    }
    catch (...)
    {
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.allocation_failed", "failed to stage simulation region"));
    }
    auto staged_next = StageJournalChange(next_change_sequence_,
                                          {0, SimulationChangeKind::RegionRegistered, id, {}, {}, {}, {}, next_revision},
                                          staged_change);
    if (!staged_next)
        return foundation::Result<SimulationRegionId>::Failure(staged_next.GetError());

    regions_.swap(staged_regions);
    revision_ = next_revision;
    CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
    return foundation::Result<SimulationRegionId>::Success(id);
}

foundation::Result<SimulationLayerId> SimulationService::RegisterLayer(SimulationLayerDefinition layer,
                                                                       ISimulationLayerExecutor *executor)
{
    if (definitions_frozen_)
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.registry_frozen", "simulation layer definitions frozen"));
    if (layer.canonical_name.empty() || executor == nullptr ||
        !IsValidSimulationMaterializationPolicy(layer.materialization_policy))
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.invalid_layer", "layer name and executor required"));
    const auto canonical = SimulationLayerId::FromString(layer.canonical_name);
    if (!layer.id.IsValid())
        layer.id = canonical;
    if (layer.id != canonical || layers_.contains(layer.id) || executor->Layer() != layer.id)
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.invalid_layer", "invalid or duplicate layer"));
    if (revision_.value == std::numeric_limits<std::uint64_t>::max())
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.revision_exhausted", "simulation revision exhausted"));

    const Revision next_revision{revision_.value + 1};
    layer.revision = next_revision;
    const auto id = layer.id;
    decltype(layers_) staged_layers;
    decltype(executors_) staged_executors;
    std::list<SimulationChange> staged_change;
    try
    {
        staged_layers = layers_;
        staged_executors = executors_;
        if (!staged_layers.emplace(id, std::move(layer)).second || !staged_executors.emplace(id, executor).second)
            return foundation::Result<SimulationLayerId>::Failure(
                Error("gameplay.simulation.invalid_layer", "invalid or duplicate layer"));
    }
    catch (...)
    {
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.allocation_failed", "failed to stage simulation layer"));
    }
    auto staged_next = StageJournalChange(next_change_sequence_,
                                          {0, SimulationChangeKind::LayerRegistered, {}, id, {}, {}, {}, next_revision},
                                          staged_change);
    if (!staged_next)
        return foundation::Result<SimulationLayerId>::Failure(staged_next.GetError());

    layers_.swap(staged_layers);
    executors_.swap(staged_executors);
    revision_ = next_revision;
    CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
    return foundation::Result<SimulationLayerId>::Success(id);
}

const SimulationRegion *SimulationService::FindRegion(SimulationRegionId id) const noexcept
{
    const auto it = regions_.find(id);
    return it == regions_.end() ? nullptr : &it->second;
}

const SimulationLayerDefinition *SimulationService::FindLayer(SimulationLayerId id) const noexcept
{
    const auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : &it->second;
}

const SimulationIntervalExecution *SimulationService::FindActiveInterval(SimulationRegionId region) const noexcept
{
    const auto it = active_intervals_.find(region);
    return it == active_intervals_.end() ? nullptr : &it->second;
}

void SimulationService::SetRetentionPolicy(SimulationRetentionPolicy policy) noexcept
{
    retention_ = policy;
    TrimRetention();
}

bool SimulationService::DetailAllows(const SimulationLayerDefinition &layer, SimulationDetailLevel detail) const noexcept
{
    if (!IsValidSimulationDetailLevel(detail) || !IsValidSimulationMaterializationPolicy(layer.materialization_policy) ||
        detail == SimulationDetailLevel::Disabled || detail == SimulationDetailLevel::Dormant)
        return false;
    if (layer.materialization_policy == SimulationMaterializationPolicy::RequiresMaterialized)
        return detail == SimulationDetailLevel::Materialized;
    if (layer.materialization_policy == SimulationMaterializationPolicy::RequiresDetailed)
        return detail == SimulationDetailLevel::Detailed || detail == SimulationDetailLevel::Materialized;
    return true;
}

foundation::Result<void> SimulationService::CreateIntervalExecution(SimulationRegion &region, GameplayTimePoint from,
                                                                    GameplayTimePoint to, GameplayContext context)
{
    auto staged_summary_ids = summary_ids_;
    auto staged_task_ids = task_ids_;
    SimulationIntervalExecution execution;
    try
    {
        execution.id = SimulationSummaryId{staged_summary_ids.Next()};
        if (!execution.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.id_exhausted", "simulation summary id generator exhausted"));
        execution.region = region.id;
        execution.from = from;
        execution.to = to;
        execution.detail = region.detail_level;
        execution.state = SimulationIntervalState::Pending;

        std::vector<SimulationLayerDefinition> ordered;
        ordered.reserve(layers_.size());
        for (const auto &[id, layer] : layers_)
        {
            (void)id;
            ordered.push_back(layer);
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto &a, const auto &b) { return a.order == b.order ? a.id < b.id : a.order < b.order; });
        execution.layers.reserve(ordered.size());
        for (const auto &layer : ordered)
        {
            SimulationLayerExecution layer_execution;
            layer_execution.task.id = SimulationTaskId{staged_task_ids.Next()};
            if (!layer_execution.task.id.IsValid())
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.id_exhausted", "simulation task id generator exhausted"));
            layer_execution.task.region = region.id;
            layer_execution.task.layer = layer.id;
            layer_execution.task.from = from;
            layer_execution.task.to = to;
            layer_execution.task.detail = execution.detail;
            layer_execution.task.state = SimulationTaskState::Pending;
            layer_execution.task.area = region.area;
            execution.layers.push_back(std::move(layer_execution));
        }
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.allocation_failed", "failed to stage simulation interval"));
    }

    if (revision_.value == std::numeric_limits<std::uint64_t>::max())
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.revision_exhausted", "simulation revision exhausted"));
    const Revision next_revision{revision_.value + 1};
    execution.revision = next_revision;
    std::list<SimulationChange> staged_change;
    auto staged_next = StageJournalChange(next_change_sequence_,
                                          {0, SimulationChangeKind::IntervalStarted, region.id, {}, {}, context.time,
                                           context, next_revision},
                                          staged_change);
    if (!staged_next)
        return foundation::Result<void>::Failure(staged_next.GetError());

    decltype(active_intervals_) staging_map;
    decltype(active_intervals_)::node_type staged_node;
    try
    {
        active_intervals_.reserve(active_intervals_.size() + 1);
        staging_map.emplace(region.id, std::move(execution));
        staged_node = staging_map.extract(region.id);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.allocation_failed", "failed to stage active simulation interval"));
    }
    if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::IntervalPublication))
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.allocation_failed", "active interval publication failed"));

    const auto inserted = active_intervals_.insert(std::move(staged_node));
    if (!inserted.inserted)
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.interval_exists", "active simulation interval already exists"));
    task_ids_ = staged_task_ids;
    summary_ids_ = staged_summary_ids;
    revision_ = next_revision;
    CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
    return foundation::Result<void>::Success();
}

foundation::Result<SimulationSummaryId> SimulationService::ContinueIntervalExecution(SimulationRegion &region,
                                                                                     GameplayContext context)
{
    auto execution_it = active_intervals_.find(region.id);
    if (execution_it == active_intervals_.end())
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.interval_missing", "active simulation interval missing"));
    auto &execution = execution_it->second;

    auto stage_revision_change = [&](SimulationChangeKind kind, const SimulationTask &task, Revision next_revision,
                                     std::list<SimulationChange> &staged_change)
        -> foundation::Result<std::uint64_t> {
        return StageJournalChange(next_change_sequence_,
                                  {0, kind, region.id, task.layer, task.id, context.time, context, next_revision},
                                  staged_change);
    };
    auto require_next_revision = [&]() -> foundation::Result<Revision> {
        if (revision_.value == std::numeric_limits<std::uint64_t>::max())
            return foundation::Result<Revision>::Failure(
                Error("gameplay.simulation.revision_exhausted", "simulation revision exhausted"));
        return foundation::Result<Revision>::Success(Revision{revision_.value + 1});
    };

    std::uint32_t work = 0;
    std::uint32_t commits = 0;
    bool budget_stopped = false;

    for (auto &layer_execution : execution.layers)
    {
        auto &task = layer_execution.task;
        if (task.state == SimulationTaskState::Completed || task.state == SimulationTaskState::Skipped)
            continue;

        if (work >= budget_.max_tasks)
        {
            if (task.state != SimulationTaskState::Deferred)
            {
                auto next = require_next_revision();
                if (!next)
                    return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
                std::list<SimulationChange> staged_change;
                auto staged_next = stage_revision_change(SimulationChangeKind::TaskDeferred, task, next.Value(), staged_change);
                if (!staged_next)
                    return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
                task.state = SimulationTaskState::Deferred;
                task.revision = next.Value();
                revision_ = next.Value();
                ++diagnostics_.tasks_deferred;
                CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
            }
            budget_stopped = true;
            break;
        }
        ++work;

        const auto layer_it = layers_.find(task.layer);
        const auto executor_it = executors_.find(task.layer);
        if (layer_it == layers_.end() || executor_it == executors_.end())
            return foundation::Result<SimulationSummaryId>::Failure(
                Error("gameplay.simulation.layer_missing", "simulation layer or executor missing"));

        if (!DetailAllows(layer_it->second, task.detail))
        {
            auto next = require_next_revision();
            if (!next)
                return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
            task.state = SimulationTaskState::Skipped;
            task.revision = next.Value();
            layer_execution.prepared = true;
            layer_execution.prepared_summary = {task.layer, SimulationTaskState::Skipped, 0, next.Value(), {}};
            revision_ = next.Value();
            continue;
        }

        if (!layer_execution.prepared)
        {
            auto prepare_task = task;
            prepare_task.state = SimulationTaskState::Running;
            foundation::Result<SimulationLayerSummary> prepared =
                foundation::Result<SimulationLayerSummary>::Failure(
                    Error("gameplay.simulation.prepare_failed", "simulation layer prepare failed"));
            try
            {
                prepared = executor_it->second->Prepare(prepare_task);
            }
            catch (const std::exception &)
            {
                prepared = foundation::Result<SimulationLayerSummary>::Failure(
                    Error("gameplay.simulation.executor_exception", "simulation layer prepare threw an exception"));
            }
            catch (...)
            {
                prepared = foundation::Result<SimulationLayerSummary>::Failure(
                    Error("gameplay.simulation.executor_exception", "simulation layer prepare threw an exception"));
            }

            if (!prepared)
            {
                auto next = require_next_revision();
                if (!next)
                    return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
                std::list<SimulationChange> staged_change;
                auto staged_next = stage_revision_change(SimulationChangeKind::TaskFailed, task, next.Value(), staged_change);
                if (!staged_next)
                    return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
                task.state = SimulationTaskState::Failed;
                task.revision = next.Value();
                revision_ = next.Value();
                CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
                return foundation::Result<SimulationSummaryId>::Failure(prepared.GetError());
            }

            auto layer_summary = std::move(prepared).Value();
            if (layer_summary.layer != task.layer)
            {
                auto next = require_next_revision();
                if (!next)
                    return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
                std::list<SimulationChange> staged_change;
                auto staged_next = stage_revision_change(SimulationChangeKind::TaskFailed, task, next.Value(), staged_change);
                if (!staged_next)
                    return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
                task.state = SimulationTaskState::Failed;
                task.revision = next.Value();
                revision_ = next.Value();
                CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.invalid_prepare", "executor returned summary for a different layer"));
            }

            auto next = require_next_revision();
            if (!next)
                return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
            layer_summary.state = SimulationTaskState::Running;
            layer_summary.revision = next.Value();
            std::list<SimulationChange> staged_change;
            auto staged_next = stage_revision_change(SimulationChangeKind::TaskPrepared, task, next.Value(), staged_change);
            if (!staged_next)
                return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
            task.state = SimulationTaskState::Running;
            task.revision = next.Value();
            layer_execution.prepared_summary = std::move(layer_summary);
            layer_execution.prepared = true;
            revision_ = next.Value();
            ++diagnostics_.tasks_prepared;
            CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
        }

        if (commits >= budget_.max_layer_commits)
        {
            if (task.state != SimulationTaskState::Deferred)
            {
                auto next = require_next_revision();
                if (!next)
                    return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
                std::list<SimulationChange> staged_change;
                auto staged_next = stage_revision_change(SimulationChangeKind::TaskDeferred, task, next.Value(), staged_change);
                if (!staged_next)
                    return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
                task.state = SimulationTaskState::Deferred;
                task.revision = next.Value();
                revision_ = next.Value();
                ++diagnostics_.tasks_deferred;
                CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
            }
            budget_stopped = true;
            break;
        }

        // G4-SIM-001: prove terminal local publication before invoking accepted external work.
        // If this is the last unfinished layer, terminal publication includes both TaskCommitted and
        // the interval summary. Stage both revisions, both journal records and the summary node before
        // the executor can accept work so revision/journal exhaustion or allocation cannot strand a
        // fully committed interval in a permanently unfinalizable state.
        const bool completes_interval = std::all_of(
            execution.layers.begin(), execution.layers.end(), [&](const SimulationLayerExecution &candidate) {
                if (&candidate == &layer_execution)
                    return true;
                return candidate.task.state == SimulationTaskState::Completed ||
                       candidate.task.state == SimulationTaskState::Skipped;
            });

        auto next = require_next_revision();
        if (!next)
            return foundation::Result<SimulationSummaryId>::Failure(next.GetError());
        std::list<SimulationChange> committed_change;
        auto committed_next = stage_revision_change(SimulationChangeKind::TaskCommitted, task, next.Value(), committed_change);
        if (!committed_next)
            return foundation::Result<SimulationSummaryId>::Failure(committed_next.GetError());

        Revision terminal_revision{};
        std::uint64_t terminal_next_sequence = committed_next.Value();
        std::list<SimulationSummary> terminal_summary;
        if (completes_interval)
        {
            if (next.Value().value == std::numeric_limits<std::uint64_t>::max())
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.revision_exhausted", "simulation revision exhausted"));
            terminal_revision = Revision{next.Value().value + 1};

            SimulationSummary summary;
            try
            {
                summary.id = execution.id;
                summary.region = execution.region;
                summary.from = execution.from;
                summary.to = execution.to;
                summary.layers.reserve(execution.layers.size());
                for (const auto &candidate : execution.layers)
                {
                    auto layer_summary = candidate.prepared_summary;
                    if (&candidate == &layer_execution)
                    {
                        layer_summary.state = SimulationTaskState::Completed;
                        layer_summary.revision = next.Value();
                    }
                    summary.layers.push_back(std::move(layer_summary));
                }
                summary.revision = terminal_revision;
            }
            catch (...)
            {
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.allocation_failed", "failed to stage terminal simulation summary"));
            }

            auto final_next = StageJournalChange(
                committed_next.Value(),
                {0, SimulationChangeKind::SummaryGenerated, region.id, {}, {}, context.time, context, terminal_revision},
                committed_change);
            if (!final_next)
                return foundation::Result<SimulationSummaryId>::Failure(final_next.GetError());
            terminal_next_sequence = final_next.Value();
            if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::SummaryPublication))
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.allocation_failed", "simulation summary publication failed"));
            try
            {
                terminal_summary.push_back(std::move(summary));
            }
            catch (...)
            {
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.allocation_failed", "simulation summary publication failed"));
            }
        }

        auto commit_task = task;
        commit_task.state = SimulationTaskState::Running;
        foundation::Result<void> committed = foundation::Result<void>::Failure(
            Error("gameplay.simulation.commit_failed", "simulation layer commit failed"));
        try
        {
            committed = executor_it->second->Commit(commit_task, layer_execution.prepared_summary);
        }
        catch (const std::exception &)
        {
            committed = foundation::Result<void>::Failure(
                Error("gameplay.simulation.executor_exception", "simulation layer commit threw an exception"));
        }
        catch (...)
        {
            committed = foundation::Result<void>::Failure(
                Error("gameplay.simulation.executor_exception", "simulation layer commit threw an exception"));
        }

        if (!committed)
        {
            std::list<SimulationChange> failed_change;
            auto failed_next = stage_revision_change(SimulationChangeKind::TaskFailed, task, next.Value(), failed_change);
            if (!failed_next)
                return foundation::Result<SimulationSummaryId>::Failure(failed_next.GetError());
            task.state = SimulationTaskState::Failed;
            task.revision = next.Value();
            revision_ = next.Value();
            CommitJournalChange(changes_, next_change_sequence_, failed_change, failed_next.Value(), retention_.max_changes);
            return foundation::Result<SimulationSummaryId>::Failure(committed.GetError());
        }

        // No potentially throwing operation follows an accepted external commit.
        ++commits;
        ++diagnostics_.tasks_committed;
        task.state = SimulationTaskState::Completed;
        task.revision = next.Value();
        layer_execution.prepared_summary.state = SimulationTaskState::Completed;
        layer_execution.prepared_summary.revision = next.Value();

        if (completes_interval)
        {
            const auto summary_id = execution.id;
            execution.state = SimulationIntervalState::Completed;
            execution.revision = terminal_revision;
            region.last_simulated_at = execution.to;
            region.revision = terminal_revision;
            summaries_.splice(summaries_.end(), terminal_summary);
            revision_ = terminal_revision;
            ++diagnostics_.summaries;
            CommitJournalChange(changes_, next_change_sequence_, committed_change, terminal_next_sequence,
                                retention_.max_changes);
            active_intervals_.erase(execution_it);
            TrimRetention();
            return foundation::Result<SimulationSummaryId>::Success(summary_id);
        }

        revision_ = next.Value();
        CommitJournalChange(changes_, next_change_sequence_, committed_change, committed_next.Value(), retention_.max_changes);
    }

    bool complete = true;
    for (const auto &layer_execution : execution.layers)
    {
        if (layer_execution.task.state != SimulationTaskState::Completed &&
            layer_execution.task.state != SimulationTaskState::Skipped)
        {
            complete = false;
            break;
        }
    }

    if (!complete)
    {
        if (budget_stopped)
        {
            std::list<SimulationChange> staged_change;
            auto staged_next = StageJournalChange(next_change_sequence_,
                                                  {0, SimulationChangeKind::BudgetExceeded, region.id, {}, {},
                                                   context.time, context, revision_},
                                                  staged_change);
            if (!staged_next)
                return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
            ++diagnostics_.budget_exhaustions;
            CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
        }
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.interval_pending", "simulation interval has pending layer work"));
    }

    SimulationSummary summary;
    try
    {
        summary.id = execution.id;
        summary.region = execution.region;
        summary.from = execution.from;
        summary.to = execution.to;
        summary.layers.reserve(execution.layers.size());
        for (const auto &layer_execution : execution.layers)
            summary.layers.push_back(layer_execution.prepared_summary);
    }
    catch (...)
    {
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.allocation_failed", "failed to stage simulation summary"));
    }

    auto final_revision = require_next_revision();
    if (!final_revision)
        return foundation::Result<SimulationSummaryId>::Failure(final_revision.GetError());
    summary.revision = final_revision.Value();
    const auto summary_id = summary.id;
    std::list<SimulationChange> staged_change;
    auto staged_next = StageJournalChange(next_change_sequence_,
                                          {0, SimulationChangeKind::SummaryGenerated, region.id, {}, {}, context.time,
                                           context, final_revision.Value()},
                                          staged_change);
    if (!staged_next)
        return foundation::Result<SimulationSummaryId>::Failure(staged_next.GetError());
    if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::SummaryPublication))
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.allocation_failed", "simulation summary publication failed"));
    std::list<SimulationSummary> staged_summary;
    try
    {
        staged_summary.push_back(std::move(summary));
    }
    catch (...)
    {
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.allocation_failed", "simulation summary publication failed"));
    }

    const auto completed_at = execution.to;
    execution.state = SimulationIntervalState::Completed;
    execution.revision = final_revision.Value();
    region.last_simulated_at = completed_at;
    region.revision = final_revision.Value();
    summaries_.splice(summaries_.end(), staged_summary);
    active_intervals_.erase(execution_it);
    revision_ = final_revision.Value();
    ++diagnostics_.summaries;
    CommitJournalChange(changes_, next_change_sequence_, staged_change, staged_next.Value(), retention_.max_changes);
    TrimRetention();
    return foundation::Result<SimulationSummaryId>::Success(summary_id);
}

foundation::Result<SimulationSummaryId> SimulationService::SimulateInterval(SimulationRegionId region_id,
                                                                            GameplayTimePoint from,
                                                                            GameplayTimePoint to,
                                                                            GameplayContext context)
{
    auto region_it = regions_.find(region_id);
    if (region_it == regions_.end())
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.region_missing", "simulation region missing"));
    if (to.ticks <= from.ticks)
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.invalid_interval", "simulation interval must move forward"));

    const auto duration = SaturatingDifference(to, from);
    if (budget_.max_interval_ticks > 0 && duration.ticks > budget_.max_interval_ticks)
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.interval_budget", "simulation interval exceeds budget"));

    for (const auto &summary : summaries_)
        if (SameInterval(summary, region_id, from, to))
            return foundation::Result<SimulationSummaryId>::Success(summary.id);

    if (auto active = active_intervals_.find(region_id); active != active_intervals_.end())
    {
        if (active->second.from != from || active->second.to != to)
            return foundation::Result<SimulationSummaryId>::Failure(
                Error("gameplay.simulation.interval_in_progress", "a different simulation interval is pending"));
        return ContinueIntervalExecution(region_it->second, context);
    }

    if (region_it->second.last_simulated_at != from)
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.interval_conflict", "interval start does not match region simulation cursor"));

    auto created = CreateIntervalExecution(region_it->second, from, to, context);
    if (!created)
        return foundation::Result<SimulationSummaryId>::Failure(created.GetError());
    return ContinueIntervalExecution(region_it->second, context);
}

std::vector<SimulationSummary> SimulationService::FindSummaries(SimulationRegionId region) const
{
    std::vector<SimulationSummary> out;
    for (const auto &s : summaries_)
        if (s.region == region)
            out.push_back(s);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

SimulationChangeBatch SimulationService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    SimulationChangeBatch out;
    out.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                     : next_change_sequence_ - 1;
    out.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > out.latest_sequence)
    {
        out.snapshot_required = true;
        return out;
    }
    if (changes_.empty())
    {
        out.snapshot_required = sequence < out.latest_sequence;
        return out;
    }
    if (sequence < out.oldest_available_sequence && out.oldest_available_sequence - sequence > 1)
    {
        out.snapshot_required = true;
        return out;
    }
    for (const auto &change : changes_)
        if (change.sequence > sequence)
            out.changes.push_back(change);
    return out;
}

std::vector<SimulationChange> SimulationService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

SimulationSnapshot SimulationService::CaptureSnapshot() const
{
    SimulationSnapshot s;
    s.regions.reserve(regions_.size());
    for (const auto &[id, region] : regions_)
    {
        (void)id;
        s.regions.push_back(region);
    }
    s.active_intervals.reserve(active_intervals_.size());
    for (const auto &[id, interval] : active_intervals_)
    {
        (void)id;
        s.active_intervals.push_back(interval);
    }
    s.summaries.assign(summaries_.begin(), summaries_.end());
    std::sort(s.regions.begin(), s.regions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.active_intervals.begin(), s.active_intervals.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.summaries.begin(), s.summaries.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.task_ids = task_ids_.GetSnapshot();
    s.summary_ids = summary_ids_.GetSnapshot();
    s.revision = revision_;
    s.change_epoch = journal_epoch_;
    s.next_change_sequence = next_change_sequence_;
    return s;
}

foundation::Result<void> SimulationService::RestoreSnapshot(SimulationSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    if (s.next_change_sequence == 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.restore_invalid", "snapshot cannot restore exhausted transient journal"));

    std::unordered_map<SimulationRegionId, SimulationRegion, IdHash> new_regions;
    std::unordered_map<SimulationRegionId, SimulationIntervalExecution, IdHash> new_intervals;
    std::list<SimulationSummary> new_summaries;
    std::unordered_set<SimulationTaskId, IdHash> seen_task_ids;
    std::unordered_set<SimulationSummaryId, IdHash> seen_summary_ids;

    try
    {
        if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::RestoreRegions))
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.allocation_failed", "region restore staging failed"));
        for (auto &restored_region : s.regions)
        {
            if (!restored_region.id.IsValid() || restored_region.canonical_name.empty() ||
                SimulationRegionId::FromString(restored_region.canonical_name) != restored_region.id ||
                !IsValidSimulationDetailLevel(restored_region.detail_level) ||
                !RevisionWithin(restored_region.revision, s.revision))
                return foundation::Result<void>::Failure(Error("gameplay.simulation.restore_invalid", "invalid region"));
            if (!new_regions.emplace(restored_region.id, restored_region).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.restore_invalid", "duplicate simulation region"));
        }

        if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::RestoreSummaries))
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.allocation_failed", "summary restore staging failed"));
        for (auto &summary : s.summaries)
        {
            const auto region_it = new_regions.find(summary.region);
            if (!summary.id.IsValid() || region_it == new_regions.end() || summary.to.ticks <= summary.from.ticks ||
                summary.to.ticks > region_it->second.last_simulated_at.ticks || summary.layers.size() != layers_.size() ||
                !RevisionWithin(summary.revision, s.revision) || !seen_summary_ids.insert(summary.id).second)
                return foundation::Result<void>::Failure(Error("gameplay.simulation.restore_invalid", "invalid summary"));
            std::unordered_set<SimulationLayerId, IdHash> summary_layers;
            for (const auto &layer_summary : summary.layers)
            {
                if (!layer_summary.layer.IsValid() || !layers_.contains(layer_summary.layer) ||
                    !summary_layers.insert(layer_summary.layer).second ||
                    !RevisionWithin(layer_summary.revision, s.revision) ||
                    !IsValidSimulationTaskState(layer_summary.state))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.simulation.restore_invalid", "invalid summary layer"));
            }
            new_summaries.push_back(summary);
        }

        if (internal_test::ConsumeAllocationFault(internal_test::AllocationFaultPoint::RestoreIntervals))
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.allocation_failed", "interval restore staging failed"));
        for (auto &interval : s.active_intervals)
        {
            const auto region_it = new_regions.find(interval.region);
            if (!interval.id.IsValid() || region_it == new_regions.end() || interval.to.ticks <= interval.from.ticks ||
                !IsValidSimulationIntervalState(interval.state) || interval.state != SimulationIntervalState::Pending ||
                interval.from != region_it->second.last_simulated_at || interval.detail != region_it->second.detail_level ||
                !RevisionWithin(interval.revision, s.revision) || !seen_summary_ids.insert(interval.id).second ||
                interval.layers.size() != layers_.size())
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.restore_invalid", "invalid active simulation interval"));

            std::unordered_set<SimulationLayerId, IdHash> interval_layers;
            for (const auto &layer_execution : interval.layers)
            {
                const auto &task = layer_execution.task;
                if (!task.id.IsValid() || !seen_task_ids.insert(task.id).second || task.region != interval.region ||
                    task.from != interval.from || task.to != interval.to || task.detail != interval.detail ||
                    !layers_.contains(task.layer) || !interval_layers.insert(task.layer).second ||
                    !RevisionWithin(task.revision, s.revision) || !IsValidSimulationTaskState(task.state))
                    return foundation::Result<void>::Failure(
                        Error("gameplay.simulation.restore_invalid", "invalid simulation task"));
                if (layer_execution.prepared)
                {
                    if (layer_execution.prepared_summary.layer != task.layer ||
                        !RevisionWithin(layer_execution.prepared_summary.revision, s.revision))
                        return foundation::Result<void>::Failure(
                            Error("gameplay.simulation.restore_invalid", "invalid prepared simulation summary"));
                }
                else if (task.state == SimulationTaskState::Completed || task.state == SimulationTaskState::Skipped ||
                         task.state == SimulationTaskState::Running)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.simulation.restore_invalid", "task state requires prepared data"));
            }
            if (!new_intervals.emplace(interval.region, interval).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.restore_invalid", "multiple active intervals for region"));
        }
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.simulation.allocation_failed", "simulation restore staging failed"));
    }

    const auto task_validation = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.task_ids, task_ids_.Scope(), MaxTaskLow(s.active_intervals));
    if (!task_validation)
        return foundation::Result<void>::Failure(
            Error(task_validation.Code(), "invalid simulation task id generator snapshot"));
    const auto summary_validation = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.summary_ids, summary_ids_.Scope(), MaxSummaryLow(s.active_intervals, s.summaries));
    if (!summary_validation)
        return foundation::Result<void>::Failure(
            Error(summary_validation.Code(), "invalid simulation summary id generator snapshot"));

    regions_.swap(new_regions);
    active_intervals_.swap(new_intervals);
    summaries_.swap(new_summaries);
    (void)task_ids_.Restore(s.task_ids);
    (void)summary_ids_.Restore(s.summary_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = s.next_change_sequence;
    diagnostics_ = {};
    TrimRetention();
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

SimulationDiagnostics SimulationService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.regions = regions_.size();
    d.layers = layers_.size();
    d.active_intervals = active_intervals_.size();
    d.summaries = summaries_.size();
    return d;
}

void SimulationService::TrimRetention() noexcept
{
    if (retention_.max_completed_summaries == 0)
        summaries_.clear();
    else
        while (summaries_.size() > retention_.max_completed_summaries)
            summaries_.pop_front();

    if (retention_.max_changes == 0)
        changes_.clear();
    else
        while (changes_.size() > retention_.max_changes)
            changes_.pop_front();
}

void SimulationService::Record(SimulationChange change)
{
    std::list<SimulationChange> staged;
    auto next = StageJournalChange(next_change_sequence_, std::move(change), staged);
    if (!next)
    {
        changes_.clear();
        const auto next_epoch = CheckedNextChangeEpoch(journal_epoch_);
        if (next_epoch)
            journal_epoch_ = *next_epoch;
        next_change_sequence_ = 1;
        return;
    }
    CommitJournalChange(changes_, next_change_sequence_, staged, next.Value(), retention_.max_changes);
}
} // namespace epidemic::gameplay::simulation
