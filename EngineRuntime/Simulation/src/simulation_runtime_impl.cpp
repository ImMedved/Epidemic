#include "simulation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/Foundation/numeric_validation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <exception>
#include <new>
#include <utility>

namespace epidemic::runtime::simulation
{
namespace
{
[[nodiscard]] bool IsValidAttention(AttentionScore score) noexcept
{
    return std::isfinite(score.value) && score.value >= 0.0f && score.value <= 1.0f;
}

[[nodiscard]] constexpr bool IsValid(SimulationLane value) noexcept
{
    switch (value)
    {
    case SimulationLane::Background:
    case SimulationLane::Zone:
    case SimulationLane::Object:
    case SimulationLane::MainThread:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValid(SimulationZoneState value) noexcept
{
    switch (value)
    {
    case SimulationZoneState::Dormant:
    case SimulationZoneState::Abstract:
    case SimulationZoneState::Scheduled:
    case SimulationZoneState::Relevant:
    case SimulationZoneState::Observed:
    case SimulationZoneState::Active:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValid(SimulationJobState value) noexcept
{
    switch (value)
    {
    case SimulationJobState::Pending:
    case SimulationJobState::Scheduled:
    case SimulationJobState::Running:
    case SimulationJobState::PartiallyComplete:
    case SimulationJobState::WaitingForMainThread:
    case SimulationJobState::Completed:
    case SimulationJobState::Failed:
    case SimulationJobState::Cancelled:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValid(MemoryLifetime value) noexcept
{
    switch (value)
    {
    case MemoryLifetime::Disposable:
    case MemoryLifetime::Temporary:
    case MemoryLifetime::Persistent:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValid(ObservationState value) noexcept
{
    switch (value)
    {
    case ObservationState::Unobserved:
    case ObservationState::Observed:
    case ObservationState::PlayerAffected:
        return true;
    }
    return false;
}

[[nodiscard]] foundation::Error CallbackError(std::string_view operation)
{
    return foundation::Error::Create("simulation.callback_exception", operation);
}

[[nodiscard]] foundation::Error AllocationError(std::string_view operation)
{
    return foundation::Error::Create("simulation.allocation_failed", operation);
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
    if (shutdown_state_ != ShutdownState::Running)
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

    const auto id_value = PeekMonotonicId(next_job_value_, "simulation.job_id_overflow", "simulation job id allocator is exhausted");
    const auto generation = PeekMonotonicId(next_job_generation_, "simulation.job_id_overflow", "simulation job generation allocator is exhausted");
    if (!id_value || !generation)
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.job_id_overflow", "simulation job identity allocator is exhausted"));
    }

    const SimulationJobId id{id_value.Value()};
    const SimulationJobHandle handle{id, generation.Value()};
    if (jobs_.contains(id))
    {
        return foundation::Result<SimulationJobHandle>::Failure(
            foundation::Error::Create("simulation.duplicate_job_id", "allocated simulation job id already exists"));
    }

    JobRecord record{};
    record.desc = metadata;
    record.handle = handle;
    record.state = SimulationJobState::Pending;
    record.executable = std::move(job);
    try
    {
        const auto [_, inserted] = jobs_.emplace(id, std::move(record));
        if (!inserted)
        {
            return foundation::Result<SimulationJobHandle>::Failure(
                foundation::Error::Create("simulation.duplicate_job_id", "allocated simulation job id already exists"));
        }
    }
    catch (...)
    {
        return foundation::Result<SimulationJobHandle>::Failure(AllocationError("failed to publish simulation job"));
    }

    CommitMonotonicId(next_job_value_, id_value.Value());
    CommitMonotonicId(next_job_generation_, generation.Value());
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
        try
        {
            const auto cancelled = job->executable->Cancel();
            if (!cancelled)
            {
                return foundation::Result<void>::Failure(cancelled.GetError());
            }
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(CallbackError("simulation job cancellation callback threw"));
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

    std::vector<SimulationJobId> work_list;
    try
    {
        work_list = BuildJobWorkList();
        result.failures.reserve(work_list.size() + scheduled_tasks_.size());
        result.proposal_batches.reserve(work_list.size());
    }
    catch (...)
    {
        return foundation::Result<SimulationTickResult>::Failure(AllocationError("failed to stage simulation tick work list"));
    }

    if (dependencies_.clock != nullptr)
    {
        SimulationTime now{};
        try
        {
            now = dependencies_.clock->Now();
        }
        catch (...)
        {
            return foundation::Result<SimulationTickResult>::Failure(CallbackError("simulation clock callback threw"));
        }
        const auto scheduled = ExecuteDueWithinBudget(now, budget_);
        for (const ScheduledTaskFailure& failure : scheduled.failures)
        {
            result.failures.push_back(SimulationTickFailure{failure.job, failure.error});
        }
    }

    const std::uint32_t job_limit = budget_.max_jobs == 0 ? static_cast<std::uint32_t>(jobs_.size()) : budget_.max_jobs;
    std::uint32_t work_budget = budget_.max_work_units == 0 ? UINT32_MAX : budget_.max_work_units;

    for (const SimulationJobId id : work_list)
    {
        if (result.processed_jobs >= job_limit || work_budget == 0)
        {
            break;
        }

        JobRecord& job = jobs_.at(id);
        if (IsTerminal(job.state) || job.state == SimulationJobState::WaitingForMainThread || job.state == SimulationJobState::Scheduled)
        {
            continue;
        }

        foundation::Result<SimulationStepResult> step = foundation::Result<SimulationStepResult>::Failure(
            foundation::Error::Create("simulation.callback_exception", "simulation job callback did not run"));
        try
        {
            step = job.executable->ExecuteStep(ToRuntimeBudget(work_budget));
        }
        catch (...)
        {
            result.failures.push_back(SimulationTickFailure{job.handle, CallbackError("simulation job ExecuteStep callback threw")});
            job.state = SimulationJobState::Failed;
            ++result.processed_jobs;
            continue;
        }

        if (!step)
        {
            result.failures.push_back(SimulationTickFailure{job.handle, step.GetError()});
            job.state = SimulationJobState::Failed;
            ++result.processed_jobs;
            continue;
        }

        const SimulationStepResult& step_result = step.Value();
        const auto validated = ValidateStepResult(job, step_result);
        if (!validated || step_result.consumed_work_units > work_budget)
        {
            const foundation::Error error = !validated
                ? validated.GetError()
                : foundation::Error::Create("simulation.work_budget_exceeded", "simulation job consumed more work than granted");
            result.failures.push_back(SimulationTickFailure{job.handle, error});
            job.state = SimulationJobState::Failed;
            ++result.processed_jobs;
            continue;
        }

        std::optional<SimulationStepResult> staged_pending;
        std::optional<SimulationProposalBatch> staged_queue_batch;
        std::optional<SimulationProposalBatch> staged_result_batch;
        try
        {
            if (step_result.state == SimulationJobState::WaitingForMainThread)
            {
                staged_pending = step_result;
                staged_pending->proposals.job = job.handle;
                staged_pending->proposals.zone = job.desc.zone;
                staged_pending->proposals.source_revision = job.desc.source_revision;
            }
            else if (step_result.state == SimulationJobState::Completed && !step_result.proposals.proposals.empty())
            {
                if (options_.max_proposal_batches != 0 && proposal_batches_.size() >= options_.max_proposal_batches)
                {
                    result.failures.push_back(SimulationTickFailure{job.handle,
                        foundation::Error::Create("simulation.proposal_queue_full", "simulation proposal queue is full")});
                    job.state = SimulationJobState::Failed;
                    ++result.processed_jobs;
                    continue;
                }
                staged_queue_batch = step_result.proposals;
                staged_queue_batch->job = job.handle;
                staged_queue_batch->zone = job.desc.zone;
                staged_queue_batch->source_revision = job.desc.source_revision;
                staged_result_batch = *staged_queue_batch;
                proposal_batches_.reserve(proposal_batches_.size() + 1);
            }
        }
        catch (...)
        {
            return foundation::Result<SimulationTickResult>::Failure(AllocationError("failed to stage simulation tick result"));
        }

        // No-fail authoritative commit begins here.
        job.state = step_result.state;
        if (staged_pending)
        {
            job.pending_main_thread_result = std::move(staged_pending);
        }
        if (staged_queue_batch)
        {
            proposal_batches_.push_back(std::move(*staged_queue_batch));
            result.proposal_batches.push_back(std::move(*staged_result_batch));
        }
        work_budget -= step_result.consumed_work_units;
        ++result.processed_jobs;
    }

    PruneTerminalJobs();
    return foundation::Result<SimulationTickResult>::Success(std::move(result));
}

foundation::Result<SimulationTickResult> SimulationRuntime::ProcessMainThreadCommits(std::uint32_t max_jobs)
{
    SimulationTickResult result{};
    std::vector<SimulationJobId> work_list;
    try
    {
        work_list = BuildMainThreadWorkList();
        result.failures.reserve(work_list.size());
        result.proposal_batches.reserve(work_list.size());
    }
    catch (...)
    {
        return foundation::Result<SimulationTickResult>::Failure(AllocationError("failed to stage main-thread simulation work"));
    }

    const std::uint32_t limit = max_jobs == 0 ? UINT32_MAX : max_jobs;
    for (const SimulationJobId id : work_list)
    {
        if (result.processed_jobs >= limit)
        {
            break;
        }
        JobRecord& job = jobs_.at(id);
        if (!job.pending_main_thread_result)
        {
            result.failures.push_back(SimulationTickFailure{job.handle,
                foundation::Error::Create("simulation.pending_result_missing", "waiting job has no pending main-thread result")});
            job.state = SimulationJobState::Failed;
            ++result.processed_jobs;
            continue;
        }

        std::optional<SimulationProposalBatch> queue_batch;
        std::optional<SimulationProposalBatch> result_batch;
        try
        {
            if (!job.pending_main_thread_result->proposals.proposals.empty())
            {
                if (options_.max_proposal_batches != 0 && proposal_batches_.size() >= options_.max_proposal_batches)
                {
                    result.failures.push_back(SimulationTickFailure{job.handle,
                        foundation::Error::Create("simulation.proposal_queue_full", "simulation proposal queue is full")});
                    ++result.processed_jobs;
                    continue; // Waiting state and payload remain retryable.
                }
                queue_batch = job.pending_main_thread_result->proposals;
                queue_batch->job = job.handle;
                queue_batch->zone = job.desc.zone;
                queue_batch->source_revision = job.desc.source_revision;
                result_batch = *queue_batch;
                proposal_batches_.reserve(proposal_batches_.size() + 1);
            }
        }
        catch (...)
        {
            return foundation::Result<SimulationTickResult>::Failure(AllocationError("failed to stage main-thread proposal publication"));
        }

        if (queue_batch)
        {
            proposal_batches_.push_back(std::move(*queue_batch));
            result.proposal_batches.push_back(std::move(*result_batch));
        }
        job.pending_main_thread_result.reset();
        job.state = SimulationJobState::Completed;
        ++result.processed_jobs;
    }

    PruneTerminalJobs();
    return foundation::Result<SimulationTickResult>::Success(std::move(result));
}

foundation::Result<void> SimulationRuntime::Shutdown()
{
    if (shutdown_state_ == ShutdownState::Shutdown)
    {
        return foundation::Result<void>::Success();
    }

    std::vector<SimulationJobId> ids;
    try
    {
        ids.reserve(jobs_.size());
        for (const auto& [id, job] : jobs_)
        {
            (void)job;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [](SimulationJobId left, SimulationJobId right) { return left.value < right.value; });
    }
    catch (...)
    {
        if (shutdown_state_ == ShutdownState::Running)
        {
            return foundation::Result<void>::Failure(AllocationError("failed to prepare shutdown work list"));
        }
        return foundation::Result<void>::Failure(AllocationError("failed to continue shutdown work list"));
    }

    shutdown_state_ = ShutdownState::ShuttingDown;
    std::optional<foundation::Error> first_error;
    for (SimulationJobId id : ids)
    {
        JobRecord* job = FindJob(id);
        if (job == nullptr || IsTerminal(job->state))
        {
            continue;
        }
        const auto cancelled = CancelJob(job->handle);
        if (!cancelled && !first_error)
        {
            first_error = cancelled.GetError();
        }
    }
    if (first_error)
    {
        return foundation::Result<void>::Failure(*first_error);
    }
    scheduled_tasks_.clear();
    shutdown_state_ = ShutdownState::Shutdown;
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

    SimulationZoneState zone_state = SimulationZoneState::Dormant;
    if (dependencies_.relevance != nullptr)
    {
        try
        {
            zone_state = dependencies_.relevance->ClassifyZone(region, score);
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(CallbackError("simulation relevance policy threw"));
        }
        if (!IsValid(zone_state))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("simulation.invalid_zone_state", "relevance policy returned invalid zone state"));
        }
    }

    try
    {
        region_attention_.insert_or_assign(region, RegionAttentionRecord{score, zone_state});
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(AllocationError("failed to publish region attention"));
    }
    return foundation::Result<void>::Success();
}

AttentionScore SimulationRuntime::GetRegionAttention(RegionId region) const
{
    const auto iterator = region_attention_.find(region);
    return iterator == region_attention_.end() ? AttentionScore{} : iterator->second.score;
}

SimulationZoneState SimulationRuntime::GetRegionZoneState(RegionId region) const
{
    const auto iterator = region_attention_.find(region);
    return iterator == region_attention_.end() ? SimulationZoneState::Dormant : iterator->second.zone_state;
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
    if (!IsValid(event.lifetime) || !IsValid(event.observation))
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.invalid_memory_enum", "memory event enum value is invalid"));
    }
    if (event.lifetime != MemoryLifetime::Persistent && !event.ttl.IsZero() && !CheckedAdd(event.happened_at, event.ttl))
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_expiration_overflow", "memory event expiration is not representable"));
    }
    if (options_.max_memory_events != 0 && memory_events_.size() >= options_.max_memory_events)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_store_full", "world memory event store capacity is exhausted"));
    }

    WorldMemoryEventId id = event.id;
    if (!id.IsValid())
    {
        const auto candidate = PeekMonotonicId(next_memory_value_, "simulation.memory_event_id_overflow", "memory event id allocator is exhausted");
        if (!candidate)
        {
            return foundation::Result<WorldMemoryEventId>::Failure(candidate.GetError());
        }
        id = WorldMemoryEventId{candidate.Value()};
    }
    if (memory_events_.contains(id))
    {
        return foundation::Result<WorldMemoryEventId>::Failure(
            foundation::Error::Create("simulation.memory_event_already_exists", "memory event id already exists"));
    }

    WorldMemoryEvent stored = event;
    stored.id = id;
    try
    {
        const auto [_, inserted] = memory_events_.emplace(id, std::move(stored));
        if (!inserted)
        {
            return foundation::Result<WorldMemoryEventId>::Failure(
                foundation::Error::Create("simulation.memory_event_already_exists", "memory event id already exists"));
        }
    }
    catch (...)
    {
        return foundation::Result<WorldMemoryEventId>::Failure(AllocationError("failed to store world memory event"));
    }

    if (!event.id.IsValid())
    {
        CommitMonotonicId(next_memory_value_, id.value);
    }
    else if (next_memory_value_ != 0 && id.value >= next_memory_value_)
    {
        next_memory_value_ = id.value == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value + 1;
    }
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

        const auto expires_at = CheckedAdd(event.happened_at, event.ttl);
        if (expires_at && expires_at->ticks <= now.ticks)
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
    if (fact.fact_type == 0 || fact.observed_at.ticks < 0 || !fact.subject.IsValid() || !fact.zone.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_fact", "abstract fact fields are invalid"));
    }
    const auto next_revision = CheckedRevisionIncrement(fact_revision_);
    if (!next_revision)
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
    stored.revision = *next_revision;
    try
    {
        facts_.push_back(stored);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(AllocationError("failed to store abstract fact"));
    }
    fact_revision_ = *next_revision;
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
    try
    {
        const auto committed = dependencies_.commit_target->Commit(proposal_batches_.front());
        if (!committed)
        {
            return foundation::Result<void>::Failure(committed.GetError());
        }
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(CallbackError("simulation commit target threw"));
    }
    proposal_batches_.erase(proposal_batches_.begin());
    PruneTerminalJobs();
    return foundation::Result<void>::Success();
}

foundation::Result<ScheduledSimulationTaskId> SimulationRuntime::Schedule(ScheduledSimulationTask task)
{
    if (!IsValid(task.lane))
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.invalid_lane", "scheduled task lane is invalid"));
    }
    if (task.due_at.ticks < 0)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.invalid_due_time", "scheduled task due time must not be negative"));
    }
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
    if (job->active_schedule || job->state == SimulationJobState::Scheduled)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(
            foundation::Error::Create("simulation.job_already_scheduled", "simulation job already has an active scheduled task"));
    }
    const auto candidate = PeekMonotonicId(next_task_value_, "simulation.task_id_overflow", "scheduled simulation task id allocator is exhausted");
    if (!candidate)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(candidate.GetError());
    }
    const ScheduledSimulationTaskId id{candidate.Value()};
    task.id = id;
    try
    {
        const auto [_, inserted] = scheduled_tasks_.emplace(id, task);
        if (!inserted)
        {
            return foundation::Result<ScheduledSimulationTaskId>::Failure(
                foundation::Error::Create("simulation.duplicate_task_id", "allocated scheduled simulation task id already exists"));
        }
    }
    catch (...)
    {
        return foundation::Result<ScheduledSimulationTaskId>::Failure(AllocationError("failed to publish scheduled simulation task"));
    }
    CommitMonotonicId(next_task_value_, candidate.Value());
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

void SimulationRuntime::SetNextJobIdentityForTesting(std::uint64_t id, std::uint32_t generation) noexcept
{
    next_job_value_ = id;
    next_job_generation_ = generation;
}

void SimulationRuntime::SetNextMemoryIdForTesting(std::uint64_t id) noexcept
{
    next_memory_value_ = id;
}

void SimulationRuntime::SetNextTaskIdForTesting(std::uint64_t id) noexcept
{
    next_task_value_ = id;
}

void SimulationRuntime::SetFactRevisionForTesting(std::uint64_t revision) noexcept
{
    fact_revision_ = revision;
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
            job.state != SimulationJobState::WaitingForMainThread)
        {
            // Scheduled jobs are included so a due occurrence activated earlier in the same Tick
            // can execute without allocating a second work list after scheduler mutation.
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
    if (!IsValid(desc.lane))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_lane", "simulation job lane is invalid"));
    }
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
    if (!IsValid(result.state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("simulation.invalid_job_state", "simulation job step returned invalid state enum"));
    }
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
