#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace epidemic::runtime::simulation
{
namespace
{
[[nodiscard]] bool IsValidAttention(AttentionScore score) noexcept
{
    return std::isfinite(score.value) && score.value >= 0.0f && score.value <= 1.0f;
}
} // namespace

SimulationRuntime::SimulationRuntime(SimulationOptions options, SimulationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
}

foundation::Result<SimulationJobHandle> SimulationRuntime::SubmitJob(
    std::shared_ptr<ISimulationJob> job,
    SimulationJobDesc metadata)
{
    if (!accepting_jobs_)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.shutdown", "simulation runtime is not accepting new jobs"));
    }
    const auto validation = ValidateJobMetadata(metadata);
    if (!validation)
    {
        return foundation::Result<SimulationJobHandle>::Failure(validation.GetError());
    }
    if (job == nullptr)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.job_missing", "simulation job executable must be provided"));
    }
    if (!CanAllocateMonotonicId(next_job_value_) || !CanAllocateMonotonicId(next_job_generation_))
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.job_id_overflow", "simulation job id allocator is exhausted"));
    }

    const auto job_value = AllocateMonotonicId(next_job_value_, "simulation.job_id_overflow", "simulation job id allocator is exhausted");
    const auto job_generation = AllocateMonotonicId(next_job_generation_, "simulation.job_id_overflow", "simulation job generation allocator is exhausted");
    if (!job_value || !job_generation)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.job_id_overflow", "simulation job id allocator is exhausted"));
    }
    const SimulationJobId id{job_value.Value()};
    const SimulationJobHandle handle{id, job_generation.Value()};
    JobRecord record{};
    record.desc = metadata;
    record.handle = handle;
    record.state = SimulationJobState::Pending;
    record.executable = std::move(job);
    const auto [job_iterator, inserted] = jobs_.emplace(id, std::move(record));
    if (!inserted)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.duplicate_job_id", "allocated simulation job id already exists"));
    }
    (void)job_iterator;
    return foundation::Result<SimulationJobHandle>::Success(handle);
}

foundation::Result<void> SimulationRuntime::CancelJob(SimulationJobHandle handle)
{
    JobRecord* job = FindJob(handle);
    if (job == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.stale_handle", "simulation job handle generation is stale"));
    }

    if (IsTerminal(job->state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.job_terminal", "terminal simulation jobs cannot be cancelled"));
    }

    if (job->executable)
    {
        const auto cancelled = job->executable->Cancel();
        if (!cancelled)
        {
            return foundation::Result<void>::Failure(cancelled.GetError());
        }
    }
    if (job->active_schedule)
    {
        scheduled_tasks_.erase(*job->active_schedule);
        job->active_schedule.reset();
    }
    job->state = SimulationJobState::Cancelled;
    return foundation::Result<void>::Success();
}

foundation::Result<SimulationJobState> SimulationRuntime::GetJobState(SimulationJobHandle handle) const
{
    const JobRecord* job = FindJob(handle);
    if (job == nullptr)
    {
        return foundation::Result<SimulationJobState>::Failure(
            foundation::Error::Create("simulation.stale_handle", "simulation job handle generation is stale"));
    }

    return foundation::Result<SimulationJobState>::Success(job->state);
}

void SimulationRuntime::SetBudget(const SimulationBudget& budget)
{
    budget_ = budget;
}

foundation::Result<SimulationTickResult> SimulationRuntime::Tick()
{
    SimulationTickResult result{};
    if (!options_.enable_budgeted_scheduler)
    {
        return foundation::Result<SimulationTickResult>::Success(std::move(result));
    }

    if (dependencies_.clock != nullptr)
    {
        const auto scheduled = ExecuteDueWithinBudget(dependencies_.clock->Now(), budget_);
        for (const ScheduledTaskFailure& failure : scheduled.failures)
        {
            result.failures.push_back(SimulationTickFailure{failure.job, failure.error});
        }
    }

    const std::uint32_t job_limit = budget_.max_jobs == 0 ? static_cast<std::uint32_t>(jobs_.size()) : budget_.max_jobs;
    std::uint32_t work_budget = budget_.max_work_units == 0 ? UINT32_MAX : budget_.max_work_units;

    for (const SimulationJobId id : BuildJobWorkList())
    {
        if (result.processed_jobs >= job_limit || work_budget == 0)
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
            result.failures.push_back(SimulationTickFailure{job.handle, step.GetError()});
            ++result.processed_jobs;
            continue;
        }

        const SimulationStepResult& step_result = step.Value();
        const auto validated = ValidateStepResult(job, step_result);
        if (!validated)
        {
            job.state = SimulationJobState::Failed;
            result.failures.push_back(SimulationTickFailure{job.handle, validated.GetError()});
            ++result.processed_jobs;
            continue;
        }
        if (step_result.consumed_work_units > work_budget)
        {
            job.state = SimulationJobState::Failed;
            result.failures.push_back(SimulationTickFailure{
                job.handle,
                foundation::Error::Create("simulation.work_budget_exceeded", "simulation job consumed more work than granted")});
            ++result.processed_jobs;
            continue;
        }
        work_budget -= std::min(work_budget, step_result.consumed_work_units);
        job.state = step_result.state;
        if (job.state == SimulationJobState::WaitingForMainThread)
        {
            job.pending_main_thread_result = step_result;
        }
        else if (job.state == SimulationJobState::Completed && !step_result.proposals.proposals.empty())
        {
            SimulationProposalBatch normalized = step_result.proposals;
            normalized.job = job.handle;
            normalized.zone = job.desc.zone;
            normalized.source_revision = job.desc.source_revision;
            const auto published = Publish(normalized);
            if (!published)
            {
                job.state = SimulationJobState::Failed;
                result.failures.push_back(SimulationTickFailure{job.handle, published.GetError()});
            }
            else
            {
                result.proposal_batches.push_back(std::move(normalized));
            }
        }
        else if (job.state == SimulationJobState::Failed || job.state == SimulationJobState::Cancelled)
        {
            continue;
        }
        ++result.processed_jobs;
    }

    PruneTerminalJobs();
    return foundation::Result<SimulationTickResult>::Success(std::move(result));
}

foundation::Result<SimulationTickResult> SimulationRuntime::ProcessMainThreadCommits(std::uint32_t max_jobs)
{
    SimulationTickResult result{};
    const std::uint32_t limit = max_jobs == 0 ? UINT32_MAX : max_jobs;
    for (const SimulationJobId id : BuildMainThreadWorkList())
    {
        if (result.processed_jobs >= limit)
        {
            break;
        }

        JobRecord& job = jobs_[id];
        if (job.pending_main_thread_result.has_value() &&
            !job.pending_main_thread_result->proposals.proposals.empty())
        {
            SimulationProposalBatch normalized = job.pending_main_thread_result->proposals;
            normalized.job = job.handle;
            normalized.zone = job.desc.zone;
            normalized.source_revision = job.desc.source_revision;
            const auto published = Publish(normalized);
            if (!published)
            {
                job.state = SimulationJobState::Failed;
                result.failures.push_back(SimulationTickFailure{job.handle, published.GetError()});
            }
            else
            {
                job.state = SimulationJobState::Completed;
                result.proposal_batches.push_back(std::move(normalized));
            }
        }
        else
        {
            job.state = SimulationJobState::Completed;
        }
        job.pending_main_thread_result.reset();
        ++result.processed_jobs;
    }

    PruneTerminalJobs();
    return foundation::Result<SimulationTickResult>::Success(std::move(result));
}

foundation::Result<void> SimulationRuntime::Shutdown()
{
    if (shutdown_)
    {
        return foundation::Result<void>::Success();
    }

    accepting_jobs_ = false;
    std::optional<foundation::Error> first_error;
    std::vector<SimulationJobId> ids;
    ids.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_)
    {
        (void)job;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](SimulationJobId left, SimulationJobId right) { return left.value < right.value; });
    for (SimulationJobId id : ids)
    {
        JobRecord* job = FindJob(id);
        if (job == nullptr || IsTerminal(job->state))
        {
            continue;
        }
        const auto cancelled = CancelJob(job->handle);
        if (!cancelled && !first_error.has_value())
        {
            first_error = cancelled.GetError();
        }
    }
    if (first_error.has_value())
    {
        return foundation::Result<void>::Failure(*first_error);
    }
    scheduled_tasks_.clear();
    shutdown_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SimulationRuntime::SetAttention(RuntimeObjectId object, AttentionScore score)
{
    if (!object.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_object", "attention target object must be valid"));
    }
    if (!IsValidAttention(score))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_attention", "attention score must be finite and within [0, 1]"));
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
    if (!IsValidAttention(score))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_attention", "attention score must be finite and within [0, 1]"));
    }

    region_attention_[region] = score;
    if (dependencies_.relevance != nullptr)
    {
        region_zone_states_[region] = dependencies_.relevance->ClassifyZone(region, score);
    }
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

SimulationZoneState SimulationRuntime::GetRegionZoneState(RegionId region) const
{
    const auto iterator = region_zone_states_.find(region);
    if (iterator == region_zone_states_.end())
    {
        return SimulationZoneState::Dormant;
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
    if (event.ttl.ticks < 0)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.invalid_memory_ttl", "memory event TTL must not be negative"));
    }
    if (event.happened_at.ticks < 0)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.invalid_memory_time", "memory event time must not be negative"));
    }

    PruneExpiredMemoryEvents();
    if (options_.max_memory_events != 0 && memory_events_.size() >= options_.max_memory_events)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_store_full", "world memory event store capacity is exhausted"));
    }

    WorldMemoryEventId id = event.id;
    if (!id.IsValid())
    {
        const auto allocated = AllocateMonotonicId(
            next_memory_value_,
            "simulation.memory_event_id_overflow",
            "memory event id allocator is exhausted");
        if (!allocated)
        {
            return foundation::Result<WorldMemoryEventId>::Failure(allocated.GetError());
        }
        id = WorldMemoryEventId{allocated.Value()};
    }
    if (memory_events_.contains(id))
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_event_already_exists", "memory event id already exists"));
    }
    if (event.id.IsValid() && next_memory_value_ != 0 && event.id.value >= next_memory_value_)
    {
        next_memory_value_ = event.id.value == std::numeric_limits<std::uint64_t>::max() ? 0 : event.id.value + 1;
    }
    WorldMemoryEvent stored = event;
    stored.id = id;
    const auto [memory_iterator, inserted] = memory_events_.emplace(id, std::move(stored));
    if (!inserted)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_event_already_exists", "memory event id already exists"));
    }
    (void)memory_iterator;
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

    PruneExpiredMemoryEvents();
    return expired;
}

foundation::Result<void> SimulationRuntime::DiscardAll(ProposalDiscardReason reason)
{
    switch (reason)
    {
    case ProposalDiscardReason::Shutdown:
    case ProposalDiscardReason::AdministrativeReset:
        break;
    default:
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_discard_reason", "proposal discard reason is invalid"));
    }
    proposal_batches_.clear();
    PruneTerminalJobs();
    return foundation::Result<void>::Success();
}

foundation::Result<void> SimulationRuntime::RecordFact(const AbstractFact& fact)
{
    if (!fact.domain.IsValid() || !fact.kind.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact_metadata", "abstract fact domain and kind must be valid"));
    }
    if (fact.fact_type == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact_type", "abstract fact type must be non-zero"));
    }
    if (fact.observed_at.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact_time", "abstract fact observation time must not be negative"));
    }
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
    if (fact_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.revision_overflow", "abstract fact revision overflow"));
    }
    if (options_.max_facts != 0 && facts_.size() >= options_.max_facts)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.fact_store_full", "abstract fact store capacity is exhausted"));
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
    const auto validation = ValidateProposalBatch(batch);
    if (!validation)
    {
        return foundation::Result<void>::Failure(validation.GetError());
    }

    if (options_.max_proposal_batches != 0 && proposal_batches_.size() >= options_.max_proposal_batches)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.proposal_queue_full", "simulation proposal queue is full"));
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
    PruneTerminalJobs();
    return foundation::Result<void>::Success();
}

foundation::Result<ScheduledSimulationTaskId> SimulationRuntime::Schedule(ScheduledSimulationTask task)
{
    JobRecord* job = FindJob(task.job);
    if (job == nullptr)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.stale_handle", "scheduled task must reference an existing job handle"));
    }
    if (IsTerminal(job->state))
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.job_terminal", "terminal simulation jobs cannot be scheduled"));
    }
    if (job->active_schedule.has_value() || job->state == SimulationJobState::Scheduled)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.job_already_scheduled", "simulation job already has an active scheduled task"));
    }
    const auto task_value = AllocateMonotonicId(
        next_task_value_,
        "simulation.task_id_overflow",
        "scheduled simulation task id allocator is exhausted");
    if (!task_value)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(task_value.GetError());
    }

    const ScheduledSimulationTaskId id{task_value.Value()};
    task.id = id;
    const auto [task_iterator, inserted] = scheduled_tasks_.emplace(id, task);
    if (!inserted)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.duplicate_task_id", "allocated scheduled simulation task id already exists"));
    }
    (void)task_iterator;
    job->state = SimulationJobState::Scheduled;
    job->active_schedule = id;
    return foundation::Result<ScheduledSimulationTaskId>::Success(id);
}

foundation::Result<void> SimulationRuntime::Cancel(ScheduledSimulationTaskId id)
{
    const auto iterator = scheduled_tasks_.find(id);
    if (iterator == scheduled_tasks_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.scheduled_task_not_found", "scheduled task was not found"));
    }
    JobRecord* job = FindJob(iterator->second.job);
    if (job != nullptr && job->active_schedule == id)
    {
        job->active_schedule.reset();
        if (job->state == SimulationJobState::Scheduled)
        {
            job->state = SimulationJobState::Pending;
        }
    }
    scheduled_tasks_.erase(iterator);
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

ScheduledTaskExecutionResult SimulationRuntime::ExecuteDueWithinBudget(SimulationTime now, const SimulationBudget& budget)
{
    const std::uint32_t limit = budget.max_jobs == 0 ? UINT32_MAX : budget.max_jobs;
    ScheduledTaskExecutionResult result{};
    for (const ScheduledSimulationTaskId id : BuildTaskWorkList())
    {
        if (result.activated >= limit)
        {
            break;
        }
        const auto iterator = scheduled_tasks_.find(id);
        if (iterator == scheduled_tasks_.end() || iterator->second.due_at.ticks > now.ticks)
        {
            continue;
        }
        const ScheduledSimulationTask task = iterator->second;
        JobRecord* job = FindJob(iterator->second.job);
        if (job == nullptr)
        {
            result.failures.push_back(ScheduledTaskFailure{
                task.id,
                task.job,
                foundation::Error::Create("simulation.stale_handle", "scheduled task references a missing or stale job")});
            scheduled_tasks_.erase(iterator);
            continue;
        }
        if (job->state != SimulationJobState::Scheduled || job->active_schedule != id)
        {
            result.failures.push_back(ScheduledTaskFailure{
                task.id,
                task.job,
                foundation::Error::Create("simulation.invalid_scheduled_job_state", "scheduled task job is not waiting for this schedule")});
            if (job->active_schedule == id)
            {
                job->active_schedule.reset();
            }
            scheduled_tasks_.erase(iterator);
            continue;
        }

        if (job != nullptr && job->state == SimulationJobState::Scheduled)
        {
            job->desc.lane = iterator->second.lane;
            job->state = SimulationJobState::Pending;
            if (job->active_schedule == id)
            {
                job->active_schedule.reset();
            }
        }
        scheduled_tasks_.erase(iterator);
        ++result.activated;
    }
    return result;
}

void SimulationRuntime::SetJobStateForTesting(SimulationJobHandle handle, SimulationJobState state)
{
    JobRecord* job = FindJob(handle);
    if (job != nullptr)
    {
        job->state = state;
    }
}

std::optional<ScheduledSimulationTaskId> SimulationRuntime::ActiveScheduleForTesting(SimulationJobHandle handle) const
{
    const JobRecord* job = FindJob(handle);
    if (job == nullptr)
    {
        return {};
    }
    return job->active_schedule;
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

SimulationRuntime::JobRecord* SimulationRuntime::FindJob(SimulationJobHandle handle)
{
    JobRecord* job = FindJob(handle.id);
    if (job == nullptr || job->handle.generation != handle.generation)
    {
        return nullptr;
    }
    return job;
}

const SimulationRuntime::JobRecord* SimulationRuntime::FindJob(SimulationJobHandle handle) const
{
    const JobRecord* job = FindJob(handle.id);
    if (job == nullptr || job->handle.generation != handle.generation)
    {
        return nullptr;
    }
    return job;
}

std::vector<SimulationJobId> SimulationRuntime::BuildJobWorkList() const
{
    std::vector<SimulationJobId> work_list;
    work_list.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_)
    {
        if (!IsTerminal(job.state) &&
            job.state != SimulationJobState::WaitingForMainThread &&
            job.state != SimulationJobState::Scheduled)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](SimulationJobId left, SimulationJobId right) {
        return left.value < right.value;
    });
    return work_list;
}

std::vector<SimulationJobId> SimulationRuntime::BuildMainThreadWorkList() const
{
    std::vector<SimulationJobId> work_list;
    for (const auto& [id, job] : jobs_)
    {
        if (job.state == SimulationJobState::WaitingForMainThread)
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

foundation::Result<void> SimulationRuntime::ValidateJobMetadata(const SimulationJobDesc& desc) const
{
    if (!desc.zone.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_zone", "simulation job must reference a valid zone"));
    }
    if (desc.work_units == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.empty_job", "simulation job must contain work units"));
    }
    if (desc.lane == SimulationLane::Object && !desc.subject.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_subject", "object-lane simulation job must reference a subject"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> SimulationRuntime::ValidateProposalBatch(const SimulationProposalBatch& batch) const
{
    const JobRecord* job = FindJob(batch.job);
    if (job == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.stale_handle", "proposal batch must reference an existing job handle"));
    }
    if (batch.zone != job->desc.zone)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.proposal_zone_mismatch", "proposal batch zone must match source job"));
    }
    if (batch.source_revision != job->desc.source_revision)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.stale_revision", "proposal batch source revision must match source job"));
    }
    if (job->state != SimulationJobState::Completed && job->state != SimulationJobState::WaitingForMainThread)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.job_not_ready_for_publish", "only completed or main-thread-ready jobs can publish proposals"));
    }
    for (const SimulationProposal& proposal : batch.proposals)
    {
        if (!proposal.domain.IsValid() || !proposal.kind.IsValid() || !proposal.schema_id.IsValid())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("simulation.invalid_proposal_schema", "proposal must carry domain, kind and schema id"));
        }
        if (proposal.schema_version == 0)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("simulation.invalid_proposal_schema_version", "proposal schema version must be non-zero"));
        }
        if (!proposal.target.IsValid())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("simulation.invalid_proposal_target", "proposal must reference a valid target"));
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> SimulationRuntime::ValidateStepResult(const JobRecord& job, const SimulationStepResult& result) const
{
    if (result.state == SimulationJobState::Pending ||
        result.state == SimulationJobState::Scheduled ||
        result.state == SimulationJobState::Running)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_job_transition", "simulation job step returned a non-terminal scheduler-owned state"));
    }
    if (IsTerminal(job.state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.job_terminal", "terminal simulation jobs cannot execute steps"));
    }
    if (!result.proposals.proposals.empty())
    {
        if (result.state != SimulationJobState::Completed && result.state != SimulationJobState::WaitingForMainThread)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("simulation.job_not_ready_for_publish", "job step proposals require a publishable result state"));
        }
        for (const SimulationProposal& proposal : result.proposals.proposals)
        {
            if (!proposal.domain.IsValid() || !proposal.kind.IsValid() || !proposal.schema_id.IsValid())
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("simulation.invalid_proposal_schema", "proposal must carry domain, kind and schema id"));
            }
            if (proposal.schema_version == 0)
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("simulation.invalid_proposal_schema_version", "proposal schema version must be non-zero"));
            }
            if (!proposal.target.IsValid())
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("simulation.invalid_proposal_target", "proposal must reference a valid target"));
            }
        }
    }
    return foundation::Result<void>::Success();
}

void SimulationRuntime::PruneTerminalJobs()
{
    if (options_.max_terminal_jobs == 0)
    {
        return;
    }

    std::vector<SimulationJobId> terminal;
    for (const auto& [id, job] : jobs_)
    {
        if (IsTerminal(job.state) && !HasPendingProposal(id))
        {
            terminal.push_back(id);
        }
    }
    std::sort(terminal.begin(), terminal.end(), [](SimulationJobId left, SimulationJobId right) {
        return left.value < right.value;
    });
    while (terminal.size() > options_.max_terminal_jobs)
    {
        jobs_.erase(terminal.front());
        terminal.erase(terminal.begin());
    }
}

void SimulationRuntime::PruneExpiredMemoryEvents()
{
    if (options_.max_memory_events == 0 || memory_events_.size() < options_.max_memory_events)
    {
        return;
    }
    for (const WorldMemoryEventId id : BuildMemoryWorkList())
    {
        if (memory_events_.size() < options_.max_memory_events)
        {
            break;
        }
        const auto iterator = memory_events_.find(id);
        if (iterator != memory_events_.end() && iterator->second.expired)
        {
            memory_events_.erase(iterator);
        }
    }
}

bool SimulationRuntime::HasPendingProposal(SimulationJobId id) const
{
    return std::any_of(proposal_batches_.begin(), proposal_batches_.end(), [id](const SimulationProposalBatch& batch) {
        return batch.job.id == id;
    });
}

} // namespace epidemic::runtime::simulation
