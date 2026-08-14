#include "Epidemic/GameFramework/Simulation/simulation.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>

namespace epidemic::gameplay::simulation
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message) { return foundation::Error::Create(code, message); }
}
SimulationService::SimulationService()
    : task_ids_(0x30322001), summary_ids_(0x30322002)
{
}
foundation::Result<SimulationRegionId> SimulationService::RegisterRegion(SimulationRegion region)
{
    if (frozen_) return foundation::Result<SimulationRegionId>::Failure(Error("gameplay.simulation.registry_frozen", "simulation registry frozen"));
    if (region.canonical_name.empty()) return foundation::Result<SimulationRegionId>::Failure(Error("gameplay.simulation.invalid_region", "region name required"));
    const auto canonical = SimulationRegionId::FromString(region.canonical_name);
    if (!region.id.IsValid()) region.id = canonical;
    if (region.id != canonical || regions_.contains(region.id)) return foundation::Result<SimulationRegionId>::Failure(Error("gameplay.simulation.invalid_region", "invalid or duplicate region"));
    Bump(); region.revision = revision_;
    const auto id = region.id;
    regions_.emplace(id, std::move(region));
    Record({0, SimulationChangeKind::RegionRegistered, id, {}, {}, {}, {}, revision_});
    return foundation::Result<SimulationRegionId>::Success(id);
}
foundation::Result<SimulationLayerId> SimulationService::RegisterLayer(SimulationLayerDefinition layer, ISimulationLayerExecutor* executor)
{
    if (frozen_) return foundation::Result<SimulationLayerId>::Failure(Error("gameplay.simulation.registry_frozen", "simulation registry frozen"));
    if (layer.canonical_name.empty() || executor == nullptr) return foundation::Result<SimulationLayerId>::Failure(Error("gameplay.simulation.invalid_layer", "layer name and executor required"));
    const auto canonical = SimulationLayerId::FromString(layer.canonical_name);
    if (!layer.id.IsValid()) layer.id = canonical;
    if (layer.id != canonical || layers_.contains(layer.id) || executor->Layer() != layer.id) return foundation::Result<SimulationLayerId>::Failure(Error("gameplay.simulation.invalid_layer", "invalid or duplicate layer"));
    Bump(); layer.revision = revision_;
    const auto id = layer.id;
    layers_.emplace(id, std::move(layer)); executors_[id] = executor;
    Record({0, SimulationChangeKind::LayerRegistered, {}, id, {}, {}, {}, revision_});
    return foundation::Result<SimulationLayerId>::Success(id);
}
const SimulationRegion* SimulationService::FindRegion(SimulationRegionId id) const noexcept { const auto it = regions_.find(id); return it == regions_.end() ? nullptr : &it->second; }
const SimulationLayerDefinition* SimulationService::FindLayer(SimulationLayerId id) const noexcept { const auto it = layers_.find(id); return it == layers_.end() ? nullptr : &it->second; }
bool SimulationService::DetailAllows(const SimulationLayerDefinition& layer, SimulationDetailLevel detail) const noexcept
{
    if (detail == SimulationDetailLevel::Disabled || detail == SimulationDetailLevel::Dormant) return false;
    if (layer.materialization_policy == SimulationMaterializationPolicy::RequiresMaterialized) return detail == SimulationDetailLevel::Materialized;
    if (layer.materialization_policy == SimulationMaterializationPolicy::RequiresDetailed) return detail == SimulationDetailLevel::Detailed || detail == SimulationDetailLevel::Materialized;
    return true;
}
foundation::Result<SimulationSummaryId> SimulationService::SimulateInterval(SimulationRegionId region_id, GameplayTimePoint from, GameplayTimePoint to, GameplayContext context)
{
    auto region_it = regions_.find(region_id);
    if (region_it == regions_.end()) return foundation::Result<SimulationSummaryId>::Failure(Error("gameplay.simulation.region_missing", "simulation region missing"));
    if (to.ticks < from.ticks) return foundation::Result<SimulationSummaryId>::Failure(Error("gameplay.simulation.invalid_interval", "invalid simulation interval"));
    if (budget_.max_interval_ticks > 0 && to.ticks - from.ticks > budget_.max_interval_ticks) return foundation::Result<SimulationSummaryId>::Failure(Error("gameplay.simulation.interval_budget", "simulation interval exceeds budget"));
    std::vector<SimulationLayerDefinition> ordered;
    for (const auto& [id, layer] : layers_) { (void)id; ordered.push_back(layer); }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b){ return a.order == b.order ? a.id < b.id : a.order < b.order; });
    if (ordered.size() > budget_.max_tasks)
    {
        ++diagnostics_.budget_exhaustions;
        Record({0, SimulationChangeKind::BudgetExceeded, region_id, {}, {}, context.time, context, revision_});
        return foundation::Result<SimulationSummaryId>::Failure(Error("gameplay.simulation.task_budget", "simulation task budget exceeded"));
    }
    SimulationSummary summary; summary.id = SimulationSummaryId{summary_ids_.Next()}; summary.region = region_id; summary.from = from; summary.to = to;
    std::uint32_t commits = 0;
    for (const auto& layer : ordered)
    {
        SimulationTask task; task.id = SimulationTaskId{task_ids_.Next()}; task.region = region_id; task.layer = layer.id; task.from = from; task.to = to; task.detail = region_it->second.detail_level; task.state = SimulationTaskState::Pending;
        Bump(); task.revision = revision_;
        Record({0, SimulationChangeKind::TaskPrepared, region_id, layer.id, task.id, context.time, context, revision_});
        ++diagnostics_.tasks_prepared;
        auto executor = executors_.find(layer.id);
        if (executor == executors_.end() || !DetailAllows(layer, task.detail))
        {
            task.state = SimulationTaskState::Skipped;
            summary.layers.push_back({layer.id, SimulationTaskState::Skipped, 0, revision_});
            continue;
        }
        if (commits >= budget_.max_layer_commits)
        {
            task.state = SimulationTaskState::Deferred;
            pending_tasks_.push_back(task);
            ++diagnostics_.tasks_deferred;
            Record({0, SimulationChangeKind::TaskDeferred, region_id, layer.id, task.id, context.time, context, revision_});
            continue;
        }
        auto prepared = executor->second->Prepare(task);
        if (!prepared)
        {
            task.state = SimulationTaskState::Failed;
            summary.layers.push_back({layer.id, SimulationTaskState::Failed, 0, revision_});
            Record({0, SimulationChangeKind::TaskFailed, region_id, layer.id, task.id, context.time, context, revision_});
            continue;
        }
        auto layer_summary = std::move(prepared).Value();
        auto committed = executor->second->Commit(task, layer_summary);
        if (!committed) return foundation::Result<SimulationSummaryId>::Failure(committed.GetError());
        ++commits; ++diagnostics_.tasks_committed;
        layer_summary.state = SimulationTaskState::Completed;
        summary.layers.push_back(layer_summary);
        Record({0, SimulationChangeKind::TaskCommitted, region_id, layer.id, task.id, context.time, context, revision_});
    }
    Bump(); summary.revision = revision_; region_it->second.last_simulated_at = to; region_it->second.revision = revision_;
    const auto id = summary.id;
    summaries_.push_back(std::move(summary));
    ++diagnostics_.summaries;
    Record({0, SimulationChangeKind::SummaryGenerated, region_id, {}, {}, context.time, context, revision_});
    return foundation::Result<SimulationSummaryId>::Success(id);
}
std::vector<SimulationSummary> SimulationService::FindSummaries(SimulationRegionId region) const
{
    std::vector<SimulationSummary> out;
    for (const auto& s : summaries_) if (s.region == region) out.push_back(s);
    std::sort(out.begin(), out.end(), [](auto&a, auto&b){ return a.id < b.id; });
    return out;
}
std::vector<SimulationChange> SimulationService::ChangesSince(std::uint64_t sequence) const
{
    std::vector<SimulationChange> out; std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out), [&](const auto& c){ return c.sequence > sequence; }); return out;
}
SimulationSnapshot SimulationService::CaptureSnapshot() const
{
    SimulationSnapshot s;
    for (const auto& [id, region] : regions_) { (void)id; s.regions.push_back(region); }
    s.pending_tasks = pending_tasks_;
    s.summaries = summaries_;
    std::sort(s.regions.begin(), s.regions.end(), [](auto&a, auto&b){ return a.id < b.id; });
    std::sort(s.pending_tasks.begin(), s.pending_tasks.end(), [](auto&a, auto&b){ return a.id < b.id; });
    std::sort(s.summaries.begin(), s.summaries.end(), [](auto&a, auto&b){ return a.id < b.id; });
    s.task_ids = task_ids_.GetSnapshot(); s.summary_ids = summary_ids_.GetSnapshot(); s.revision = revision_;
    return s;
}
foundation::Result<void> SimulationService::RestoreSnapshot(SimulationSnapshot s)
{
    regions_.clear(); pending_tasks_ = std::move(s.pending_tasks); summaries_ = std::move(s.summaries);
    for (auto& region : s.regions)
    {
        if (!region.id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.simulation.restore_invalid", "invalid region"));
        regions_[region.id] = std::move(region);
    }
    task_ids_.Restore(s.task_ids); summary_ids_.Restore(s.summary_ids); revision_ = s.revision; changes_.clear(); next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
SimulationDiagnostics SimulationService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_; d.regions = regions_.size(); d.layers = layers_.size(); d.summaries = summaries_.size(); return d;
}
void SimulationService::Record(SimulationChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(change);
}
}
