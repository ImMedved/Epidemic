#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <new>
#include <tuple>
#include <utility>

namespace epidemic::runtime::streaming
{
namespace
{
constexpr float kRequestedProgress = 0.0f;
constexpr float kQueuedProgress = 0.10f;
constexpr float kLoadingProgress = 0.30f;
constexpr float kLoadedProgress = 0.75f;
constexpr float kActivatingProgress = 0.90f;
constexpr float kResidentProgress = 0.95f;
constexpr float kActiveProgress = 1.0f;
constexpr float kUnloadingProgress = 0.90f;
constexpr float kTerminalProgress = 1.0f;

[[nodiscard]] foundation::Result<void> StreamingFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> StreamingFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}

template <typename TInteger>
void CommitMonotonicCounter(TInteger& next_value) noexcept
{
    if (next_value == 0)
    {
        return;
    }
    if (next_value == std::numeric_limits<TInteger>::max())
    {
        next_value = 0;
    }
    else
    {
        ++next_value;
    }
}

[[nodiscard]] bool IsLocalTransitionFailure(const foundation::Error& error) noexcept
{
    return error.HasCode("streaming.completion_sequence_overflow") || error.HasCode("streaming.revision_overflow") ||
           error.HasCode("streaming.unsupported_target") || error.HasCode("streaming.invalid_chunk");
}
} // namespace

foundation::Result<void> InMemoryResidencyController::ActivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before activation");
    }
    const auto iterator = chunk_states_.find(chunk);
    if (iterator != chunk_states_.end())
    {
        if (iterator->second == StreamingState::Active)
        {
            return foundation::Result<void>::Success();
        }
        if (iterator->second != StreamingState::Deactivating && iterator->second != StreamingState::Unloaded)
        {
            return StreamingFailure("streaming.invalid_residency_transition", "chunk cannot be activated from its current residency state");
        }
        iterator->second = StreamingState::Active;
        return foundation::Result<void>::Success();
    }
    try
    {
        chunk_states_.emplace(chunk, StreamingState::Active);
    }
    catch (...)
    {
        return StreamingFailure("streaming.allocation_failed", "residency state could not be published");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::DeactivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before deactivation");
    }
    const auto iterator = chunk_states_.find(chunk);
    if (iterator == chunk_states_.end())
    {
        return StreamingFailure("streaming.invalid_residency_transition", "unknown chunk cannot be deactivated");
    }
    if (iterator->second == StreamingState::Deactivating)
    {
        return foundation::Result<void>::Success();
    }
    if (iterator->second != StreamingState::Active)
    {
        return StreamingFailure("streaming.invalid_residency_transition", "chunk can only be deactivated from Active state");
    }
    iterator->second = StreamingState::Deactivating;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::UnloadChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before unloading");
    }
    const auto iterator = chunk_states_.find(chunk);
    if (iterator == chunk_states_.end())
    {
        return StreamingFailure("streaming.invalid_residency_transition", "unknown chunk cannot be unloaded");
    }
    if (iterator->second == StreamingState::Unloaded)
    {
        return foundation::Result<void>::Success();
    }
    if (iterator->second != StreamingState::Deactivating)
    {
        return StreamingFailure("streaming.invalid_residency_transition", "chunk can only be unloaded after deactivation");
    }
    iterator->second = StreamingState::Unloaded;
    return foundation::Result<void>::Success();
}

StreamingState InMemoryResidencyController::GetResidencyState(ChunkId chunk) const
{
    const auto iterator = chunk_states_.find(chunk);
    return iterator == chunk_states_.end() ? StreamingState::NotRequested : iterator->second;
}

StreamingRuntime::StreamingRuntime(StreamingDependencies dependencies,
                                   IStreamingPriorityResolver* priority_resolver)
    : dependencies_(std::move(dependencies)), priority_resolver_(priority_resolver)
{
}

foundation::Result<StreamingDemandHandle> StreamingRuntime::Request(const StreamingTarget& target, StreamingPriorityClass priority)
{
    if (lifecycle_ != Lifecycle::Running)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.shutdown", "streaming runtime is not accepting new requests");
    }
    if (!IsValidPriority(priority))
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.invalid_priority", "streaming priority is outside the enum domain");
    }
    const auto chunk_target = GetChunkTarget(target);
    if (!chunk_target)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.unsupported_target", "reference streaming runtime only executes chunk targets");
    }
    const ChunkId chunk = *chunk_target;
    if (!chunk.IsValid())
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.invalid_chunk", "chunk id must be valid before requesting streaming");
    }

    StreamingPriorityClass resolved_priority = priority;
    try
    {
        if (resolved_priority == StreamingPriorityClass::Normal && dependencies_.priority_provider)
        {
            resolved_priority = dependencies_.priority_provider->GetPriority(target);
        }
        else if (resolved_priority == StreamingPriorityClass::Normal && priority_resolver_ != nullptr)
        {
            resolved_priority = priority_resolver_->ResolvePriority(chunk);
        }
    }
    catch (...)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.priority_exception", "streaming priority provider threw an exception");
    }
    if (!IsValidPriority(resolved_priority))
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.invalid_priority", "streaming priority provider returned an invalid enum value");
    }

    RequestRecord* record = nullptr;
    std::optional<StreamingRequestId> predecessor;
    const auto existing = chunk_to_request_.find(chunk);
    if (existing != chunk_to_request_.end())
    {
        RequestRecord* existing_record = FindRequest(existing->second);
        if (existing_record != nullptr && existing_record->state == StreamingState::Unloading)
        {
            if (existing_record->successor.has_value())
            {
                RequestRecord* successor = FindRequest(*existing_record->successor);
                if (successor == nullptr || successor->state != StreamingState::WaitingForPredecessor ||
                    successor->predecessor != existing_record->request.id)
                {
                    return StreamingFailureValue<StreamingDemandHandle>("streaming.invalid_successor_state", "unloading request successor link is inconsistent");
                }
                record = successor;
            }
            else
            {
                predecessor = existing_record->request.id;
            }
        }
        else if (existing_record != nullptr && !IsTerminal(existing_record->state))
        {
            record = existing_record;
        }
    }

    if (!CanAllocateMonotonicId(next_demand_value_) || !CanAllocateMonotonicId(next_demand_generation_))
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.id_overflow", "streaming demand id allocator is exhausted");
    }

    if (record == nullptr)
    {
        if (!CanAllocateMonotonicId(next_request_value_) || !CanAllocateMonotonicId(next_request_generation_))
        {
            return StreamingFailureValue<StreamingDemandHandle>("streaming.id_overflow", "streaming request id allocator is exhausted");
        }

        const StreamingRequestId request_id{next_request_value_};
        const StreamingRequestHandle request_handle{request_id, next_request_generation_};
        const StreamingDemandHandle demand{StreamingDemandId{next_demand_value_}, request_handle, next_demand_generation_};

        StreamingRequest request{};
        request.id = request_id;
        request.handle = request_handle;
        request.target = target;
        request.priority = resolved_priority;
        request.demand_count = 1;
        request.cancellation.generation = request_handle.generation;
        request.load_plan.steps = {
            StreamingPlanStepRecord{StreamingPlanStep::ResolveTarget, 64, 0},
            StreamingPlanStepRecord{StreamingPlanStep::PrepareData, 256, 0},
            StreamingPlanStepRecord{StreamingPlanStep::PrepareResources, 512, 0},
            StreamingPlanStepRecord{StreamingPlanStep::Commit, 128, 0}};

        if (dependencies_.data_source)
        {
            try
            {
                const auto plan = dependencies_.data_source->BuildLoadPlan(request);
                if (!plan)
                {
                    return foundation::Result<StreamingDemandHandle>::Failure(plan.GetError());
                }
                request.load_plan = plan.Value();
            }
            catch (...)
            {
                return StreamingFailureValue<StreamingDemandHandle>("streaming.data_source_exception", "streaming data source threw while building load plan");
            }
        }
        const auto valid_plan = ValidatePlan(request.load_plan);
        if (!valid_plan)
        {
            return foundation::Result<StreamingDemandHandle>::Failure(valid_plan.GetError());
        }

        RequestRecord prepared{};
        prepared.request = std::move(request);
        prepared.state = predecessor ? StreamingState::WaitingForPredecessor : StreamingState::Requested;
        prepared.progress = kRequestedProgress;
        prepared.revision = 1;
        prepared.active_demands = 1;
        prepared.max_priority = resolved_priority;
        prepared.predecessor = predecessor;
        try
        {
            prepared.demands.emplace(demand.id, DemandRecord{demand, resolved_priority, true});
        }
        catch (...)
        {
            return StreamingFailureValue<StreamingDemandHandle>("streaming.allocation_failed", "streaming demand could not be prepared");
        }

        bool inserted_request = false;
        bool inserted_chunk_mapping = false;
        bool inserted_chunk_state = false;
        try
        {
            if (fail_next_request_publication_for_testing_)
            {
                fail_next_request_publication_for_testing_ = false;
                throw std::bad_alloc{};
            }
            auto [request_it, inserted] = requests_.emplace(request_id, std::move(prepared));
            if (!inserted)
            {
                return StreamingFailureValue<StreamingDemandHandle>("streaming.duplicate_request_id", "allocated streaming request id already exists");
            }
            inserted_request = true;
            (void)request_it;

            if (!predecessor)
            {
                // Prepare the allocating state entry before overwriting an existing
                // chunk mapping, so rollback never has to reconstruct the previous key.
                const auto state_it = chunk_states_.find(chunk);
                if (state_it == chunk_states_.end())
                {
                    chunk_states_.emplace(chunk, StreamingState::Requested);
                    inserted_chunk_state = true;
                }
                else
                {
                    state_it->second = StreamingState::Requested;
                }
                const auto mapping_it = chunk_to_request_.find(chunk);
                if (mapping_it == chunk_to_request_.end())
                {
                    chunk_to_request_.emplace(chunk, request_id);
                    inserted_chunk_mapping = true;
                }
                else
                {
                    mapping_it->second = request_id;
                }
            }
        }
        catch (...)
        {
            if (inserted_chunk_state)
            {
                chunk_states_.erase(chunk);
            }
            if (inserted_chunk_mapping)
            {
                chunk_to_request_.erase(chunk);
            }
            if (inserted_request)
            {
                requests_.erase(request_id);
            }
            return StreamingFailureValue<StreamingDemandHandle>("streaming.allocation_failed", "streaming request publication failed");
        }

        if (predecessor)
        {
            RequestRecord* predecessor_record = FindRequest(*predecessor);
            if (predecessor_record == nullptr || predecessor_record->successor.has_value())
            {
                requests_.erase(request_id);
                return StreamingFailureValue<StreamingDemandHandle>("streaming.invalid_predecessor_state", "unloading predecessor changed before successor publication");
            }
            predecessor_record->successor = request_id;
        }

        CommitMonotonicCounter(next_request_value_);
        CommitMonotonicCounter(next_request_generation_);
        CommitMonotonicCounter(next_demand_value_);
        CommitMonotonicCounter(next_demand_generation_);
        SaturatingIncrement(statistics_.requested);
        return foundation::Result<StreamingDemandHandle>::Success(demand);
    }

    const StreamingDemandHandle demand{StreamingDemandId{next_demand_value_}, record->request.handle, next_demand_generation_};
    bool inserted_demand = false;
    try
    {
        if (fail_next_demand_publication_for_testing_)
        {
            fail_next_demand_publication_for_testing_ = false;
            throw std::bad_alloc{};
        }
        record->demands.reserve(record->demands.size() + 1);
        const auto [_, inserted] = record->demands.emplace(demand.id, DemandRecord{demand, resolved_priority, true});
        if (!inserted)
        {
            return StreamingFailureValue<StreamingDemandHandle>("streaming.duplicate_demand_id", "allocated streaming demand id already exists");
        }
        inserted_demand = true;
    }
    catch (...)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.allocation_failed", "streaming demand publication failed");
    }

    if (record->state == StreamingState::Deactivating)
    {
        const auto revision = ReserveRevision(*record);
        if (!revision)
        {
            record->demands.erase(demand.id);
            return foundation::Result<StreamingDemandHandle>::Failure(revision.GetError());
        }
        bool inserted_chunk_state = false;
        try
        {
            if (!chunk_states_.contains(chunk))
            {
                chunk_states_.emplace(chunk, record->state);
                inserted_chunk_state = true;
            }
        }
        catch (...)
        {
            record->demands.erase(demand.id);
            return StreamingFailureValue<StreamingDemandHandle>("streaming.allocation_failed", "chunk state could not be prepared before reactivation");
        }

        if (IResidencyController* controller = ResidencyController())
        {
            foundation::Result<void> activated = StreamingFailure("streaming.backend_exception", "residency activation failed unexpectedly");
            try
            {
                activated = controller->ActivateChunk(chunk);
            }
            catch (...)
            {
                activated = StreamingFailure("streaming.residency_exception", "residency controller threw while reactivating chunk");
            }
            if (!activated)
            {
                record->demands.erase(demand.id);
                if (inserted_chunk_state)
                {
                    chunk_states_.erase(chunk);
                }
                return foundation::Result<StreamingDemandHandle>::Failure(activated.GetError());
            }
        }
        record->activation_completed = true;
        record->deactivation_completed = false;
        record->resources_released = false;
        record->residency_unloaded = false;
        CommitState(*record, StreamingState::Resident, kResidentProgress);
        record->request.cancellation.requested = false;
        chunk_states_.find(chunk)->second = record->state;
    }

    if (!inserted_demand)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.internal_error", "streaming demand was not published");
    }
    ++record->active_demands;
    record->request.demand_count = record->active_demands;
    UpdatePriority(*record);
    CommitMonotonicCounter(next_demand_value_);
    CommitMonotonicCounter(next_demand_generation_);
    return foundation::Result<StreamingDemandHandle>::Success(demand);
}

foundation::Result<void> StreamingRuntime::ReleaseDemand(StreamingDemandHandle demand)
{
    if (!demand.IsValid())
    {
        return StreamingFailure("streaming.invalid_handle", "streaming demand handle must be valid");
    }
    RequestRecord* record = FindByDemand(demand);
    if (record == nullptr)
    {
        return StreamingFailure("streaming.stale_handle", "streaming demand handle is unknown or stale");
    }
    const auto demand_it = record->demands.find(demand.id);
    if (demand_it == record->demands.end() || !demand_it->second.active)
    {
        return StreamingFailure("streaming.reference_underflow", "streaming demand has already been released");
    }
    if (record->active_demands == 0)
    {
        return StreamingFailure("streaming.reference_underflow", "streaming demand reference count is already zero");
    }

    if (record->active_demands == 1)
    {
        return ReleaseLastDemand(*record, demand);
    }

    const auto revision = ReserveRevision(*record);
    if (!revision)
    {
        return revision;
    }
    record->demands.erase(demand_it);
    --record->active_demands;
    record->request.demand_count = record->active_demands;
    ++record->revision;
    UpdatePriority(*record);
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::ReleaseLastDemand(RequestRecord& record, StreamingDemandHandle demand)
{
    const auto erase_demand = [&]() noexcept {
        record.demands.erase(demand.id);
        record.active_demands = 0;
        record.request.demand_count = 0;
        UpdatePriority(record);
    };

    foundation::Result<void> transition = foundation::Result<void>::Success();
    switch (record.state)
    {
    case StreamingState::Requested:
    case StreamingState::Queued:
    {
        const auto prepared = PrepareTerminalTransition(record);
        if (!prepared)
        {
            return foundation::Result<void>::Failure(prepared.GetError());
        }
        CommitTerminal(record, StreamingState::Cancelled, prepared.Value());
        MarkCancellationAccepted(record);
        PublishChunkStateIfCurrent(record);
        break;
    }
    case StreamingState::WaitingForPredecessor:
    {
        const auto prepared = PrepareTerminalTransition(record);
        if (!prepared)
        {
            return foundation::Result<void>::Failure(prepared.GetError());
        }
        CommitTerminal(record, StreamingState::Cancelled, prepared.Value());
        ClearGraphLinks(record);
        record.predecessor.reset();
        record.successor.reset();
        MarkCancellationAccepted(record);
        break;
    }
    case StreamingState::Loading:
    case StreamingState::Loaded:
    case StreamingState::Activating:
    case StreamingState::RollbackFailed:
        transition = Rollback(record, StreamingState::Cancelled);
        if (!transition)
        {
            if (!IsLocalTransitionFailure(transition.GetError()))
            {
                const auto revision = ReserveRevision(record);
                if (revision)
                {
                    CommitState(record, StreamingState::RollbackFailed, record.progress);
                    SaturatingIncrement(statistics_.rollback_failed);
                    MarkCancellationAccepted(record);
                    PublishChunkStateIfCurrent(record);
                }
            }
            return transition;
        }
        MarkCancellationAccepted(record);
        break;
    case StreamingState::Resident:
    case StreamingState::Active:
        transition = BeginUnload(record);
        if (!transition)
        {
            return transition;
        }
        MarkCancellationAccepted(record);
        break;
    case StreamingState::Deactivating:
    case StreamingState::Unloading:
        MarkCancellationAccepted(record);
        break;
    case StreamingState::NotRequested:
    case StreamingState::Unloaded:
    case StreamingState::Cancelled:
    case StreamingState::Failed:
        break;
    }

    erase_demand();
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::Shutdown()
{
    if (lifecycle_ == Lifecycle::Shutdown)
    {
        return foundation::Result<void>::Success();
    }
    lifecycle_ = Lifecycle::ShuttingDown;

    // Release live ownership one handle at a time. A failed last-release retains its
    // handle and phase so the next Shutdown call continues from the same operation.
    std::vector<StreamingDemandHandle> demands;
    try
    {
        for (const auto& [id, record] : requests_)
        {
            (void)id;
            demands.reserve(demands.size() + record.demands.size());
            for (const auto& [demand_id, demand] : record.demands)
            {
                (void)demand_id;
                if (demand.active)
                {
                    demands.push_back(demand.handle);
                }
            }
        }
    }
    catch (...)
    {
        return StreamingFailure("streaming.shutdown_allocation_failed", "streaming shutdown work list allocation failed");
    }
    std::sort(demands.begin(), demands.end(), [](const StreamingDemandHandle& left, const StreamingDemandHandle& right) {
        return std::tie(left.request.id.value, left.id.value) < std::tie(right.request.id.value, right.id.value);
    });
    for (const StreamingDemandHandle demand : demands)
    {
        if (FindByDemand(demand) == nullptr)
        {
            continue;
        }
        const auto released = ReleaseDemand(demand);
        if (!released)
        {
            return released;
        }
    }

    std::vector<StreamingRequestId> ids;
    try
    {
        ids.reserve(requests_.size());
        for (const auto& [id, record] : requests_)
        {
            (void)record;
            ids.push_back(id);
        }
    }
    catch (...)
    {
        return StreamingFailure("streaming.shutdown_allocation_failed", "streaming shutdown request list allocation failed");
    }
    std::sort(ids.begin(), ids.end(), [](StreamingRequestId left, StreamingRequestId right) { return left.value < right.value; });

    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        for (const StreamingRequestId id : ids)
        {
            RequestRecord* record = FindRequest(id);
            if (record == nullptr || record->active_demands != 0)
            {
                continue;
            }
            const StreamingState before = record->state;
            foundation::Result<void> result = foundation::Result<void>::Success();
            switch (record->state)
            {
            case StreamingState::Requested:
            case StreamingState::Queued:
            case StreamingState::Loading:
            case StreamingState::Loaded:
            case StreamingState::Activating:
            case StreamingState::Resident:
            case StreamingState::Active:
            case StreamingState::WaitingForPredecessor:
            case StreamingState::RollbackFailed:
                result = CancelRequest(record->request.handle);
                break;
            case StreamingState::Deactivating:
            case StreamingState::Unloading:
                result = AdvanceRequest(*record, RuntimeBudget{});
                break;
            case StreamingState::NotRequested:
            case StreamingState::Unloaded:
            case StreamingState::Cancelled:
            case StreamingState::Failed:
                break;
            }
            if (!result)
            {
                return result;
            }
            if (record->state != before)
            {
                progressed = true;
            }
        }
    }

    for (const auto& [id, record] : requests_)
    {
        (void)id;
        if (record.active_demands != 0 ||
            (IsLiveWork(record.state) && record.state != StreamingState::WaitingForPredecessor))
        {
            return StreamingFailure("streaming.shutdown_incomplete", "streaming shutdown did not drain all live work");
        }
    }
    lifecycle_ = Lifecycle::Shutdown;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::CancelRequest(StreamingRequestHandle request)
{
    if (!request.IsValid())
    {
        return StreamingFailure("streaming.invalid_handle", "streaming request handle must be valid");
    }
    RequestRecord* record = FindRequest(request.id);
    if (record == nullptr || record->request.handle.generation != request.generation)
    {
        return StreamingFailure("streaming.stale_handle", "streaming request handle generation is stale");
    }
    if (record->active_demands != 0)
    {
        return StreamingFailure("streaming.active_demands", "streaming request cancellation requires all demands to be released first");
    }

    foundation::Result<void> result = foundation::Result<void>::Success();
    switch (record->state)
    {
    case StreamingState::Requested:
    case StreamingState::Queued:
    {
        const auto prepared = PrepareTerminalTransition(*record);
        if (!prepared)
        {
            return foundation::Result<void>::Failure(prepared.GetError());
        }
        CommitTerminal(*record, StreamingState::Cancelled, prepared.Value());
        PublishChunkStateIfCurrent(*record);
        break;
    }
    case StreamingState::Loading:
    case StreamingState::Loaded:
    case StreamingState::Activating:
    case StreamingState::RollbackFailed:
        result = Rollback(*record, StreamingState::Cancelled);
        if (!result)
        {
            if (!IsLocalTransitionFailure(result.GetError()))
            {
                const auto revision = ReserveRevision(*record);
                if (revision)
                {
                    CommitState(*record, StreamingState::RollbackFailed, record->progress);
                    SaturatingIncrement(statistics_.rollback_failed);
                }
            }
            MarkCancellationAccepted(*record);
            return result;
        }
        break;
    case StreamingState::Resident:
    case StreamingState::Active:
        result = BeginUnload(*record);
        if (!result)
        {
            return result;
        }
        break;
    case StreamingState::WaitingForPredecessor:
        result = CancelWaitingForPredecessor(*record);
        if (!result)
        {
            return result;
        }
        break;
    case StreamingState::Deactivating:
    case StreamingState::Unloading:
    case StreamingState::Unloaded:
    case StreamingState::Cancelled:
    case StreamingState::Failed:
    case StreamingState::NotRequested:
        break;
    }
    MarkCancellationAccepted(*record);
    return foundation::Result<void>::Success();
}

StreamingState StreamingRuntime::GetChunkState(ChunkId chunk) const
{
    const auto iterator = chunk_states_.find(chunk);
    return iterator == chunk_states_.end() ? StreamingState::NotRequested : iterator->second;
}

void StreamingRuntime::SetBudget(const StreamingBudget& budget)
{
    budget_ = budget;
    if (budget_.cpu_budget.count() < 0)
    {
        // Existing public API is void; normalize invalid negative durations to the
        // documented zero/unlimited sentinel instead of publishing contradictory state.
        budget_.cpu_budget = std::chrono::microseconds{0};
    }
}

StreamingTickResult StreamingRuntime::Tick()
{
    StreamingTickResult result{};
    std::vector<StreamingRequestId> work_list;
    try
    {
        work_list = BuildWorkList();
        result.failures.reserve(work_list.size());
    }
    catch (...)
    {
        SaturatingIncrement(statistics_.failed);
        return result;
    }

    const std::size_t max_requests = budget_.max_requests == 0 ? work_list.size() : budget_.max_requests;
    const std::size_t max_bytes = budget_.max_bytes == 0 ? std::numeric_limits<std::size_t>::max() : budget_.max_bytes;
    const auto started = std::chrono::steady_clock::now();

    std::size_t processed = 0;
    std::size_t bytes = 0;
    for (const StreamingRequestId request_id : work_list)
    {
        if (processed >= max_requests || bytes >= max_bytes)
        {
            break;
        }
        if (budget_.cpu_budget.count() > 0 && std::chrono::steady_clock::now() - started >= budget_.cpu_budget)
        {
            break;
        }
        RequestRecord* record = FindRequest(request_id);
        if (record == nullptr || !IsLiveWork(record->state))
        {
            continue;
        }
        RuntimeBudget available_budget{};
        available_budget.max_bytes = static_cast<std::uint64_t>(max_bytes - bytes);
        const std::size_t before_bytes = record->processed_bytes;
        foundation::Result<void> advanced = StreamingFailure("streaming.runtime_exception", "streaming request failed unexpectedly");
        try
        {
            advanced = AdvanceRequest(*record, available_budget);
        }
        catch (...)
        {
            advanced = StreamingFailure("streaming.runtime_exception", "streaming request processing threw an unexpected exception");
        }
        const std::size_t request_bytes = record->processed_bytes >= before_bytes ? record->processed_bytes - before_bytes : 0;
        if (request_bytes > std::numeric_limits<std::size_t>::max() - bytes)
        {
            SaturatingIncrement(statistics_.budget_violations);
            result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target,
                foundation::Error::Create("streaming.byte_counter_overflow", "streaming tick byte counter overflowed")});
            break;
        }
        bytes += request_bytes;
        if (!advanced)
        {
            const StreamingState failed_state = record->state;
            // Only a load-step failure requires rollback of staged/committed load work.
            // Residency/unload/revision failures are retryable in-place and must retain
            // their durable phase instead of being converted into a second side effect.
            if (failed_state == StreamingState::Loading)
            {
                const auto rolled_back = Rollback(*record, StreamingState::Failed);
                if (!rolled_back)
                {
                    if (!IsLocalTransitionFailure(rolled_back.GetError()))
                    {
                        const auto revision = ReserveRevision(*record);
                        if (revision)
                        {
                            CommitState(*record, StreamingState::RollbackFailed, record->progress);
                            SaturatingIncrement(statistics_.rollback_failed);
                            if (const auto chunk = GetChunkTarget(record->request.target); chunk && chunk_states_.contains(*chunk))
                            {
                                chunk_states_.find(*chunk)->second = record->state;
                            }
                        }
                    }
                    result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, rolled_back.GetError()});
                }
                else
                {
                    result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, advanced.GetError()});
                }
            }
            else
            {
                result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, advanced.GetError()});
            }
            SaturatingIncrement(statistics_.failed);
        }
        ++processed;
    }
    result.processed_requests = processed;
    result.processed_bytes = bytes;
    CleanupHistoryIfNeeded();
    return result;
}

std::optional<StreamingProgress> StreamingRuntime::GetProgress(StreamingRequestHandle request) const
{
    const RequestRecord* record = FindRequest(request.id);
    if (record == nullptr || record->request.handle.generation != request.generation)
    {
        return std::nullopt;
    }
    return StreamingProgress{record->request.id, record->request.handle, record->request.target, record->state,
                             record->progress, record->active_demands, record->revision, record->processed_bytes};
}

StreamingStatistics StreamingRuntime::GetStatistics() const
{
    return statistics_;
}

std::size_t StreamingRuntime::RecordCount() const noexcept
{
    return requests_.size();
}

void StreamingRuntime::CleanupCompletedRecords(std::size_t max_history)
{
    history_limit_ = max_history;
    CleanupHistoryIfNeeded();
}

bool StreamingRuntime::SetRevisionForTesting(StreamingRequestHandle handle, std::uint64_t value) noexcept
{
    RequestRecord* record = FindRequest(handle.id);
    if (record == nullptr || record->request.handle.generation != handle.generation)
    {
        return false;
    }
    record->revision = value;
    return true;
}

bool StreamingRuntime::IsShuttingDownForTesting() const noexcept
{
    return lifecycle_ == Lifecycle::ShuttingDown;
}

bool StreamingRuntime::IsTerminal(StreamingState state)
{
    return state == StreamingState::Unloaded || state == StreamingState::Cancelled || state == StreamingState::Failed ||
           state == StreamingState::RollbackFailed;
}

bool StreamingRuntime::IsLiveWork(StreamingState state)
{
    return state != StreamingState::NotRequested && !IsTerminal(state);
}

int StreamingRuntime::PriorityRank(StreamingPriorityClass priority)
{
    switch (priority)
    {
    case StreamingPriorityClass::Critical: return 0;
    case StreamingPriorityClass::High: return 1;
    case StreamingPriorityClass::Normal: return 2;
    case StreamingPriorityClass::Low: return 3;
    case StreamingPriorityClass::Background: return 4;
    }
    return 5;
}

bool StreamingRuntime::IsValidPriority(StreamingPriorityClass priority) noexcept
{
    switch (priority)
    {
    case StreamingPriorityClass::Critical:
    case StreamingPriorityClass::High:
    case StreamingPriorityClass::Normal:
    case StreamingPriorityClass::Low:
    case StreamingPriorityClass::Background:
        return true;
    }
    return false;
}

bool StreamingRuntime::IsValidPlanStep(StreamingPlanStep step) noexcept
{
    switch (step)
    {
    case StreamingPlanStep::ResolveTarget:
    case StreamingPlanStep::PrepareData:
    case StreamingPlanStep::PrepareResources:
    case StreamingPlanStep::Commit:
    case StreamingPlanStep::Rollback:
    case StreamingPlanStep::Release:
        return true;
    }
    return false;
}

foundation::Result<void> StreamingRuntime::ValidatePlan(const ProgressiveLoadPlan& plan)
{
    if (plan.cursor != 0)
    {
        return StreamingFailure("streaming.invalid_plan", "streaming load plan must start at cursor zero");
    }
    if (plan.steps.empty())
    {
        return StreamingFailure("streaming.invalid_plan", "streaming load plan must contain at least one step");
    }
    for (const StreamingPlanStepRecord& step : plan.steps)
    {
        if (!IsValidPlanStep(step.step))
        {
            return StreamingFailure("streaming.invalid_plan_step", "streaming load plan contains an invalid step enum value");
        }
        if (step.processed_bytes != 0)
        {
            return StreamingFailure("streaming.invalid_plan", "streaming load plan cannot contain pre-processed bytes");
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::ReserveRevision(const RequestRecord& record, std::uint64_t count)
{
    if (count == 0)
    {
        return foundation::Result<void>::Success();
    }
    if (record.revision > std::numeric_limits<std::uint64_t>::max() - count)
    {
        return StreamingFailure("streaming.revision_overflow", "streaming request revision cannot advance beyond UINT64_MAX");
    }
    return foundation::Result<void>::Success();
}

void StreamingRuntime::CommitState(RequestRecord& record, StreamingState state, float progress) noexcept
{
    if (record.state == state && record.progress == progress)
    {
        return;
    }
    record.state = state;
    record.progress = progress;
    ++record.revision;
}

void StreamingRuntime::SaturatingIncrement(std::uint64_t& counter) noexcept
{
    if (counter != std::numeric_limits<std::uint64_t>::max())
    {
        ++counter;
    }
}

std::optional<ChunkId> StreamingRuntime::GetChunkTarget(const StreamingTarget& target)
{
    if (const auto* chunk = std::get_if<ChunkStreamingTarget>(&target))
    {
        return chunk->chunk;
    }
    return std::nullopt;
}

StreamingRuntime::RequestRecord* StreamingRuntime::FindRequest(StreamingRequestId id)
{
    const auto iterator = requests_.find(id);
    return iterator == requests_.end() ? nullptr : &iterator->second;
}

const StreamingRuntime::RequestRecord* StreamingRuntime::FindRequest(StreamingRequestId id) const
{
    const auto iterator = requests_.find(id);
    return iterator == requests_.end() ? nullptr : &iterator->second;
}

StreamingRuntime::RequestRecord* StreamingRuntime::FindByDemand(StreamingDemandHandle demand)
{
    RequestRecord* record = FindRequest(demand.request.id);
    if (record == nullptr || record->request.handle.generation != demand.request.generation)
    {
        return nullptr;
    }
    const auto iterator = record->demands.find(demand.id);
    if (iterator == record->demands.end() || iterator->second.handle.generation != demand.generation ||
        iterator->second.handle.request != demand.request)
    {
        return nullptr;
    }
    return record;
}

const StreamingRuntime::RequestRecord* StreamingRuntime::FindByDemand(StreamingDemandHandle demand) const
{
    const RequestRecord* record = FindRequest(demand.request.id);
    if (record == nullptr || record->request.handle.generation != demand.request.generation)
    {
        return nullptr;
    }
    const auto iterator = record->demands.find(demand.id);
    if (iterator == record->demands.end() || iterator->second.handle.generation != demand.generation ||
        iterator->second.handle.request != demand.request)
    {
        return nullptr;
    }
    return record;
}

std::vector<StreamingRequestId> StreamingRuntime::BuildWorkList() const
{
    std::vector<StreamingRequestId> work_list;
    work_list.reserve(requests_.size());
    for (const auto& [request_id, record] : requests_)
    {
        if (IsLiveWork(record.state) && !record.predecessor.has_value())
        {
            work_list.push_back(request_id);
        }
    }
    std::sort(work_list.begin(), work_list.end(), [this](StreamingRequestId left, StreamingRequestId right) {
        const RequestRecord* left_record = FindRequest(left);
        const RequestRecord* right_record = FindRequest(right);
        const int left_rank = left_record == nullptr ? 5 : PriorityRank(left_record->max_priority);
        const int right_rank = right_record == nullptr ? 5 : PriorityRank(right_record->max_priority);
        return std::tie(left_rank, left.value) < std::tie(right_rank, right.value);
    });
    return work_list;
}

foundation::Result<void> StreamingRuntime::AdvanceRequest(RequestRecord& record, RuntimeBudget available_budget)
{
    const auto chunk = GetChunkTarget(record.request.target);
    if (!chunk)
    {
        return StreamingFailure("streaming.unsupported_target", "reference streaming runtime only executes chunk targets");
    }

    switch (record.state)
    {
    case StreamingState::Requested:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Queued, kQueuedProgress);
        break;
    }
    case StreamingState::Queued:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Loading, kLoadingProgress);
        break;
    }
    case StreamingState::Loading:
        return ExecutePlanStep(record, available_budget);
    case StreamingState::Loaded:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Activating, kActivatingProgress);
        break;
    }
    case StreamingState::Activating:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        if (!chunk_states_.contains(*chunk))
        {
            try
            {
                chunk_states_.emplace(*chunk, record.state);
            }
            catch (...)
            {
                return StreamingFailure("streaming.allocation_failed", "chunk state could not be prepared before activation");
            }
        }
        try
        {
            if (IResidencyController* controller = ResidencyController())
            {
                const auto activated = controller->ActivateChunk(*chunk);
                if (!activated) return activated;
            }
        }
        catch (...)
        {
            return StreamingFailure("streaming.residency_exception", "residency controller threw while activating chunk");
        }
        record.activation_completed = true;
        CommitState(record, StreamingState::Resident, kResidentProgress);
        SaturatingIncrement(statistics_.committed);
        break;
    }
    case StreamingState::Resident:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Active, kActiveProgress);
        break;
    }
    case StreamingState::Deactivating:
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Unloading, kUnloadingProgress);
        break;
    }
    case StreamingState::Unloading:
    {
        const auto prepared = PrepareTerminalTransition(record);
        if (!prepared) return foundation::Result<void>::Failure(prepared.GetError());
        RequestRecord* successor = record.successor ? FindRequest(*record.successor) : nullptr;
        if (successor != nullptr && successor->state == StreamingState::WaitingForPredecessor &&
            successor->active_demands > 0 && successor->predecessor == record.request.id)
        {
            const auto successor_revision = ReserveRevision(*successor);
            if (!successor_revision) return successor_revision;
        }

        if (!record.resources_released)
        {
            try
            {
                if (IStreamingResourceSource* source = ResourceSource())
                {
                    const auto released = source->ReleaseChunkResources(*chunk);
                    if (!released) return released;
                }
            }
            catch (...)
            {
                return StreamingFailure("streaming.resource_exception", "streaming resource source threw while releasing chunk resources");
            }
            record.resources_released = true;
        }
        if (!record.residency_unloaded)
        {
            try
            {
                if (IResidencyController* controller = ResidencyController())
                {
                    const auto unloaded = controller->UnloadChunk(*chunk);
                    if (!unloaded) return unloaded;
                }
            }
            catch (...)
            {
                return StreamingFailure("streaming.residency_exception", "residency controller threw while unloading chunk");
            }
            record.residency_unloaded = true;
        }

        CommitTerminal(record, StreamingState::Unloaded, prepared.Value());
        SaturatingIncrement(statistics_.unloaded);
        if (record.successor)
        {
            successor = FindRequest(*record.successor);
            if (successor != nullptr)
            {
                if (successor->state == StreamingState::WaitingForPredecessor && successor->active_demands > 0 &&
                    successor->predecessor == record.request.id)
                {
                    successor->predecessor.reset();
                    CommitState(*successor, StreamingState::Requested, kRequestedProgress);
                    const auto successor_chunk = GetChunkTarget(successor->request.target);
                    if (successor_chunk)
                    {
                        auto mapping = chunk_to_request_.find(*successor_chunk);
                        if (mapping != chunk_to_request_.end()) mapping->second = successor->request.id;
                        auto state = chunk_states_.find(*successor_chunk);
                        if (state != chunk_states_.end()) state->second = successor->state;
                    }
                }
                else if (successor->predecessor == record.request.id)
                {
                    successor->predecessor.reset();
                }
            }
            record.successor.reset();
        }
        break;
    }
    default:
        break;
    }

    const auto mapping = chunk_to_request_.find(*chunk);
    if (mapping == chunk_to_request_.end() || mapping->second == record.request.id)
    {
        const auto state = chunk_states_.find(*chunk);
        if (state != chunk_states_.end())
        {
            state->second = record.state;
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::ExecutePlanStep(RequestRecord& record, RuntimeBudget available_budget)
{
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        const auto revision = ReserveRevision(record);
        if (!revision) return revision;
        CommitState(record, StreamingState::Loaded, kLoadedProgress);
        PublishChunkStateIfCurrent(record);
        return foundation::Result<void>::Success();
    }

    const std::size_t cursor = record.request.load_plan.cursor;
    StreamingPlanStepRecord step = record.request.load_plan.steps[cursor];
    if (!IsValidPlanStep(step.step))
    {
        return StreamingFailure("streaming.invalid_plan_step", "streaming plan contains an invalid step enum value");
    }
    bool step_completed = true;
    if (dependencies_.data_source)
    {
        try
        {
            const auto executed = dependencies_.data_source->ExecuteStep(record.request, step, available_budget);
            if (!executed) return foundation::Result<void>::Failure(executed.GetError());
            step.processed_bytes = executed.Value().processed_bytes;
            step_completed = executed.Value().completed;
        }
        catch (...)
        {
            return StreamingFailure("streaming.data_source_exception", "streaming data source threw while executing load step");
        }
    }
    else
    {
        try
        {
            if (step.step == StreamingPlanStep::PrepareData && PersistenceSource())
            {
                const auto prepared = PersistenceSource()->PrepareChunkData(record.request);
                if (!prepared) return prepared;
            }
            if (step.step == StreamingPlanStep::ResolveTarget && WorldSource())
            {
                const auto chunk = GetChunkTarget(record.request.target);
                if (!chunk) return StreamingFailure("streaming.unsupported_target", "reference streaming runtime only resolves chunk targets");
                (void)WorldSource()->ResolveRegion(*chunk);
            }
            if (step.step == StreamingPlanStep::PrepareResources && ResourceSource())
            {
                const auto prepared = ResourceSource()->PrepareChunkResources(record.request);
                if (!prepared) return prepared;
            }
        }
        catch (...)
        {
            return StreamingFailure("streaming.extension_exception", "streaming extension threw while executing load step");
        }
    }
    if (!dependencies_.data_source && step.processed_bytes == 0)
    {
        step.processed_bytes = step.estimated_bytes;
    }

    if (available_budget.HasByteLimit() && step.processed_bytes > available_budget.max_bytes)
    {
        SaturatingIncrement(statistics_.budget_violations);
        return StreamingFailure("streaming.step_budget_violation", "streaming plan step exceeded the available byte budget");
    }
    if (step.processed_bytes > std::numeric_limits<std::size_t>::max() - record.processed_bytes ||
        step.processed_bytes > std::numeric_limits<std::size_t>::max() - record.request.load_plan.steps[cursor].processed_bytes)
    {
        SaturatingIncrement(statistics_.budget_violations);
        return StreamingFailure("streaming.byte_counter_overflow", "streaming request byte counter overflowed");
    }

    const std::uint64_t revision_increments = 1u + ((step_completed && cursor + 1u >= record.request.load_plan.steps.size()) ? 1u : 0u);
    const auto revision = ReserveRevision(record, revision_increments);
    if (!revision) return revision;

    if (step_completed && step.step == StreamingPlanStep::Commit && !record.commit_completed && dependencies_.commit_target)
    {
        try
        {
            const auto committed = dependencies_.commit_target->Commit(record.request);
            if (!committed) return committed;
        }
        catch (...)
        {
            return StreamingFailure("streaming.commit_exception", "streaming commit target threw during commit");
        }
        record.commit_completed = true;
    }

    record.processed_bytes += step.processed_bytes;
    record.request.load_plan.steps[cursor].processed_bytes += step.processed_bytes;
    if (step_completed) ++record.request.load_plan.cursor;
    record.progress = std::min(0.70f, kLoadingProgress + (0.40f * static_cast<float>(record.request.load_plan.cursor)) /
                                                    static_cast<float>(std::max<std::size_t>(1, record.request.load_plan.steps.size())));
    ++record.revision;
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        CommitState(record, StreamingState::Loaded, kLoadedProgress);
    }
    if (const auto chunk = GetChunkTarget(record.request.target); chunk && chunk_states_.contains(*chunk))
    {
        chunk_states_.find(*chunk)->second = record.state;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::Rollback(RequestRecord& record, StreamingState terminal_state)
{
    const auto prepared = PrepareTerminalTransition(record);
    if (!prepared)
    {
        return foundation::Result<void>::Failure(prepared.GetError());
    }
    if (!record.rollback_completed && dependencies_.commit_target)
    {
        try
        {
            const auto rolled_back = dependencies_.commit_target->Rollback(record.request);
            if (!rolled_back) return rolled_back;
        }
        catch (...)
        {
            return StreamingFailure("streaming.rollback_exception", "streaming commit target threw during rollback");
        }
        record.rollback_completed = true;
    }
    CommitTerminal(record, terminal_state, prepared.Value());
    if (const auto chunk = GetChunkTarget(record.request.target); chunk && chunk_states_.contains(*chunk))
    {
        chunk_states_.find(*chunk)->second = record.state;
    }
    SaturatingIncrement(statistics_.rolled_back);
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::BeginUnload(RequestRecord& record)
{
    const auto revision = ReserveRevision(record);
    if (!revision) return revision;
    const auto chunk = GetChunkTarget(record.request.target);
    if (!chunk) return StreamingFailure("streaming.unsupported_target", "reference streaming runtime only deactivates chunk targets");
    if (!chunk_states_.contains(*chunk))
    {
        try
        {
            chunk_states_.emplace(*chunk, record.state);
        }
        catch (...)
        {
            return StreamingFailure("streaming.allocation_failed", "chunk state could not be prepared before deactivation");
        }
    }
    if (!record.deactivation_completed)
    {
        try
        {
            if (IResidencyController* controller = ResidencyController())
            {
                const auto deactivated = controller->DeactivateChunk(*chunk);
                if (!deactivated) return deactivated;
            }
        }
        catch (...)
        {
            return StreamingFailure("streaming.residency_exception", "residency controller threw while deactivating chunk");
        }
        record.deactivation_completed = true;
    }
    CommitState(record, StreamingState::Deactivating, kUnloadingProgress);
    chunk_states_.find(*chunk)->second = record.state;
    return foundation::Result<void>::Success();
}

foundation::Result<StreamingRuntime::PreparedTerminalTransition> StreamingRuntime::PrepareTerminalTransition(const RequestRecord& record) const
{
    const auto revision = ReserveRevision(record);
    if (!revision)
    {
        return foundation::Result<PreparedTerminalTransition>::Failure(revision.GetError());
    }
    if (!CanAllocateMonotonicId(next_completion_sequence_))
    {
        return StreamingFailureValue<PreparedTerminalTransition>("streaming.completion_sequence_overflow", "streaming completion sequence allocator is exhausted");
    }
    return foundation::Result<PreparedTerminalTransition>::Success(PreparedTerminalTransition{next_completion_sequence_});
}

void StreamingRuntime::CommitTerminal(RequestRecord& record, StreamingState state, PreparedTerminalTransition prepared) noexcept
{
    CommitState(record, state, kTerminalProgress);
    record.completion_sequence = prepared.completion_sequence;
    CommitMonotonicCounter(next_completion_sequence_);
}

foundation::Result<void> StreamingRuntime::CancelWaitingForPredecessor(RequestRecord& record)
{
    if (record.active_demands != 0)
    {
        return StreamingFailure("streaming.active_demands", "waiting successor cancellation requires all demands to be released first");
    }
    const auto prepared = PrepareTerminalTransition(record);
    if (!prepared)
    {
        return foundation::Result<void>::Failure(prepared.GetError());
    }
    CommitTerminal(record, StreamingState::Cancelled, prepared.Value());
    ClearGraphLinks(record);
    record.predecessor.reset();
    record.successor.reset();
    return foundation::Result<void>::Success();
}

void StreamingRuntime::ClearGraphLinks(RequestRecord& record)
{
    if (record.predecessor)
    {
        if (RequestRecord* predecessor = FindRequest(*record.predecessor); predecessor != nullptr && predecessor->successor == record.request.id)
        {
            predecessor->successor.reset();
        }
    }
    if (record.successor)
    {
        if (RequestRecord* successor = FindRequest(*record.successor); successor != nullptr && successor->predecessor == record.request.id)
        {
            successor->predecessor.reset();
        }
    }
}

IResidencyController* StreamingRuntime::ResidencyController() const noexcept
{
    return dependencies_.residency_controller.get();
}

IStreamingWorldSource* StreamingRuntime::WorldSource() const noexcept
{
    return dependencies_.world_source.get();
}

IStreamingPersistenceSource* StreamingRuntime::PersistenceSource() const noexcept
{
    return dependencies_.persistence_source.get();
}

IStreamingResourceSource* StreamingRuntime::ResourceSource() const noexcept
{
    return dependencies_.resource_source.get();
}

void StreamingRuntime::UpdatePriority(RequestRecord& record) noexcept
{
    StreamingPriorityClass best = StreamingPriorityClass::Background;
    for (const auto& [id, demand] : record.demands)
    {
        (void)id;
        if (demand.active && PriorityRank(demand.priority) < PriorityRank(best))
        {
            best = demand.priority;
        }
    }
    record.max_priority = best;
    record.request.priority = best;
}

void StreamingRuntime::MarkCancellationAccepted(RequestRecord& record) noexcept
{
    record.request.cancellation.requested = true;
    if (!record.cancellation_counted)
    {
        record.cancellation_counted = true;
        SaturatingIncrement(statistics_.cancelled);
    }
}

void StreamingRuntime::PublishChunkStateIfCurrent(const RequestRecord& record) noexcept
{
    const auto chunk = GetChunkTarget(record.request.target);
    if (!chunk)
    {
        return;
    }
    const auto mapping = chunk_to_request_.find(*chunk);
    if (mapping == chunk_to_request_.end() || mapping->second != record.request.id)
    {
        return;
    }
    const auto state = chunk_states_.find(*chunk);
    if (state != chunk_states_.end())
    {
        state->second = record.state;
    }
}

void StreamingRuntime::CleanupHistoryIfNeeded()
{
    if (history_limit_ == 0)
    {
        return;
    }
    while (requests_.size() > history_limit_)
    {
        auto candidate = requests_.end();
        for (auto iterator = requests_.begin(); iterator != requests_.end(); ++iterator)
        {
            if (IsTerminal(iterator->second.state) && iterator->second.active_demands == 0)
            {
                if (candidate == requests_.end() ||
                    std::tie(iterator->second.completion_sequence, iterator->first.value) <
                        std::tie(candidate->second.completion_sequence, candidate->first.value))
                {
                    candidate = iterator;
                }
            }
        }
        if (candidate == requests_.end()) return;
        ClearGraphLinks(candidate->second);
        if (const auto chunk = GetChunkTarget(candidate->second.request.target))
        {
            const auto mapping = chunk_to_request_.find(*chunk);
            if (mapping != chunk_to_request_.end() && mapping->second == candidate->first)
            {
                chunk_to_request_.erase(mapping);
            }
        }
        requests_.erase(candidate);
    }
}
} // namespace epidemic::runtime::streaming
