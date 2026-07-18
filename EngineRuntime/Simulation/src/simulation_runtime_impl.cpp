#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace epidemic::runtime::simulation
{
namespace
{
class ReferenceSimulationJob final : public ISimulationJob
{
public:
    ReferenceSimulationJob(SimulationJobHandle handle, SimulationJobDesc desc)
        : handle_(handle), desc_(desc), pending_work_units_(desc.work_units)
    {
    }

    foundation::Result<SimulationStepResult> ExecuteStep(const RuntimeBudget& budget) override
    {
        const std::uint32_t work_budget = budget.max_items == 0 ? pending_work_units_ : std::min<std::uint32_t>(budget.max_items, pending_work_units_);
        pending_work_units_ -= work_budget;

        SimulationStepResult result{};
        result.consumed_work_units = work_budget;
        result.proposals.job = handle_;
        result.proposals.zone = desc_.zone;
        result.proposals.source_revision = desc_.source_revision;
        if (pending_work_units_ == 0)
        {
            result.state = desc_.wait_for_main_thread ? SimulationJobState::WaitingForMainThread : SimulationJobState::Completed;
            result.proposals.proposals.push_back(SimulationProposal{
                foundation::StringId::FromString("simulation"),
                foundation::StringId::FromString("reference"),
                desc_.subject,
                foundation::StringId::FromString("simulation.reference.v1"),
                1,
                {}});
        }
        else
        {
            result.state = SimulationJobState::PartiallyComplete;
        }
        return foundation::Result<SimulationStepResult>::Success(std::move(result));
    }

    foundation::Result<void> Cancel() override
    {
        pending_work_units_ = 0;
        return foundation::Result<void>::Success();
    }

private:
    SimulationJobHandle handle_{};
    SimulationJobDesc desc_{};
    std::uint32_t pending_work_units_ = 0;
};
} // namespace

SimulationRuntime::SimulationRuntime(SimulationOptions options, SimulationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
}

foundation::Result<SimulationJobId> SimulationRuntime::SubmitJob(const SimulationJobDesc& desc)
{
    const auto handle = SubmitJobHandle(desc);
    if (!handle)
    {
        return foundation::Result<SimulationJobId>::Failure(handle.GetError());
    }

    return foundation::Result<SimulationJobId>::Success(handle.Value().id);
}

foundation::Result<SimulationJobHandle> SimulationRuntime::SubmitJobHandle(const SimulationJobDesc& desc)
{
    if (!desc.zone.IsValid())
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.invalid_zone", "simulation job must reference a valid zone"));
    }

    if (desc.work_units == 0)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.empty_job", "simulation job must contain work units"));
    }

    const SimulationJobId id{next_job_value_++};
    const SimulationJobHandle handle{id, next_job_generation_++};
    JobRecord record{};
    record.desc = desc;
    record.handle = handle;
    record.state = SimulationJobState::Pending;
    record.executable = std::make_unique<ReferenceSimulationJob>(handle, desc);
    jobs_.emplace(id, std::move(record));
    return foundation::Result<SimulationJobHandle>::Success(handle);
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
    if (job->executable)
    {
        (void)job->executable->Cancel();
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> SimulationRuntime::CancelJob(SimulationJobHandle handle)
{
    JobRecord* job = FindJob(handle.id);
    if (job == nullptr || job->handle.generation != handle.generation)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.stale_handle", "simulation job handle generation is stale"));
    }

    return CancelJob(handle.id);
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

    for (const SimulationJobId id : BuildJobWorkList())
    {
        if (transitioned >= job_limit || work_budget == 0)
        {
            break;
        }

        JobRecord& job = jobs_[id];
        if (IsTerminal(job.state) || job.state == SimulationJobState::WaitingForMainThread)
        {
            continue;
        }

        job.state = SimulationJobState::Running;
        const auto step = job.executable->ExecuteStep(ToRuntimeBudget(work_budget));
        if (!step)
        {
            job.state = SimulationJobState::Failed;
            ++transitioned;
            continue;
        }

        const SimulationStepResult& step_result = step.Value();
        work_budget -= std::min(work_budget, step_result.consumed_work_units);
        job.state = step_result.state;
        if (job.state == SimulationJobState::Completed && !step_result.proposals.proposals.empty())
        {
            const auto published = Publish(step_result.proposals);
            if (!published)
            {
                job.state = SimulationJobState::Failed;
            }
        }
        else if (job.state == SimulationJobState::Failed || job.state == SimulationJobState::Cancelled)
        {
            continue;
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
    for (const WorldMemoryEventId id : BuildMemoryWorkList())
    {
        const auto& event = memory_events_.at(id);
        if (query.region.IsValid() && event.region != query.region)
        {
            continue;
        }

        if (!query.include_expired && event.expired)
        {
            continue;
        }

        matches.push_back(event);
    }

    return matches;
}

void SimulationRuntime::ExpireOldEvents(SimulationTime now)
{
    (void)ExpireOldEvents(now, 0);
}

std::size_t SimulationRuntime::ExpireOldEvents(SimulationTime now, std::uint32_t max_events)
{
    const std::uint32_t limit = max_events == 0 ? UINT32_MAX : max_events;
    std::size_t expired = 0;

    for (const WorldMemoryEventId id : BuildMemoryWorkList())
    {
        if (expired >= limit)
        {
            break;
        }

        auto& event = memory_events_[id];
        if (event.expired || event.lifetime == MemoryLifetime::Persistent || event.ttl.IsZero())
        {
            continue;
        }

        if ((event.happened_at + event.ttl).ticks <= now.ticks)
        {
            event.expired = true;
            ++expired;
        }
    }

    return expired;
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
    proposal_batches_.clear();
}

foundation::Result<void> SimulationRuntime::RecordFact(const AbstractFact& fact)
{
    if (!fact.subject.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact_subject", "abstract fact must reference a valid subject"));
    }
    if (!fact.zone.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact_zone", "abstract fact must reference a valid zone"));
    }

    AbstractFact stored = fact;
    stored.revision = ++fact_revision_;
    facts_.push_back(stored);
    return foundation::Result<void>::Success();
}

std::vector<AbstractFact> SimulationRuntime::QueryFacts(SimulationZoneId zone) const
{
    std::vector<AbstractFact> matches;
    for (const AbstractFact& fact : facts_)
    {
        if (!zone.IsValid() || fact.zone == zone)
        {
            matches.push_back(fact);
        }
    }
    return matches;
}

std::uint64_t SimulationRuntime::Revision() const
{
    return fact_revision_;
}

foundation::Result<void> SimulationRuntime::Publish(const SimulationProposalBatch& batch)
{
    if (!batch.job.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_proposal_job", "proposal batch must reference a valid job"));
    }
    if (!batch.zone.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_proposal_zone", "proposal batch must reference a valid zone"));
    }

    proposal_batches_.push_back(batch);
    return foundation::Result<void>::Success();
}

std::span<const SimulationProposalBatch> SimulationRuntime::PendingBatches() const
{
    return proposal_batches_;
}

foundation::Result<void> SimulationRuntime::CommitNext()
{
    if (proposal_batches_.empty())
    {
        return foundation::Result<void>::Success();
    }

    if (dependencies_.commit_target == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.commit_target_missing", "simulation commit target is required to commit proposals"));
    }

    const auto committed = dependencies_.commit_target->Commit(proposal_batches_.front());
    if (!committed)
    {
        return foundation::Result<void>::Failure(committed.GetError());
    }

    proposal_batches_.erase(proposal_batches_.begin());
    return foundation::Result<void>::Success();
}

foundation::Result<ScheduledSimulationTaskId> SimulationRuntime::Schedule(ScheduledSimulationTask task)
{
    if (!task.job.IsValid())
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.invalid_scheduled_job", "scheduled task must reference a valid job"));
    }

    const ScheduledSimulationTaskId id{next_task_value_++};
    task.id = id;
    scheduled_tasks_[id] = task;
    return foundation::Result<ScheduledSimulationTaskId>::Success(id);
}

foundation::Result<void> SimulationRuntime::Cancel(ScheduledSimulationTaskId id)
{
    if (scheduled_tasks_.erase(id) == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.scheduled_task_not_found", "scheduled task was not found"));
    }
    return foundation::Result<void>::Success();
}

std::vector<ScheduledSimulationTask> SimulationRuntime::QueryDue(SimulationTime now) const
{
    std::vector<ScheduledSimulationTask> due;
    for (const ScheduledSimulationTaskId id : BuildTaskWorkList())
    {
        const auto& task = scheduled_tasks_.at(id);
        if (task.due_at.ticks <= now.ticks)
        {
            due.push_back(task);
        }
    }
    return due;
}

std::size_t SimulationRuntime::ExecuteDueWithinBudget(SimulationTime now, const SimulationBudget& budget)
{
    const std::uint32_t limit = budget.max_jobs == 0 ? UINT32_MAX : budget.max_jobs;
    std::size_t executed = 0;
    for (const ScheduledSimulationTaskId id : BuildTaskWorkList())
    {
        if (executed >= limit)
        {
            break;
        }
        const auto iterator = scheduled_tasks_.find(id);
        if (iterator == scheduled_tasks_.end() || iterator->second.due_at.ticks > now.ticks)
        {
            continue;
        }
        (void)CancelJob(iterator->second.job);
        scheduled_tasks_.erase(iterator);
        ++executed;
    }
    return executed;
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

std::vector<SimulationJobId> SimulationRuntime::BuildJobWorkList() const
{
    std::vector<SimulationJobId> work_list;
    work_list.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_)
    {
        if (!IsTerminal(job.state) && job.state != SimulationJobState::WaitingForMainThread)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](SimulationJobId left, SimulationJobId right) {
        return left.value < right.value;
    });
    return work_list;
}

std::vector<WorldMemoryEventId> SimulationRuntime::BuildMemoryWorkList() const
{
    std::vector<WorldMemoryEventId> work_list;
    work_list.reserve(memory_events_.size());
    for (const auto& [id, event] : memory_events_)
    {
        (void)event;
        work_list.push_back(id);
    }

    std::sort(work_list.begin(), work_list.end(), [](WorldMemoryEventId left, WorldMemoryEventId right) {
        return left.value < right.value;
    });
    return work_list;
}

std::vector<ScheduledSimulationTaskId> SimulationRuntime::BuildTaskWorkList() const
{
    std::vector<ScheduledSimulationTaskId> work_list;
    work_list.reserve(scheduled_tasks_.size());
    for (const auto& [id, task] : scheduled_tasks_)
    {
        (void)task;
        work_list.push_back(id);
    }

    std::sort(work_list.begin(), work_list.end(), [](ScheduledSimulationTaskId left, ScheduledSimulationTaskId right) {
        return left.value < right.value;
    });
    return work_list;
}

bool SimulationRuntime::IsTerminal(SimulationJobState state) const noexcept
{
    return state == SimulationJobState::Completed || state == SimulationJobState::Failed || state == SimulationJobState::Cancelled;
}

RuntimeBudget SimulationRuntime::ToRuntimeBudget(std::uint32_t work_units) const noexcept
{
    return RuntimeBudget{.max_items = work_units};
}
} // namespace epidemic::runtime::simulation
