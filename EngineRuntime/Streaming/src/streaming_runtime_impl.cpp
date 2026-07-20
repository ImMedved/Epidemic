#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <tuple>

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
} // namespace

foundation::Result<void> InMemoryResidencyController::ActivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before activation");
    }
    chunk_states_[chunk] = StreamingState::Active;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::DeactivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before deactivation");
    }
    chunk_states_[chunk] = StreamingState::Deactivating;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::UnloadChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return StreamingFailure("streaming.invalid_chunk", "chunk id must be valid before unloading");
    }
    chunk_states_[chunk] = StreamingState::Unloaded;
    return foundation::Result<void>::Success();
}

StreamingState InMemoryResidencyController::GetResidencyState(ChunkId chunk) const
{
    const auto iterator = chunk_states_.find(chunk);
    return iterator == chunk_states_.end() ? StreamingState::NotRequested : iterator->second;
}

StreamingRuntime::StreamingRuntime(StreamingDependencies dependencies,
                                   IStreamingPriorityResolver* priority_resolver)
    : dependencies_(std::move(dependencies)),
      priority_resolver_(priority_resolver)
{
}

foundation::Result<StreamingDemandHandle> StreamingRuntime::Request(const StreamingTarget& target, StreamingPriorityClass priority)
{
    if (!accepting_requests_)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.shutdown", "streaming runtime is not accepting new requests");
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
    if (resolved_priority == StreamingPriorityClass::Normal && dependencies_.priority_provider)
    {
        resolved_priority = dependencies_.priority_provider->GetPriority(target);
    }
    else if (resolved_priority == StreamingPriorityClass::Normal && priority_resolver_ != nullptr)
    {
        resolved_priority = priority_resolver_->ResolvePriority(chunk);
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
                    return StreamingFailureValue<StreamingDemandHandle>(
                        "streaming.invalid_successor_state",
                        "unloading request successor link is inconsistent");
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

    if (record == nullptr)
    {
        if (next_request_value_ == std::numeric_limits<std::uint64_t>::max() ||
            next_request_generation_ == std::numeric_limits<std::uint32_t>::max() ||
            next_demand_value_ == std::numeric_limits<std::uint64_t>::max() ||
            next_demand_generation_ == std::numeric_limits<std::uint32_t>::max())
        {
            return StreamingFailureValue<StreamingDemandHandle>("streaming.id_overflow", "streaming id allocator overflow");
        }
        const StreamingRequestId request_id{next_request_value_++};
        const StreamingRequestHandle request_handle{request_id, next_request_generation_++};

        StreamingRequest request{};
        request.id = request_id;
        request.handle = request_handle;
        request.target = target;
        request.priority = resolved_priority;
        request.cancellation.generation = request_handle.generation;
        request.load_plan.steps = {
            StreamingPlanStepRecord{StreamingPlanStep::ResolveTarget, 64, 0},
            StreamingPlanStepRecord{StreamingPlanStep::PrepareData, 256, 0},
            StreamingPlanStepRecord{StreamingPlanStep::PrepareResources, 512, 0},
            StreamingPlanStepRecord{StreamingPlanStep::Commit, 128, 0}};
        if (dependencies_.data_source)
        {
            const auto plan = dependencies_.data_source->BuildLoadPlan(request);
            if (!plan)
            {
                return foundation::Result<StreamingDemandHandle>::Failure(plan.GetError());
            }
            request.load_plan = plan.Value();
        }

        RequestRecord new_record{};
        new_record.request = request;
        new_record.state = predecessor ? StreamingState::WaitingForPredecessor : StreamingState::Requested;
        new_record.progress = kRequestedProgress;
        new_record.revision = 1;
        new_record.max_priority = resolved_priority;
        new_record.predecessor = predecessor;
        requests_.emplace(request_id, std::move(new_record));
        if (predecessor)
        {
            if (RequestRecord* predecessor_record = FindRequest(*predecessor))
            {
                predecessor_record->successor = request_id;
            }
        }
        else
        {
            chunk_to_request_[chunk] = request_id;
            chunk_states_[chunk] = StreamingState::Requested;
        }
        record = FindRequest(request_id);
        ++statistics_.requested;
    }

    if (next_demand_value_ == std::numeric_limits<std::uint64_t>::max() || next_demand_generation_ == std::numeric_limits<std::uint32_t>::max())
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.id_overflow", "streaming demand id allocator overflow");
    }
    StreamingDemandHandle demand{StreamingDemandId{next_demand_value_++}, record->request.handle, next_demand_generation_++};
    if (record->state == StreamingState::Deactivating)
    {
        if (IResidencyController* controller = ResidencyController())
        {
            const auto activated = controller->ActivateChunk(chunk);
            if (!activated)
            {
                return foundation::Result<StreamingDemandHandle>::Failure(activated.GetError());
            }
        }
        SetState(*record, StreamingState::Resident, kResidentProgress);
        record->request.cancellation.requested = false;
        chunk_states_[chunk] = record->state;
    }
    record->demands.emplace(demand.id, DemandRecord{demand, resolved_priority, true});
    ++record->active_demands;
    record->request.demand_count = record->active_demands;
    UpdatePriority(*record);
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

    auto demand_it = record->demands.find(demand.id);
    if (demand_it == record->demands.end() || !demand_it->second.active)
    {
        return StreamingFailure("streaming.reference_underflow", "streaming demand has already been released");
    }

    if (record->active_demands > 0)
    {
        --record->active_demands;
    }
    record->demands.erase(demand_it);
    record->request.demand_count = record->active_demands;
    ++record->revision;
    UpdatePriority(*record);

    if (record->active_demands == 0)
    {
        return CancelRequest(record->request.handle);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::Shutdown()
{
    if (shutdown_)
    {
        return foundation::Result<void>::Success();
    }

    accepting_requests_ = false;
    std::optional<foundation::Error> first_error;
    bool progressed = false;
    do
    {
        progressed = false;
        std::vector<StreamingRequestId> ids;
        ids.reserve(requests_.size());
        for (const auto& [id, record] : requests_)
        {
            (void)record;
            ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [](StreamingRequestId left, StreamingRequestId right) { return left.value < right.value; });
        for (StreamingRequestId id : ids)
        {
            RequestRecord* record = FindRequest(id);
            if (record == nullptr)
            {
                continue;
            }
            const StreamingState before_state = record->state;
            const std::size_t before_cursor = record->request.load_plan.cursor;
            const std::size_t before_bytes = record->processed_bytes;
            record->active_demands = 0;
            record->request.demand_count = 0;
            record->demands.clear();

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
                result = CancelRequest(record->request.handle);
                break;
            case StreamingState::Deactivating:
            case StreamingState::Unloading:
                result = AdvanceRequest(*record, RuntimeBudget{});
                break;
            case StreamingState::WaitingForPredecessor:
                result = CancelWaitingForPredecessor(*record);
                break;
            case StreamingState::RollbackFailed:
                result = Rollback(*record);
                break;
            case StreamingState::NotRequested:
            case StreamingState::Unloaded:
            case StreamingState::Cancelled:
            case StreamingState::Failed:
                break;
            }
            if (!result && !first_error.has_value())
            {
                first_error = result.GetError();
            }
            if (record->state != before_state || record->request.load_plan.cursor != before_cursor || record->processed_bytes != before_bytes)
            {
                progressed = true;
            }
        }
    } while (progressed);

    bool has_live_work = false;
    bool has_resident_chunks = false;
    for (const auto& [id, record] : requests_)
    {
        (void)id;
        if (record.active_demands != 0 || IsLiveWork(record.state))
        {
            has_live_work = true;
        }
        if (record.state == StreamingState::Resident || record.state == StreamingState::Active ||
            record.state == StreamingState::Deactivating || record.state == StreamingState::Unloading)
        {
            has_resident_chunks = true;
        }
    }
    if (has_live_work || has_resident_chunks)
    {
        if (!first_error.has_value())
        {
            first_error = foundation::Error::Create("streaming.shutdown_incomplete", "streaming shutdown did not drain all live work");
        }
    }
    if (first_error.has_value())
    {
        return foundation::Result<void>::Failure(*first_error);
    }
    shutdown_ = true;
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

    record->request.cancellation.requested = true;
    ++statistics_.cancelled;
    switch (record->state)
    {
    case StreamingState::Requested:
    case StreamingState::Queued:
    {
        const auto completed = CompleteTerminal(*record, StreamingState::Cancelled);
        if (!completed)
        {
            return completed;
        }
        if (const auto chunk = GetChunkTarget(record->request.target))
        {
            chunk_states_[*chunk] = record->state;
        }
        return foundation::Result<void>::Success();
    }
    case StreamingState::Loading:
    case StreamingState::Loaded:
    {
        const auto rolled_back = Rollback(*record);
        if (!rolled_back)
        {
            const auto completed = CompleteTerminal(*record, StreamingState::RollbackFailed);
            if (!completed)
            {
                return completed;
            }
            ++statistics_.rollback_failed;
            if (const auto chunk = GetChunkTarget(record->request.target))
            {
                chunk_states_[*chunk] = record->state;
            }
        }
        return rolled_back;
    }
    case StreamingState::Resident:
    case StreamingState::Active:
        return BeginUnload(*record);
    case StreamingState::Activating:
    {
        const auto rolled_back = Rollback(*record);
        if (!rolled_back)
        {
            const auto completed = CompleteTerminal(*record, StreamingState::RollbackFailed);
            if (!completed)
            {
                return completed;
            }
            ++statistics_.rollback_failed;
            if (const auto chunk = GetChunkTarget(record->request.target))
            {
                chunk_states_[*chunk] = record->state;
            }
        }
        return rolled_back;
    }
    case StreamingState::Deactivating:
    case StreamingState::Unloading:
        return foundation::Result<void>::Success();
    case StreamingState::WaitingForPredecessor:
        return CancelWaitingForPredecessor(*record);
    case StreamingState::Unloaded:
    case StreamingState::Cancelled:
    case StreamingState::Failed:
    case StreamingState::RollbackFailed:
    case StreamingState::NotRequested:
        return foundation::Result<void>::Success();
    }
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
}

StreamingTickResult StreamingRuntime::Tick()
{
    const std::vector<StreamingRequestId> work_list = BuildWorkList();
    const std::size_t max_requests = budget_.max_requests == 0 ? work_list.size() : budget_.max_requests;
    const std::size_t max_bytes = budget_.max_bytes == 0 ? std::numeric_limits<std::size_t>::max() : budget_.max_bytes;
    const auto started = std::chrono::steady_clock::now();

    StreamingTickResult result{};
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
        if (max_bytes != std::numeric_limits<std::size_t>::max())
        {
            available_budget.max_bytes = static_cast<std::uint64_t>(max_bytes - bytes);
        }
        const std::size_t before_bytes = record->processed_bytes;
        const auto advanced = AdvanceRequest(*record, available_budget);
        const std::size_t request_bytes = record->processed_bytes - before_bytes;
        if (request_bytes > std::numeric_limits<std::size_t>::max() - bytes)
        {
            ++statistics_.budget_violations;
            result.failures.push_back(StreamingTickFailure{
                record->request.handle,
                record->request.target,
                foundation::Error::Create("streaming.byte_counter_overflow", "streaming tick byte counter overflowed")});
            break;
        }
        bytes += request_bytes;
        if (!advanced)
        {
            const auto rolled_back = Rollback(*record);
            if (!rolled_back)
            {
                const auto completed = CompleteTerminal(*record, StreamingState::RollbackFailed);
                if (!completed)
                {
                    result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, completed.GetError()});
                    break;
                }
                ++statistics_.rollback_failed;
                if (const auto chunk = GetChunkTarget(record->request.target))
                {
                    chunk_states_[*chunk] = record->state;
                }
                result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, rolled_back.GetError()});
            }
            else
            {
                const auto completed = CompleteTerminal(*record, StreamingState::Failed);
                if (!completed)
                {
                    result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, completed.GetError()});
                    break;
                }
                if (const auto chunk = GetChunkTarget(record->request.target))
                {
                    chunk_states_[*chunk] = record->state;
                }
                result.failures.push_back(StreamingTickFailure{record->request.handle, record->request.target, advanced.GetError()});
            }
            ++statistics_.failed;
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

void StreamingRuntime::SetState(RequestRecord& record, StreamingState state, float progress)
{
    if (record.state == state && record.progress == progress)
    {
        return;
    }
    record.state = state;
    record.progress = progress;
    ++record.revision;
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
    if (record == nullptr)
    {
        return nullptr;
    }
    const auto iterator = record->demands.find(demand.id);
    if (iterator == record->demands.end() || iterator->second.handle.generation != demand.generation)
    {
        return nullptr;
    }
    return record;
}

const StreamingRuntime::RequestRecord* StreamingRuntime::FindByDemand(StreamingDemandHandle demand) const
{
    const RequestRecord* record = FindRequest(demand.request.id);
    if (record == nullptr)
    {
        return nullptr;
    }
    const auto iterator = record->demands.find(demand.id);
    if (iterator == record->demands.end() || iterator->second.handle.generation != demand.generation)
    {
        return nullptr;
    }
    return record;
}

std::vector<StreamingRequestId> StreamingRuntime::BuildWorkList() const
{
    std::vector<StreamingRequestId> work_list;
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
    switch (record.state)
    {
    case StreamingState::Requested:
        SetState(record, StreamingState::Queued, kQueuedProgress);
        break;
    case StreamingState::Queued:
        SetState(record, StreamingState::Loading, kLoadingProgress);
        break;
    case StreamingState::Loading:
        return ExecutePlanStep(record, available_budget);
    case StreamingState::Loaded:
        SetState(record, StreamingState::Activating, kActivatingProgress);
        break;
    case StreamingState::Activating:
        if (IResidencyController* controller = ResidencyController())
        {
            const auto chunk = GetChunkTarget(record.request.target);
            const auto activated = chunk ? controller->ActivateChunk(*chunk) : StreamingFailure("streaming.unsupported_target", "reference streaming runtime only activates chunk targets");
            if (!activated)
            {
                return activated;
            }
        }
        SetState(record, StreamingState::Resident, kResidentProgress);
        ++statistics_.committed;
        break;
    case StreamingState::Resident:
        SetState(record, StreamingState::Active, kActiveProgress);
        break;
    case StreamingState::Deactivating:
        SetState(record, StreamingState::Unloading, kUnloadingProgress);
        break;
    case StreamingState::Unloading:
        if (IStreamingResourceSource* resource_source = ResourceSource())
        {
            const auto chunk = GetChunkTarget(record.request.target);
            const auto released = chunk ? resource_source->ReleaseChunkResources(*chunk) : StreamingFailure("streaming.unsupported_target", "reference streaming runtime only releases chunk resources");
            if (!released)
            {
                return released;
            }
        }
        if (IResidencyController* controller = ResidencyController())
        {
            const auto chunk = GetChunkTarget(record.request.target);
            const auto unloaded = chunk ? controller->UnloadChunk(*chunk) : StreamingFailure("streaming.unsupported_target", "reference streaming runtime only unloads chunk targets");
            if (!unloaded)
            {
                return unloaded;
            }
        }
        {
        const auto completed = CompleteTerminal(record, StreamingState::Unloaded);
        if (!completed)
        {
            return completed;
        }
        ++statistics_.unloaded;
        if (record.successor)
        {
            if (RequestRecord* successor = FindRequest(*record.successor))
            {
                if (successor->state == StreamingState::WaitingForPredecessor &&
                    successor->active_demands > 0 &&
                    successor->predecessor == record.request.id)
                {
                    successor->predecessor.reset();
                    SetState(*successor, StreamingState::Requested, kRequestedProgress);
                    if (const auto successor_chunk = GetChunkTarget(successor->request.target))
                    {
                        chunk_to_request_[*successor_chunk] = successor->request.id;
                        chunk_states_[*successor_chunk] = successor->state;
                    }
                }
                else if (successor->predecessor == record.request.id)
                {
                    successor->predecessor.reset();
                }
            }
            record.successor.reset();
        }
        }
        break;
    default:
        break;
    }
    if (const auto chunk = GetChunkTarget(record.request.target))
    {
        const auto mapping = chunk_to_request_.find(*chunk);
        if (mapping == chunk_to_request_.end() || mapping->second == record.request.id)
        {
            chunk_states_[*chunk] = record.state;
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::ExecutePlanStep(RequestRecord& record, RuntimeBudget available_budget)
{
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        SetState(record, StreamingState::Loaded, kLoadedProgress);
        if (const auto chunk = GetChunkTarget(record.request.target))
        {
            chunk_states_[*chunk] = record.state;
        }
        return foundation::Result<void>::Success();
    }

    const std::size_t cursor = record.request.load_plan.cursor;
    StreamingPlanStepRecord step = record.request.load_plan.steps[cursor];
    bool step_completed = true;
    if (dependencies_.data_source)
    {
        const auto executed = dependencies_.data_source->ExecuteStep(record.request, step, available_budget);
        if (!executed)
        {
            return foundation::Result<void>::Failure(executed.GetError());
        }
        step.processed_bytes = executed.Value().processed_bytes;
        step_completed = executed.Value().completed;
    }
    else
    {
        if (step.step == StreamingPlanStep::PrepareData && PersistenceSource())
        {
            const auto prepared = PersistenceSource()->PrepareChunkData(record.request);
            if (!prepared)
            {
                return prepared;
            }
        }
        if (step.step == StreamingPlanStep::ResolveTarget && WorldSource())
        {
            const auto chunk = GetChunkTarget(record.request.target);
            if (!chunk)
            {
                return StreamingFailure("streaming.unsupported_target", "reference streaming runtime only resolves chunk targets");
            }
            (void)WorldSource()->ResolveRegion(*chunk);
        }
        if (step.step == StreamingPlanStep::PrepareResources && ResourceSource())
        {
            const auto prepared = ResourceSource()->PrepareChunkResources(record.request);
            if (!prepared)
            {
                return prepared;
            }
        }
    }
    if (step_completed && step.step == StreamingPlanStep::Commit && !record.commit_completed && dependencies_.commit_target)
    {
        const auto committed = dependencies_.commit_target->Commit(record.request);
        if (!committed)
        {
            return committed;
        }
        record.commit_completed = true;
    }
    if (!dependencies_.data_source && step.processed_bytes == 0)
    {
        step.processed_bytes = step.estimated_bytes;
    }
    if (available_budget.HasByteLimit() && step.processed_bytes > available_budget.max_bytes)
    {
        ++statistics_.budget_violations;
        return StreamingFailure("streaming.step_budget_violation", "streaming plan step exceeded the available byte budget");
    }
    if (step.processed_bytes > std::numeric_limits<std::size_t>::max() - record.processed_bytes)
    {
        ++statistics_.budget_violations;
        return StreamingFailure("streaming.byte_counter_overflow", "streaming request byte counter overflowed");
    }
    record.processed_bytes += step.processed_bytes;
    record.request.load_plan.steps[cursor].processed_bytes += step.processed_bytes;
    if (step_completed)
    {
        ++record.request.load_plan.cursor;
    }

    record.progress = std::min(0.70f, kLoadingProgress + (0.40f * static_cast<float>(record.request.load_plan.cursor)) /
                                                    static_cast<float>(std::max<std::size_t>(1, record.request.load_plan.steps.size())));
    ++record.revision;
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        SetState(record, StreamingState::Loaded, kLoadedProgress);
    }
    if (const auto chunk = GetChunkTarget(record.request.target))
    {
        chunk_states_[*chunk] = record.state;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::Rollback(RequestRecord& record)
{
    if (dependencies_.commit_target)
    {
        const auto rolled_back = dependencies_.commit_target->Rollback(record.request);
        if (!rolled_back)
        {
            return rolled_back;
        }
    }
    const auto completed = CompleteTerminal(record, StreamingState::Cancelled);
    if (!completed)
    {
        return completed;
    }
    if (const auto chunk = GetChunkTarget(record.request.target))
    {
        chunk_states_[*chunk] = record.state;
    }
    ++statistics_.rolled_back;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::BeginUnload(RequestRecord& record)
{
    if (IResidencyController* controller = ResidencyController())
    {
        const auto chunk = GetChunkTarget(record.request.target);
        const auto deactivated = chunk ? controller->DeactivateChunk(*chunk) : StreamingFailure("streaming.unsupported_target", "reference streaming runtime only deactivates chunk targets");
        if (!deactivated)
        {
            return deactivated;
        }
    }
    SetState(record, StreamingState::Deactivating, kUnloadingProgress);
    if (const auto chunk = GetChunkTarget(record.request.target))
    {
        chunk_states_[*chunk] = record.state;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<std::uint64_t> StreamingRuntime::AllocateCompletionSequence()
{
    if (next_completion_sequence_ == std::numeric_limits<std::uint64_t>::max())
    {
        return StreamingFailureValue<std::uint64_t>("streaming.completion_sequence_overflow", "streaming completion sequence overflow");
    }
    return foundation::Result<std::uint64_t>::Success(next_completion_sequence_++);
}

foundation::Result<void> StreamingRuntime::CompleteTerminal(RequestRecord& record, StreamingState state)
{
    const auto sequence = AllocateCompletionSequence();
    if (!sequence)
    {
        return foundation::Result<void>::Failure(sequence.GetError());
    }
    SetState(record, state, kTerminalProgress);
    record.completion_sequence = sequence.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::CancelWaitingForPredecessor(RequestRecord& record)
{
    if (record.active_demands != 0)
    {
        return StreamingFailure("streaming.active_demands", "waiting successor cancellation requires all demands to be released first");
    }
    ClearGraphLinks(record);
    record.predecessor.reset();
    record.successor.reset();
    return CompleteTerminal(record, StreamingState::Cancelled);
}

void StreamingRuntime::ClearGraphLinks(RequestRecord& record)
{
    if (record.predecessor)
    {
        if (RequestRecord* predecessor = FindRequest(*record.predecessor);
            predecessor != nullptr && predecessor->successor == record.request.id)
        {
            predecessor->successor.reset();
        }
    }
    if (record.successor)
    {
        if (RequestRecord* successor = FindRequest(*record.successor);
            successor != nullptr && successor->predecessor == record.request.id)
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

void StreamingRuntime::UpdatePriority(RequestRecord& record)
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
        if (candidate == requests_.end())
        {
            return;
        }
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
