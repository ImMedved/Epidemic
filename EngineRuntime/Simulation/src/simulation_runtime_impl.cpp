#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime::simulation
{
SimulationRuntime::SimulationRuntime(SimulationOptions options) : options_(options)
{
}

foundation::Result<SimulationJobId> SimulationRuntime::SubmitJob(const SimulationJobDesc& desc)
{
    if (!desc.zone.IsValid())
    {
        return foundation::Result<SimulationJobId>::Failure(
            foundation::Error::Create("simulation.invalid_zone", "simulation job must reference a valid zone"));
    }

    if (desc.work_units == 0)
    {
        return foundation::Result<SimulationJobId>::Failure(
            foundation::Error::Create("simulation.empty_job", "simulation job must contain work units"));
    }

    const SimulationJobId id{next_job_value_++};
    JobRecord record{};
    record.desc = desc;
    record.state = SimulationJobState::Pending;
    record.remaining_work_units = desc.work_units;
    jobs_.emplace(id, record);
    return foundation::Result<SimulationJobId>::Success(id);
}

foundation::Result<void> SimulationRuntime::CancelJob(SimulationJobId id)
{
    JobRecord* job = FindJob(id);
    if (job == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.job_not_found", "simulation job was not found for cancellation"));
    }

    if (IsTerminal(job->state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.job_terminal", "terminal simulation jobs cannot be cancelled"));
    }

    job->state = SimulationJobState::Cancelled;
    return foundation::Result<void>::Success();
}

SimulationJobState SimulationRuntime::GetJobState(SimulationJobId id) const
{
    const JobRecord* job = FindJob(id);
    if (job == nullptr)
    {
        return SimulationJobState::Failed;
    }

    return job->state;
}

void SimulationRuntime::SetBudget(const SimulationBudget& budget)
{
    budget_ = budget;
}

std::size_t SimulationRuntime::Tick()
{
    if (!options_.enable_budgeted_scheduler)
    {
        return 0;
    }

    const std::uint32_t job_limit = budget_.max_jobs == 0 ? static_cast<std::uint32_t>(jobs_.size()) : budget_.max_jobs;
    std::uint32_t work_budget = budget_.max_work_units == 0 ? UINT32_MAX : budget_.max_work_units;
    std::size_t transitioned = 0;

    for (auto& [id, job] : jobs_)
    {
        (void)id;
        if (transitioned >= job_limit || work_budget == 0)
        {
            break;
        }

        if (IsTerminal(job.state) || job.state == SimulationJobState::WaitingForMainThread)
        {
            continue;
        }

        job.state = SimulationJobState::Running;
        const std::uint32_t consumed = std::min(job.remaining_work_units, work_budget);
        job.remaining_work_units -= consumed;
        work_budget -= consumed;

        if (job.remaining_work_units == 0)
        {
            job.state = job.desc.wait_for_main_thread ? SimulationJobState::WaitingForMainThread : SimulationJobState::Completed;
        }
        else
        {
            job.state = SimulationJobState::PartiallyComplete;
        }
        ++transitioned;
    }

    return transitioned;
}

foundation::Result<void> SimulationRuntime::SetAttention(RuntimeObjectId object, AttentionScore score)
{
    if (!object.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_object", "attention target object must be valid"));
    }

    object_attention_[object] = score;
    return foundation::Result<void>::Success();
}

AttentionScore SimulationRuntime::GetAttention(RuntimeObjectId object) const
{
    const auto iterator = object_attention_.find(object);
    if (iterator == object_attention_.end())
    {
        return AttentionScore{};
    }

    return iterator->second;
}

foundation::Result<void> SimulationRuntime::SetRegionAttention(RegionId region, AttentionScore score)
{
    if (!region.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_region", "attention target region must be valid"));
    }

    region_attention_[region] = score;
    return foundation::Result<void>::Success();
}

AttentionScore SimulationRuntime::GetRegionAttention(RegionId region) const
{
    const auto iterator = region_attention_.find(region);
    if (iterator == region_attention_.end())
    {
        return AttentionScore{};
    }

    return iterator->second;
}

foundation::Result<WorldMemoryEventId> SimulationRuntime::RecordEvent(const WorldMemoryEvent& event)
{
    if (!event.region.IsValid())
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.invalid_memory_region", "memory event must reference a valid region"));
    }

    const WorldMemoryEventId id = event.id.IsValid() ? event.id : WorldMemoryEventId{next_memory_value_++};
    WorldMemoryEvent stored = event;
    stored.id = id;
    memory_events_[id] = stored;
    return foundation::Result<WorldMemoryEventId>::Success(id);
}

std::vector<WorldMemoryEvent> SimulationRuntime::QueryEvents(const WorldMemoryQuery& query) const
{
    std::vector<WorldMemoryEvent> matches;
    for (const auto& [id, event] : memory_events_)
    {
        (void)id;
        if (query.region.IsValid() && event.region != query.region)
        {
            continue;
        }

        if (!query.include_expired && event.state == WorldMemoryEventState::Expired)
        {
            continue;
        }

        matches.push_back(event);
    }

    return matches;
}

void SimulationRuntime::ExpireOldEvents(GameTime now)
{
    for (auto& [id, event] : memory_events_)
    {
        (void)id;
        if (event.state == WorldMemoryEventState::Persistent || event.state == WorldMemoryEventState::Expired || event.ttl.IsZero())
        {
            continue;
        }

        if ((event.happened_at + event.ttl).ticks <= now.ticks)
        {
            event.state = WorldMemoryEventState::Expired;
        }
    }
}

foundation::Result<void> SimulationRuntime::Submit(const SimulationEffect& effect)
{
    if (!effect.target.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_effect_target", "simulation effect must reference a valid target"));
    }

    if (!effect.zone.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_effect_zone", "simulation effect must reference a valid zone"));
    }

    effects_.push_back(effect);
    return foundation::Result<void>::Success();
}

std::span<const SimulationEffect> SimulationRuntime::Effects() const
{
    return effects_;
}

void SimulationRuntime::Clear()
{
    effects_.clear();
}

SimulationRuntime::JobRecord* SimulationRuntime::FindJob(SimulationJobId id)
{
    const auto iterator = jobs_.find(id);
    if (iterator == jobs_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const SimulationRuntime::JobRecord* SimulationRuntime::FindJob(SimulationJobId id) const
{
    const auto iterator = jobs_.find(id);
    if (iterator == jobs_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

bool SimulationRuntime::IsTerminal(SimulationJobState state) const noexcept
{
    return state == SimulationJobState::Completed || state == SimulationJobState::Failed || state == SimulationJobState::Cancelled;
}
} // namespace epidemic::runtime::simulation
