#include "streaming_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
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
                                   IStreamingPriorityResolver* priority_resolver,
                                   IResidencyController* residency_controller,
                                   IStreamingWorldSource* world_source,
                                   IStreamingPersistenceSource* persistence_source,
                                   IStreamingResourceSource* resource_source)
    : dependencies_(std::move(dependencies)),
      priority_resolver_(priority_resolver),
      residency_controller_(residency_controller),
      world_source_(world_source),
      persistence_source_(persistence_source),
      resource_source_(resource_source)
{
}

foundation::Result<StreamingRequestId> StreamingRuntime::RequestChunk(ChunkId chunk, StreamingPriorityClass priority)
{
    const auto demand = Request(StreamingTarget{ChunkStreamingTarget{chunk}}, priority);
    if (!demand)
    {
        return foundation::Result<StreamingRequestId>::Failure(demand.GetError());
    }
    return foundation::Result<StreamingRequestId>::Success(demand.Value().request);
}

foundation::Result<StreamingDemandHandle> StreamingRuntime::Request(const StreamingTarget& target, StreamingPriorityClass priority)
{
    const auto request = RequestTarget(target, priority, 1);
    if (!request)
    {
        return foundation::Result<StreamingDemandHandle>::Failure(request.GetError());
    }
    const RequestRecord* record = FindRequest(request.Value().id);
    if (record == nullptr)
    {
        return StreamingFailureValue<StreamingDemandHandle>("streaming.request_not_found", "new streaming request disappeared");
    }
    return foundation::Result<StreamingDemandHandle>::Success(record->request.demand_handle);
}

foundation::Result<StreamingRequestHandle> StreamingRuntime::RequestTarget(const StreamingTarget& target,
                                                                           StreamingPriorityClass priority,
                                                                           std::uint32_t demand_count)
{
    if (demand_count == 0)
    {
        return StreamingFailureValue<StreamingRequestHandle>("streaming.invalid_demand", "streaming demand count must be positive");
    }

    const auto chunk_target = GetChunkTarget(target);
    if (!chunk_target)
    {
        return StreamingFailureValue<StreamingRequestHandle>("streaming.unsupported_target", "reference streaming runtime only executes chunk targets");
    }
    const ChunkId chunk = *chunk_target;
    if (!chunk.IsValid())
    {
        return StreamingFailureValue<StreamingRequestHandle>("streaming.invalid_chunk", "chunk id must be valid before requesting streaming");
    }

    RequestRecord* record = nullptr;
    const auto existing = chunk_to_request_.find(chunk);
    if (existing != chunk_to_request_.end())
    {
        RequestRecord* existing_record = FindRequest(existing->second);
        if (existing_record != nullptr && existing_record->state != StreamingState::Failed &&
            existing_record->state != StreamingState::Cancelled && existing_record->state != StreamingState::Unloaded)
        {
            record = existing_record;
        }
    }

    if (record == nullptr)
    {
        const StreamingRequestId request_id{next_request_value_++};
        const StreamingRequestHandle request_handle{request_id, next_request_generation_++};
        StreamingPriorityClass resolved_priority = priority;
        if (resolved_priority == StreamingPriorityClass::Normal && dependencies_.priority_provider)
        {
            resolved_priority = dependencies_.priority_provider->GetPriority(target);
        }
        else if (resolved_priority == StreamingPriorityClass::Normal && priority_resolver_ != nullptr)
        {
            resolved_priority = priority_resolver_->ResolvePriority(chunk);
        }

        StreamingRequest request{};
        request.id = request_id;
        request.handle = request_handle;
        request.target = target;
        request.chunk = chunk;
        request.priority = resolved_priority;
        request.cancellation.generation = request_handle.generation;
        request.budget_hint = RuntimeBudget{budget_.cpu_budget, static_cast<std::uint32_t>(budget_.max_requests), budget_.max_bytes};
        request.load_plan.steps = {
            StreamingPlanStep::ResolveTarget,
            StreamingPlanStep::PrepareData,
            StreamingPlanStep::PrepareResources,
            StreamingPlanStep::Commit};
        if (world_source_ != nullptr)
        {
            const auto region = world_source_->ResolveRegion(chunk);
            if (region)
            {
                request.region = *region;
            }
        }
        if (dependencies_.data_source)
        {
            const auto plan = dependencies_.data_source->BuildLoadPlan(request);
            if (!plan)
            {
                return foundation::Result<StreamingRequestHandle>::Failure(plan.GetError());
            }
            request.load_plan = plan.Value();
        }

        RequestRecord new_record{};
        new_record.request = request;
        new_record.state = StreamingState::Requested;
        new_record.progress = kRequestedProgress;
        new_record.revision = 1;
        new_record.max_priority = resolved_priority;
        requests_.emplace(request_id, std::move(new_record));
        chunk_to_request_[chunk] = request_id;
        chunk_states_[chunk] = StreamingState::Requested;
        record = FindRequest(request_id);
        ++statistics_.requested;
    }

    StreamingDemandHandle demand{StreamingDemandId{next_demand_value_++}, record->request.id, next_demand_generation_++};
    record->demands.emplace(demand.id, DemandRecord{demand, priority, true});
    record->active_demands += demand_count;
    record->request.demand_count = record->active_demands;
    record->request.demand_handle = demand;
    UpdatePriority(*record);
    return foundation::Result<StreamingRequestHandle>::Success(record->request.handle);
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

    demand_it->second.active = false;
    if (record->active_demands > 0)
    {
        --record->active_demands;
    }
    record->request.demand_count = record->active_demands;
    ++record->revision;
    UpdatePriority(*record);

    if (record->active_demands == 0)
    {
        return CancelRequest(record->request.handle);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::CancelRequest(StreamingRequestId request)
{
    RequestRecord* record = FindRequest(request);
    if (record == nullptr)
    {
        return StreamingFailure("streaming.request_not_found", "streaming request was not found for cancellation");
    }

    record->request.cancellation.requested = true;
    ++statistics_.cancelled;
    switch (record->state)
    {
    case StreamingState::Requested:
    case StreamingState::Queued:
        SetState(*record, StreamingState::Cancelled, kTerminalProgress);
        chunk_states_[record->request.chunk] = record->state;
        return foundation::Result<void>::Success();
    case StreamingState::Loading:
    case StreamingState::Loaded:
        return Rollback(*record);
    case StreamingState::Resident:
    case StreamingState::Active:
        return BeginUnload(*record);
    case StreamingState::Activating:
        return Rollback(*record);
    case StreamingState::Deactivating:
    case StreamingState::Unloading:
    case StreamingState::Unloaded:
    case StreamingState::Cancelled:
    case StreamingState::Failed:
    case StreamingState::NotRequested:
        return foundation::Result<void>::Success();
    }
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
    return CancelRequest(request.id);
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

void StreamingRuntime::Tick()
{
    const std::vector<StreamingRequestId> work_list = BuildWorkList();
    const std::size_t max_requests = budget_.max_requests == 0 ? work_list.size() : budget_.max_requests;
    const std::size_t max_steps = budget_.max_bytes == 0 ? std::numeric_limits<std::size_t>::max() : budget_.max_bytes;

    std::size_t processed = 0;
    std::size_t steps = 0;
    for (const StreamingRequestId request_id : work_list)
    {
        if (processed >= max_requests || steps >= max_steps)
        {
            break;
        }
        RequestRecord* record = FindRequest(request_id);
        if (record == nullptr || !IsLiveWork(record->state))
        {
            continue;
        }
        const auto advanced = AdvanceRequest(*record);
        if (!advanced)
        {
            (void)Rollback(*record);
            SetState(*record, StreamingState::Failed, kTerminalProgress);
            chunk_states_[record->request.chunk] = record->state;
            ++statistics_.failed;
        }
        ++processed;
        ++steps;
    }
    CleanupHistoryIfNeeded();
}

std::optional<StreamingProgress> StreamingRuntime::GetProgress(StreamingRequestId request) const
{
    const RequestRecord* record = FindRequest(request);
    if (record == nullptr)
    {
        return std::nullopt;
    }
    return StreamingProgress{record->request.id, record->request.handle, record->request.target, record->state,
                             record->progress, record->active_demands, record->revision};
}

std::optional<StreamingProgress> StreamingRuntime::GetProgress(StreamingRequestHandle request) const
{
    const RequestRecord* record = FindRequest(request.id);
    if (record == nullptr || record->request.handle.generation != request.generation)
    {
        return std::nullopt;
    }
    return GetProgress(request.id);
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
    return state == StreamingState::Unloaded || state == StreamingState::Cancelled || state == StreamingState::Failed;
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
    RequestRecord* record = FindRequest(demand.request);
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
    const RequestRecord* record = FindRequest(demand.request);
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
        if (IsLiveWork(record.state))
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

foundation::Result<void> StreamingRuntime::AdvanceRequest(RequestRecord& record)
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
        return ExecutePlanStep(record);
    case StreamingState::Loaded:
        SetState(record, StreamingState::Activating, kActivatingProgress);
        break;
    case StreamingState::Activating:
        if (residency_controller_)
        {
            const auto activated = residency_controller_->ActivateChunk(record.request.chunk);
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
        if (resource_source_)
        {
            const auto released = resource_source_->ReleaseChunkResources(record.request.chunk);
            if (!released)
            {
                return released;
            }
        }
        if (residency_controller_)
        {
            const auto unloaded = residency_controller_->UnloadChunk(record.request.chunk);
            if (!unloaded)
            {
                return unloaded;
            }
        }
        SetState(record, StreamingState::Unloaded, kTerminalProgress);
        ++statistics_.rolled_back;
        break;
    default:
        break;
    }
    chunk_states_[record.request.chunk] = record.state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::ExecutePlanStep(RequestRecord& record)
{
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        SetState(record, StreamingState::Loaded, kLoadedProgress);
        chunk_states_[record.request.chunk] = record.state;
        return foundation::Result<void>::Success();
    }

    const StreamingPlanStep step = record.request.load_plan.steps[record.request.load_plan.cursor++];
    if (dependencies_.data_source)
    {
        const auto executed = dependencies_.data_source->ExecuteStep(record.request, step);
        if (!executed)
        {
            return executed;
        }
    }
    else
    {
        if (step == StreamingPlanStep::PrepareData && persistence_source_)
        {
            const auto prepared = persistence_source_->PrepareChunkData(record.request);
            if (!prepared)
            {
                return prepared;
            }
        }
        if (step == StreamingPlanStep::PrepareResources && resource_source_)
        {
            const auto prepared = resource_source_->PrepareChunkResources(record.request);
            if (!prepared)
            {
                return prepared;
            }
        }
    }
    if (step == StreamingPlanStep::Commit && dependencies_.commit_target)
    {
        const auto committed = dependencies_.commit_target->Commit(record.request);
        if (!committed)
        {
            return committed;
        }
        record.commit_completed = true;
    }

    record.progress = std::min(0.70f, kLoadingProgress + (0.40f * static_cast<float>(record.request.load_plan.cursor)) /
                                                    static_cast<float>(std::max<std::size_t>(1, record.request.load_plan.steps.size())));
    ++record.revision;
    if (record.request.load_plan.cursor >= record.request.load_plan.steps.size())
    {
        SetState(record, StreamingState::Loaded, kLoadedProgress);
    }
    chunk_states_[record.request.chunk] = record.state;
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
    SetState(record, StreamingState::Cancelled, kTerminalProgress);
    chunk_states_[record.request.chunk] = record.state;
    ++statistics_.rolled_back;
    return foundation::Result<void>::Success();
}

foundation::Result<void> StreamingRuntime::BeginUnload(RequestRecord& record)
{
    if (residency_controller_)
    {
        const auto deactivated = residency_controller_->DeactivateChunk(record.request.chunk);
        if (!deactivated)
        {
            return deactivated;
        }
    }
    SetState(record, StreamingState::Deactivating, kUnloadingProgress);
    chunk_states_[record.request.chunk] = record.state;
    return foundation::Result<void>::Success();
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
                candidate = iterator;
                break;
            }
        }
        if (candidate == requests_.end())
        {
            return;
        }
        const auto chunk = candidate->second.request.chunk;
        chunk_to_request_.erase(chunk);
        requests_.erase(candidate);
    }
}
} // namespace epidemic::runtime::streaming
