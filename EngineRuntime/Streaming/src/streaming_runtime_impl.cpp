#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <tuple>

namespace epidemic::runtime::streaming
{
namespace
{
constexpr float kRequestedProgress = 0.0f;
constexpr float kQueuedProgress = 0.15f;
constexpr float kLoadingProgress = 0.35f;
constexpr float kLoadedProgress = 0.65f;
constexpr float kActivatingProgress = 0.85f;
constexpr float kActiveProgress = 1.0f;
constexpr float kDeactivatingProgress = 0.75f;
constexpr float kUnloadingProgress = 0.9f;
constexpr float kUnloadedProgress = 1.0f;
constexpr float kFailedProgress = 1.0f;
} // namespace

foundation::Result<void> InMemoryResidencyController::ActivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.invalid_chunk", "chunk id must be valid before activation"));
    }

    chunk_states_[chunk] = StreamingState::Active;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::DeactivateChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.invalid_chunk", "chunk id must be valid before deactivation"));
    }

    chunk_states_[chunk] = StreamingState::Deactivating;
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryResidencyController::UnloadChunk(ChunkId chunk)
{
    if (!chunk.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.invalid_chunk", "chunk id must be valid before unloading"));
    }

    chunk_states_[chunk] = StreamingState::Unloaded;
    return foundation::Result<void>::Success();
}

StreamingState InMemoryResidencyController::GetResidencyState(ChunkId chunk) const
{
    const auto iterator = chunk_states_.find(chunk);
    if (iterator == chunk_states_.end())
    {
        return StreamingState::NotRequested;
    }

    return iterator->second;
}

StreamingRuntime::StreamingRuntime(
    IStreamingPriorityResolver* priority_resolver,
    IResidencyController* residency_controller,
    IStreamingWorldSource* world_source,
    IStreamingPersistenceSource* persistence_source,
    IStreamingResourceSource* resource_source)
    : priority_resolver_(priority_resolver),
      residency_controller_(residency_controller),
      world_source_(world_source),
      persistence_source_(persistence_source),
      resource_source_(resource_source)
{
}

foundation::Result<StreamingRequestId> StreamingRuntime::RequestChunk(
    ChunkId chunk,
    StreamingPriorityClass priority)
{
    const auto request = RequestTarget(ChunkStreamingTarget{chunk}, priority, 1);
    if (!request)
    {
        return foundation::Result<StreamingRequestId>::Failure(request.GetError());
    }

    return foundation::Result<StreamingRequestId>::Success(request.Value().id);
}

foundation::Result<StreamingRequestHandle> StreamingRuntime::RequestTarget(
    const StreamingTarget& target,
    StreamingPriorityClass priority,
    std::uint32_t demand_count)
{
    const auto chunk_target = GetChunkTarget(target);
    if (!chunk_target)
    {
        return foundation::Result<StreamingRequestHandle>::Failure(
            foundation::Error::Create("streaming.unsupported_target", "reference streaming runtime only executes chunk targets"));
    }

    const ChunkId chunk = *chunk_target;
    if (!chunk.IsValid())
    {
        return foundation::Result<StreamingRequestHandle>::Failure(
            foundation::Error::Create("streaming.invalid_chunk", "chunk id must be valid before requesting streaming"));
    }
    if (demand_count == 0)
    {
        return foundation::Result<StreamingRequestHandle>::Failure(
            foundation::Error::Create("streaming.invalid_demand", "streaming demand count must be positive"));
    }

    const auto existing_request = chunk_to_request_.find(chunk);
    if (existing_request != chunk_to_request_.end())
    {
        RequestRecord* current = FindRequest(existing_request->second);
        if (current != nullptr && !IsTerminal(current->state))
        {
            if (PriorityRank(priority) < PriorityRank(current->request.priority))
            {
                current->request.priority = priority;
            }
            current->request.demand_count += demand_count;

            return foundation::Result<StreamingRequestHandle>::Success(current->request.handle);
        }
    }

    const StreamingRequestId request_id{next_request_value_++};
    const StreamingRequestHandle request_handle{request_id, next_generation_++};
    StreamingPriorityClass resolved_priority = priority;
    if (resolved_priority == StreamingPriorityClass::Normal && priority_resolver_ != nullptr)
    {
        resolved_priority = priority_resolver_->ResolvePriority(chunk);
    }

    RuntimeBudget request_budget{};
    request_budget.max_time = budget_.cpu_budget;
    request_budget.max_items = static_cast<std::uint32_t>(budget_.max_requests);
    request_budget.max_bytes = budget_.max_bytes;

    StreamingRequest request{};
    request.id = request_id;
    request.handle = request_handle;
    request.target = target;
    request.chunk = chunk;
    request.priority = resolved_priority;
    request.demand_count = demand_count;
    request.cancellation.generation = request_handle.generation;
    request.load_plan.steps = {
        StreamingPlanStep::ResolveTarget,
        StreamingPlanStep::PrepareData,
        StreamingPlanStep::PrepareResources,
        StreamingPlanStep::Commit,
        StreamingPlanStep::Rollback,
        StreamingPlanStep::Release,
    };
    request.budget_hint = request_budget;
    if (world_source_ != nullptr)
    {
        const auto region = world_source_->ResolveRegion(chunk);
        if (region.has_value())
        {
            request.region = *region;
        }
    }

    RequestRecord record{};
    record.request = request;
    record.state = StreamingState::Requested;
    record.progress = kRequestedProgress;
    record.revision = 1;

    requests_.emplace(request_id, record);
    chunk_to_request_[chunk] = request_id;
    chunk_states_[chunk] = StreamingState::Requested;
    ++statistics_.requested;
    return foundation::Result<StreamingRequestHandle>::Success(request_handle);
}

foundation::Result<void> StreamingRuntime::CancelRequest(StreamingRequestId request)
{
    RequestRecord* record = FindRequest(request);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.request_not_found", "streaming request was not found for cancellation"));
    }

    if (record->state == StreamingState::Unloaded || record->state == StreamingState::Failed)
    {
        return foundation::Result<void>::Success();
    }

    if (record->state == StreamingState::Deactivating || record->state == StreamingState::Unloading)
    {
        return foundation::Result<void>::Success();
    }

    SetState(*record, StreamingState::Deactivating, kDeactivatingProgress);
    record->request.cancellation.requested = true;
    chunk_states_[record->request.chunk] = record->state;
    ++statistics_.cancelled;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::CancelRequest(StreamingRequestHandle request)
{
    if (!request.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.invalid_handle", "streaming request handle must be valid"));
    }

    RequestRecord* record = FindRequest(request.id);
    if (record == nullptr || record->request.handle.generation != request.generation)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("streaming.stale_handle", "streaming request handle generation is stale"));
    }

    return CancelRequest(request.id);
}

StreamingState StreamingRuntime::GetChunkState(ChunkId chunk) const
{
    const auto iterator = chunk_states_.find(chunk);
    if (iterator == chunk_states_.end())
    {
        return StreamingState::NotRequested;
    }

    return iterator->second;
}

void StreamingRuntime::SetBudget(const StreamingBudget& budget)
{
    budget_ = budget;
}

void StreamingRuntime::Tick()
{
    const std::vector<StreamingRequestId> work_list = BuildWorkList();
    const std::size_t max_requests = budget_.max_requests == 0 ? work_list.size() : budget_.max_requests;

    std::size_t processed = 0;
    for (const StreamingRequestId request_id : work_list)
    {
        if (processed >= max_requests)
        {
            break;
        }

        RequestRecord* record = FindRequest(request_id);
        if (record == nullptr || IsTerminal(record->state))
        {
            continue;
        }

        const auto advance_result = AdvanceRequest(*record);
        if (!advance_result)
        {
            SetState(*record, StreamingState::Failed, kFailedProgress);
            chunk_states_[record->request.chunk] = record->state;
            ++statistics_.failed;
        }

        ++processed;
    }
}

std::optional<StreamingProgress> StreamingRuntime::GetProgress(StreamingRequestId request) const
{
    const RequestRecord* record = FindRequest(request);
    if (record == nullptr)
    {
        return std::nullopt;
    }

    return StreamingProgress{
        record->request.id,
        record->request.handle,
        record->request.target,
        record->state,
        record->progress,
        record->revision,
    };
}

std::optional<StreamingProgress> StreamingRuntime::GetProgress(StreamingRequestHandle request) const
{
    const RequestRecord* record = FindRequest(request.id);
    if (record == nullptr || record->request.handle.generation != request.generation)
    {
        return std::nullopt;
    }

    return StreamingProgress{
        record->request.id,
        record->request.handle,
        record->request.target,
        record->state,
        record->progress,
        record->revision,
    };
}

StreamingStatistics StreamingRuntime::GetStatistics() const
{
    return statistics_;
}

bool StreamingRuntime::IsTerminal(StreamingState state)
{
    return state == StreamingState::Active || state == StreamingState::Resident ||
           state == StreamingState::Unloaded || state == StreamingState::Failed;
}

int StreamingRuntime::PriorityRank(StreamingPriorityClass priority)
{
    switch (priority)
    {
    case StreamingPriorityClass::Critical:
        return 0;
    case StreamingPriorityClass::High:
        return 1;
    case StreamingPriorityClass::Normal:
        return 2;
    case StreamingPriorityClass::Low:
        return 3;
    case StreamingPriorityClass::Background:
        return 4;
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
    if (iterator == requests_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const StreamingRuntime::RequestRecord* StreamingRuntime::FindRequest(StreamingRequestId id) const
{
    const auto iterator = requests_.find(id);
    if (iterator == requests_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

std::vector<StreamingRequestId> StreamingRuntime::BuildWorkList() const
{
    std::vector<StreamingRequestId> work_list;
    work_list.reserve(requests_.size());

    for (const auto& [request_id, record] : requests_)
    {
        if (!IsTerminal(record.state))
        {
            work_list.push_back(request_id);
        }
    }

    std::sort(
        work_list.begin(),
        work_list.end(),
        [this](StreamingRequestId left, StreamingRequestId right) {
            const RequestRecord* left_record = FindRequest(left);
            const RequestRecord* right_record = FindRequest(right);
            const int left_rank = left_record == nullptr ? 5 : PriorityRank(left_record->request.priority);
            const int right_rank = right_record == nullptr ? 5 : PriorityRank(right_record->request.priority);
            return std::tie(left_rank, left.value) < std::tie(right_rank, right.value);
        });

    return work_list;
}

foundation::Result<void> StreamingRuntime::AdvanceRequest(RequestRecord& record)
{
    switch (record.state)
    {
    case StreamingState::Requested:
        SetState(record, StreamingState::Queued, kQueuedProgress);
        break;

    case StreamingState::Queued:
        if (persistence_source_ != nullptr)
        {
            const auto persistence_result = persistence_source_->PrepareChunkData(record.request);
            if (!persistence_result)
            {
                return persistence_result;
            }
        }

        SetState(record, StreamingState::Loading, kLoadingProgress);
        break;

    case StreamingState::Loading:
        if (resource_source_ != nullptr)
        {
            const auto resource_result = resource_source_->PrepareChunkResources(record.request);
            if (!resource_result)
            {
                return resource_result;
            }
        }

        SetState(record, StreamingState::Loaded, kLoadedProgress);
        break;

    case StreamingState::Loaded:
        SetState(record, StreamingState::Activating, kActivatingProgress);
        break;

    case StreamingState::Activating:
        return AdvanceActivation(record);

    case StreamingState::Deactivating:
        if (residency_controller_ != nullptr)
        {
            const auto deactivate_result = residency_controller_->DeactivateChunk(record.request.chunk);
            if (!deactivate_result)
            {
                return deactivate_result;
            }
        }

        SetState(record, StreamingState::Unloading, kUnloadingProgress);
        break;

    case StreamingState::Unloading:
        return AdvanceUnload(record);

    case StreamingState::NotRequested:
    case StreamingState::Active:
    case StreamingState::Resident:
    case StreamingState::Unloaded:
    case StreamingState::Failed:
        break;
    }

    chunk_states_[record.request.chunk] = record.state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::AdvanceActivation(RequestRecord& record)
{
    if (residency_controller_ != nullptr)
    {
        const auto activate_result = residency_controller_->ActivateChunk(record.request.chunk);
        if (!activate_result)
        {
            return activate_result;
        }
    }

    SetState(record, StreamingState::Active, kActiveProgress);
    chunk_states_[record.request.chunk] = record.state;
    ++statistics_.committed;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::AdvanceUnload(RequestRecord& record)
{
    if (resource_source_ != nullptr)
    {
        const auto release_result = resource_source_->ReleaseChunkResources(record.request.chunk);
        if (!release_result)
        {
            return release_result;
        }
    }

    if (residency_controller_ != nullptr)
    {
        const auto unload_result = residency_controller_->UnloadChunk(record.request.chunk);
        if (!unload_result)
        {
            return unload_result;
        }
    }

    SetState(record, StreamingState::Unloaded, kUnloadedProgress);
    chunk_states_[record.request.chunk] = record.state;
    ++statistics_.rolled_back;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime::streaming
