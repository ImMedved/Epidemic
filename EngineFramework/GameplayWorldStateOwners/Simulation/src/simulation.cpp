#include "Epidemic/GameFramework/Simulation/simulation.h"
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
} // namespace

SimulationService::SimulationService() : task_ids_(0x30322001), summary_ids_(0x30322002)
{
}

foundation::Result<SimulationRegionId> SimulationService::RegisterRegion(SimulationRegion region)
{
    if (region.canonical_name.empty())
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.invalid_region", "region name required"));
    const auto canonical = SimulationRegionId::FromString(region.canonical_name);
    if (!region.id.IsValid())
        region.id = canonical;
    if (region.id != canonical || regions_.contains(region.id))
        return foundation::Result<SimulationRegionId>::Failure(
            Error("gameplay.simulation.invalid_region", "invalid or duplicate region"));

    Bump();
    region.revision = revision_;
    const auto id = region.id;
    regions_.emplace(id, std::move(region));
    Record({0, SimulationChangeKind::RegionRegistered, id, {}, {}, {}, {}, revision_});
    return foundation::Result<SimulationRegionId>::Success(id);
}

foundation::Result<SimulationLayerId> SimulationService::RegisterLayer(SimulationLayerDefinition layer,
                                                                       ISimulationLayerExecutor *executor)
{
    if (definitions_frozen_)
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.registry_frozen", "simulation layer definitions frozen"));
    if (layer.canonical_name.empty() || executor == nullptr)
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.invalid_layer", "layer name and executor required"));
    const auto canonical = SimulationLayerId::FromString(layer.canonical_name);
    if (!layer.id.IsValid())
        layer.id = canonical;
    if (layer.id != canonical || layers_.contains(layer.id) || executor->Layer() != layer.id)
        return foundation::Result<SimulationLayerId>::Failure(
            Error("gameplay.simulation.invalid_layer", "invalid or duplicate layer"));

    Bump();
    layer.revision = revision_;
    const auto id = layer.id;
    layers_.emplace(id, std::move(layer));
    executors_[id] = executor;
    Record({0, SimulationChangeKind::LayerRegistered, {}, id, {}, {}, {}, revision_});
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
    if (detail == SimulationDetailLevel::Disabled || detail == SimulationDetailLevel::Dormant)
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

    task_ids_ = staged_task_ids;
    summary_ids_ = staged_summary_ids;
    Bump();
    execution.revision = revision_;
    const auto id = execution.id;
    active_intervals_.emplace(region.id, std::move(execution));
    Record({0, SimulationChangeKind::IntervalStarted, region.id, {}, {}, context.time, context, revision_});
    (void)id;
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
                task.state = SimulationTaskState::Deferred;
                Bump();
                task.revision = revision_;
                ++diagnostics_.tasks_deferred;
                Record({0, SimulationChangeKind::TaskDeferred, region.id, task.layer, task.id, context.time, context,
                        revision_});
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
            Bump();
            task.state = SimulationTaskState::Skipped;
            task.revision = revision_;
            layer_execution.prepared = true;
            layer_execution.prepared_summary = {task.layer, SimulationTaskState::Skipped, 0, revision_};
            continue;
        }

        if (!layer_execution.prepared)
        {
            task.state = SimulationTaskState::Running;
            foundation::Result<SimulationLayerSummary> prepared =
                foundation::Result<SimulationLayerSummary>::Failure(
                    Error("gameplay.simulation.prepare_failed", "simulation layer prepare failed"));
            try
            {
                prepared = executor_it->second->Prepare(task);
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
                Bump();
                task.state = SimulationTaskState::Failed;
                task.revision = revision_;
                Record({0, SimulationChangeKind::TaskFailed, region.id, task.layer, task.id, context.time, context,
                        revision_});
                return foundation::Result<SimulationSummaryId>::Failure(prepared.GetError());
            }

            auto layer_summary = std::move(prepared).Value();
            if (layer_summary.layer != task.layer)
            {
                Bump();
                task.state = SimulationTaskState::Failed;
                task.revision = revision_;
                Record({0, SimulationChangeKind::TaskFailed, region.id, task.layer, task.id, context.time, context,
                        revision_});
                return foundation::Result<SimulationSummaryId>::Failure(
                    Error("gameplay.simulation.invalid_prepare", "executor returned summary for a different layer"));
            }
            layer_summary.state = SimulationTaskState::Running;
            Bump();
            task.revision = revision_;
            layer_summary.revision = revision_;
            layer_execution.prepared_summary = std::move(layer_summary);
            layer_execution.prepared = true;
            ++diagnostics_.tasks_prepared;
            Record({0, SimulationChangeKind::TaskPrepared, region.id, task.layer, task.id, context.time, context,
                    revision_});
        }

        if (commits >= budget_.max_layer_commits)
        {
            if (task.state != SimulationTaskState::Deferred)
            {
                Bump();
                task.state = SimulationTaskState::Deferred;
                task.revision = revision_;
                ++diagnostics_.tasks_deferred;
                Record({0, SimulationChangeKind::TaskDeferred, region.id, task.layer, task.id, context.time, context,
                        revision_});
            }
            budget_stopped = true;
            break;
        }

        task.state = SimulationTaskState::Running;
        foundation::Result<void> committed = foundation::Result<void>::Failure(
            Error("gameplay.simulation.commit_failed", "simulation layer commit failed"));
        try
        {
            committed = executor_it->second->Commit(task, layer_execution.prepared_summary);
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
            Bump();
            task.state = SimulationTaskState::Failed;
            task.revision = revision_;
            Record({0, SimulationChangeKind::TaskFailed, region.id, task.layer, task.id, context.time, context,
                    revision_});
            return foundation::Result<SimulationSummaryId>::Failure(committed.GetError());
        }

        ++commits;
        ++diagnostics_.tasks_committed;
        Bump();
        task.state = SimulationTaskState::Completed;
        task.revision = revision_;
        layer_execution.prepared_summary.state = SimulationTaskState::Completed;
        layer_execution.prepared_summary.revision = revision_;
        Record({0, SimulationChangeKind::TaskCommitted, region.id, task.layer, task.id, context.time, context,
                revision_});
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
            ++diagnostics_.budget_exhaustions;
            Record({0, SimulationChangeKind::BudgetExceeded, region.id, {}, {}, context.time, context, revision_});
        }
        return foundation::Result<SimulationSummaryId>::Failure(
            Error("gameplay.simulation.interval_pending", "simulation interval has pending layer work"));
    }

    SimulationSummary summary;
    summary.id = execution.id;
    summary.region = execution.region;
    summary.from = execution.from;
    summary.to = execution.to;
    summary.layers.reserve(execution.layers.size());
    for (const auto &layer_execution : execution.layers)
        summary.layers.push_back(layer_execution.prepared_summary);

    Bump();
    summary.revision = revision_;
    execution.state = SimulationIntervalState::Completed;
    execution.revision = revision_;
    region.last_simulated_at = execution.to;
    region.revision = revision_;
    const auto summary_id = summary.id;
    summaries_.push_back(std::move(summary));
    active_intervals_.erase(execution_it);
    ++diagnostics_.summaries;
    TrimRetention();
    Record({0, SimulationChangeKind::SummaryGenerated, region.id, {}, {}, context.time, context, revision_});
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
    return s;
}

foundation::Result<void> SimulationService::RestoreSnapshot(SimulationSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<SimulationRegionId, SimulationRegion, IdHash> new_regions;
    std::unordered_map<SimulationRegionId, SimulationIntervalExecution, IdHash> new_intervals;
    std::deque<SimulationSummary> new_summaries;
    std::unordered_set<SimulationTaskId, IdHash> task_ids;
    std::unordered_set<SimulationSummaryId, IdHash> summary_ids;

    for (auto &region : s.regions)
    {
        if (!region.id.IsValid() || region.canonical_name.empty() ||
            SimulationRegionId::FromString(region.canonical_name) != region.id || !RevisionWithin(region.revision, s.revision))
            return foundation::Result<void>::Failure(Error("gameplay.simulation.restore_invalid", "invalid region"));
        if (!new_regions.emplace(region.id, region).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.restore_invalid", "duplicate simulation region"));
    }

    for (auto &summary : s.summaries)
    {
        const auto region_it = new_regions.find(summary.region);
        if (!summary.id.IsValid() || region_it == new_regions.end() || summary.to.ticks <= summary.from.ticks ||
            summary.to.ticks > region_it->second.last_simulated_at.ticks || summary.layers.size() != layers_.size() ||
            !RevisionWithin(summary.revision, s.revision) || !summary_ids.insert(summary.id).second)
            return foundation::Result<void>::Failure(Error("gameplay.simulation.restore_invalid", "invalid summary"));
        std::unordered_set<SimulationLayerId, IdHash> summary_layers;
        for (const auto &layer_summary : summary.layers)
        {
            if (!layer_summary.layer.IsValid() || !layers_.contains(layer_summary.layer) ||
                !summary_layers.insert(layer_summary.layer).second || !RevisionWithin(layer_summary.revision, s.revision))
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.restore_invalid", "invalid summary layer"));
        }
        new_summaries.push_back(summary);
    }

    for (auto &interval : s.active_intervals)
    {
        const auto region_it = new_regions.find(interval.region);
        if (!interval.id.IsValid() || region_it == new_regions.end() || interval.to.ticks <= interval.from.ticks ||
            interval.state != SimulationIntervalState::Pending || interval.from != region_it->second.last_simulated_at ||
            interval.detail != region_it->second.detail_level || !RevisionWithin(interval.revision, s.revision) ||
            !summary_ids.insert(interval.id).second || interval.layers.size() != layers_.size())
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.restore_invalid", "invalid active simulation interval"));

        std::unordered_set<SimulationLayerId, IdHash> interval_layers;
        for (const auto &layer_execution : interval.layers)
        {
            const auto &task = layer_execution.task;
            if (!task.id.IsValid() || !task_ids.insert(task.id).second || task.region != interval.region ||
                task.from != interval.from || task.to != interval.to || task.detail != interval.detail ||
                !layers_.contains(task.layer) || !interval_layers.insert(task.layer).second ||
                !RevisionWithin(task.revision, s.revision))
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
            {
                return foundation::Result<void>::Failure(
                    Error("gameplay.simulation.restore_invalid", "task state requires prepared data"));
            }
        }
        if (!new_intervals.emplace(interval.region, interval).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.simulation.restore_invalid", "multiple active intervals for region"));
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

    regions_ = std::move(new_regions);
    active_intervals_ = std::move(new_intervals);
    summaries_ = std::move(new_summaries);
    task_ids_.Restore(s.task_ids);
    summary_ids_.Restore(s.summary_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
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
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(change);
    TrimRetention();
}
} // namespace epidemic::gameplay::simulation
