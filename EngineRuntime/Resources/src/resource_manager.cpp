#include "resource_manager.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <exception>
#include <limits>
#include <string_view>
#include <vector>
#include <utility>

namespace epidemic::runtime
{
namespace
{
constexpr ResourceGeneration kInitialGeneration = 1;

[[nodiscard]] foundation::Result<void> ResourceFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> ResourceFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}

class LoadingResourceGuard
{
  public:
    LoadingResourceGuard(std::unordered_set<ResourceId>& loading_resources, ResourceId id) noexcept
        : loading_resources_(loading_resources), id_(id)
    {
    }

    LoadingResourceGuard(const LoadingResourceGuard&) = delete;
    LoadingResourceGuard& operator=(const LoadingResourceGuard&) = delete;

    ~LoadingResourceGuard()
    {
        loading_resources_.erase(id_);
    }

  private:
    std::unordered_set<ResourceId>& loading_resources_;
    ResourceId id_{};
};
} // namespace

void ResourceLoadQueue::Enqueue(ResourceLoadJob job)
{
    if (fail_next_enqueue_for_testing_)
    {
        fail_next_enqueue_for_testing_ = false;
        throw std::bad_alloc{};
    }
    jobs_.push_back(std::move(job));
}

void ResourceLoadQueue::PopBack() noexcept
{
    if (!jobs_.empty())
    {
        jobs_.pop_back();
    }
}

void ResourceLoadQueue::FailNextEnqueueForTesting() noexcept
{
    fail_next_enqueue_for_testing_ = true;
}

bool ResourceLoadQueue::IsEmpty() const noexcept
{
    return jobs_.empty();
}

std::optional<ResourceLoadJob> ResourceLoadQueue::Dequeue()
{
    if (jobs_.empty())
    {
        return std::nullopt;
    }

    ResourceLoadJob job = jobs_.front();
    jobs_.pop_front();
    return job;
}

std::size_t ResourceLoadQueue::Size() const noexcept
{
    return jobs_.size();
}

ResourceSlot* ResourceCache::Find(ResourceId id)
{
    const auto iterator = slots_.find(id);
    if (iterator == slots_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

const ResourceSlot* ResourceCache::Find(ResourceId id) const
{
    const auto iterator = slots_.find(id);
    if (iterator == slots_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

ResourceSlot& ResourceCache::FindOrCreate(ResourceId id, ResourceType type)
{
    auto [iterator, inserted] = slots_.try_emplace(id);
    if (inserted)
    {
        iterator->second.id = id;
        iterator->second.type = type;
        iterator->second.generation = kInitialGeneration;
        iterator->second.state = ResourceState::Unloaded;
    }
    return iterator->second;
}

std::unordered_map<ResourceId, ResourceSlot>& ResourceCache::Entries()
{
    return slots_;
}

const std::unordered_map<ResourceId, ResourceSlot>& ResourceCache::Entries() const
{
    return slots_;
}

ResourceManager::ResourceManager(IResourceLoaderRegistry* loader_registry) : loader_registry_(loader_registry)
{
}

foundation::Result<ResourceLease> ResourceManager::RequestLease(ResourceRequest request)
{
    const auto valid = ValidateRequest(request);
    if (!valid)
    {
        return foundation::Result<ResourceLease>::Failure(valid.GetError());
    }

    const auto acquisition = PeekAcquisitionId();
    if (!acquisition)
    {
        return foundation::Result<ResourceLease>::Failure(acquisition.GetError());
    }

    const auto handle = Acquire(std::move(request), acquisition.Value());
    if (!handle)
    {
        return foundation::Result<ResourceLease>::Failure(handle.GetError());
    }

    CommitAcquisitionId();
    return foundation::Result<ResourceLease>::Success(ResourceLease{handle.Value(), acquisition.Value()});
}

foundation::Result<ResourceHandle> ResourceManager::Acquire(ResourceRequest request, ResourceAcquisitionId acquisition)
{
    const auto valid = ValidateRequest(request);
    if (!valid)
    {
        return foundation::Result<ResourceHandle>::Failure(valid.GetError());
    }
    if (acquisition == 0)
    {
        return ResourceFailureValue<ResourceHandle>("resource.invalid_acquisition", "resource acquisition id must be valid");
    }

    ResourceSlot* slot = cache_.Find(request.resource_id);
    if (slot != nullptr && slot->type.IsValid() && slot->type != request.type)
    {
        return ResourceFailureValue<ResourceHandle>("resource.type_mismatch", "resource id is already associated with a different type");
    }
    if (slot != nullptr && slot->state == ResourceState::Evicting)
    {
        return ResourceFailureValue<ResourceHandle>("resource.evicting", "resource cannot be acquired while eviction reconciliation is pending");
    }
    if (slot != nullptr && slot->reference_count == std::numeric_limits<std::uint32_t>::max())
    {
        return ResourceFailureValue<ResourceHandle>("resource.reference_overflow", "resource reference count is exhausted");
    }

    const bool existed = slot != nullptr;
    bool queued_job = false;
    try
    {
        if (!existed)
        {
            ResourceSlot prepared{};
            prepared.id = request.resource_id;
            prepared.type = request.type;
            prepared.generation = kInitialGeneration;
            prepared.state = ResourceState::Unloaded;
            auto [it, inserted] = cache_.Entries().try_emplace(request.resource_id, std::move(prepared));
            if (!inserted)
            {
                return ResourceFailureValue<ResourceHandle>("resource.duplicate_slot", "resource slot unexpectedly already exists");
            }
            slot = &it->second;
        }

        const bool needs_queue = slot->state != ResourceState::Ready && slot->state != ResourceState::Queued &&
                                 slot->state != ResourceState::Loading && slot->state != ResourceState::WaitingForDependencies;
        if (needs_queue)
        {
            load_queue_.Enqueue(ResourceLoadJob{request, slot->generation});
            queued_job = true;
        }

        // active_acquisitions insertion is the last allocation before local publication.
        if (fail_next_acquisition_publish_for_testing_)
        {
            fail_next_acquisition_publish_for_testing_ = false;
            throw std::bad_alloc{};
        }
        const auto [_, inserted] = slot->active_acquisitions.insert(acquisition);
        if (!inserted)
        {
            if (queued_job)
            {
                load_queue_.PopBack();
            }
            if (!existed && slot->reference_count == 0 && slot->active_acquisitions.empty())
            {
                cache_.Entries().erase(slot->id);
            }
            return ResourceFailureValue<ResourceHandle>("resource.duplicate_acquisition", "resource acquisition id already exists");
        }

        ++slot->reference_count;
        slot->type = request.type;
        if (needs_queue)
        {
            slot->state = ResourceState::Queued;
            slot->pending_artifact.reset();
        }
        return foundation::Result<ResourceHandle>::Success(ResourceHandle{slot->id, slot->generation});
    }
    catch (const std::bad_alloc&)
    {
        if (queued_job)
        {
            load_queue_.PopBack();
        }
        if (slot != nullptr)
        {
            slot->active_acquisitions.erase(acquisition);
            if (!existed && slot->reference_count == 0 && slot->active_acquisitions.empty())
            {
                cache_.Entries().erase(request.resource_id);
            }
        }
        return ResourceFailureValue<ResourceHandle>("resource.allocation_failed", "resource acquisition could not be published");
    }
    catch (const std::exception&)
    {
        if (queued_job)
        {
            load_queue_.PopBack();
        }
        if (slot != nullptr)
        {
            slot->active_acquisitions.erase(acquisition);
            if (!existed && slot->reference_count == 0 && slot->active_acquisitions.empty())
            {
                cache_.Entries().erase(request.resource_id);
            }
        }
        return ResourceFailureValue<ResourceHandle>("resource.acquire_exception", "resource acquisition failed with an unexpected exception");
    }
}

foundation::Result<ResourceProcessingStats> ResourceManager::ProcessPendingLoads(RuntimeBudget budget)
{
    ResourceProcessingStats stats{};
    const std::size_t max_jobs = budget.HasItemLimit() ? budget.max_items : std::numeric_limits<std::size_t>::max();
    const auto started_at = std::chrono::steady_clock::now();

    while (!load_queue_.IsEmpty() && stats.attempted_jobs < max_jobs)
    {
        if (budget.HasTimeLimit() && std::chrono::steady_clock::now() - started_at >= budget.max_time)
        {
            break;
        }
        if (budget.HasByteLimit() && stats.bytes_loaded >= budget.max_bytes)
        {
            break;
        }

        std::optional<ResourceLoadJob> job = load_queue_.Dequeue();
        if (!job)
        {
            break;
        }
        ++stats.attempted_jobs;

        ResourceSlot* slot = cache_.Find(job->request.resource_id);
        if (slot == nullptr || slot->generation != job->generation)
        {
            ++stats.skipped_jobs;
            continue;
        }
        if (slot->reference_count == 0)
        {
            slot->state = ResourceState::Unloaded;
            const auto rolled_back = RollbackLoadAttempt(*slot);
            if (!rolled_back)
            {
                ++stats.failed_resources;
            }
            ++stats.skipped_jobs;
            continue;
        }

        ++stats.processed_jobs;
        foundation::Result<void> result = slot->pending_artifact ? FinishLoadedArtifact(*slot, std::move(*slot->pending_artifact), stats)
                                                                 : LoadSlot(*slot, job->request, stats);
        if (!result)
        {
            ++stats.failed_resources;
            continue;
        }
    }

    return foundation::Result<ResourceProcessingStats>::Success(stats);
}

foundation::Result<void> ResourceManager::Release(ResourceLease lease)
{
    if (!lease.IsValid())
    {
        return ResourceFailure("resource.invalid_lease", "resource lease must be valid before release");
    }

    ResourceSlot* slot = cache_.Find(lease.resource.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, lease.resource))
    {
        return ResourceFailure("resource.stale_handle", "resource lease target is unknown or stale");
    }
    if (!slot->active_acquisitions.contains(lease.acquisition))
    {
        return ResourceFailure("resource.acquisition_underflow", "resource acquisition has already been released");
    }
    if (slot->reference_count == 0)
    {
        return ResourceFailure("resource.reference_underflow", "resource reference count underflow while releasing acquisition");
    }
    slot->active_acquisitions.erase(lease.acquisition);
    --slot->reference_count;
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourceManager::Evict(ResourceId id)
{
    if (!id.IsValid())
    {
        return ResourceFailure("resource.invalid_id", "resource id must be valid before eviction");
    }

    ResourceSlot* slot = cache_.Find(id);
    if (slot == nullptr)
    {
        return ResourceFailure("resource.not_found", "resource slot was not found for eviction");
    }
    if (slot->reference_count != 0)
    {
        return ResourceFailure("resource.in_use", "resource cannot be evicted while references are held");
    }

    const auto next_generation = NextGeneration(slot->generation);
    if (!next_generation)
    {
        return foundation::Result<void>::Failure(next_generation.GetError());
    }

    // Evicting is an explicit retryable prefix-progress state. Successfully released
    // dependency leases are removed from dependency_handles, so retries continue only
    // the unfinished cleanup and never release a lease twice.
    slot->state = ResourceState::Evicting;
    const auto released_dependencies = ReleaseDependencyHandles(*slot);
    if (!released_dependencies)
    {
        return foundation::Result<void>::Failure(released_dependencies.GetError());
    }
    dependency_graph_.RemoveDependencies(slot->id);
    RemovePayload(*slot);
    slot->pending_artifact.reset();
    slot->generation = next_generation.Value();
    slot->state = ResourceState::Evicted;
    return foundation::Result<void>::Success();
}

std::size_t ResourceManager::EvictUnreferenced()
{
    std::size_t evicted_count = 0;
    for (auto& [resource_id, slot] : cache_.Entries())
    {
        (void)resource_id;
        if (slot.reference_count != 0 ||
            (slot.state != ResourceState::Ready && slot.state != ResourceState::Failed && slot.state != ResourceState::Evicting))
        {
            continue;
        }
        const auto next_generation = NextGeneration(slot.generation);
        if (!next_generation)
        {
            continue;
        }
        slot.state = ResourceState::Evicting;
        const auto released_dependencies = ReleaseDependencyHandles(slot);
        if (!released_dependencies)
        {
            continue;
        }
        dependency_graph_.RemoveDependencies(slot.id);
        RemovePayload(slot);
        slot.pending_artifact.reset();
        slot.generation = next_generation.Value();
        slot.state = ResourceState::Evicted;
        ++evicted_count;
    }
    return evicted_count;
}

foundation::Result<void> ResourceManager::ValidateHandle(ResourceHandle handle) const
{
    if (!handle.IsValid())
    {
        return ResourceFailure("resource.invalid_handle", "resource handle must be valid");
    }
    const ResourceSlot* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return ResourceFailure("resource.stale_handle", "resource handle is unknown or stale");
    }
    return foundation::Result<void>::Success();
}

ResourceState ResourceManager::GetState(ResourceHandle handle) const
{
    const ResourceSlot* slot = cache_.Find(handle.id);
    if (!handle.IsValid() || slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return ResourceState::Unknown;
    }
    return slot->state;
}

bool ResourceManager::IsReady(ResourceHandle handle) const
{
    return GetState(handle) == ResourceState::Ready;
}

std::optional<ResourceId> ResourceManager::GetResourceId(ResourceHandle handle) const
{
    const ResourceSlot* slot = cache_.Find(handle.id);
    if (!handle.IsValid() || slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return std::nullopt;
    }
    return slot->id;
}

ResourcePayloadPtr ResourceManager::GetPayload(ResourceHandle handle) const
{
    const ResourceSlot* slot = cache_.Find(handle.id);
    if (!handle.IsValid() || slot == nullptr || !IsHandleCurrent(*slot, handle) || slot->state != ResourceState::Ready)
    {
        return {};
    }
    return slot->payload;
}

void ResourceManager::SetMemoryBudgetBytes(std::size_t bytes)
{
    memory_budget_bytes_ = bytes;
}

ResourceMemoryStats ResourceManager::GetMemoryStatistics() const
{
    ResourceMemoryStats stats{};
    stats.resident_bytes = resident_bytes_;
    stats.budget_bytes = memory_budget_bytes_;
    stats.slot_count = cache_.Entries().size();
    stats.resource_count = cache_.Entries().size();
    stats.invariant_failure_count = invariant_failure_count_;
    for (const auto& [resource_id, slot] : cache_.Entries())
    {
        (void)resource_id;
        if (slot.state == ResourceState::Ready)
        {
            if (stats.ready_count != std::numeric_limits<std::size_t>::max()) ++stats.ready_count;
            stats.ready_bytes = slot.memory_bytes > std::numeric_limits<std::size_t>::max() - stats.ready_bytes
                                    ? std::numeric_limits<std::size_t>::max()
                                    : stats.ready_bytes + slot.memory_bytes;
            if (slot.reference_count == 0)
            {
                stats.cached_unreferenced_bytes = slot.memory_bytes > std::numeric_limits<std::size_t>::max() - stats.cached_unreferenced_bytes
                                                    ? std::numeric_limits<std::size_t>::max()
                                                    : stats.cached_unreferenced_bytes + slot.memory_bytes;
            }
        }
    }
    return stats;
}

const ResourceSlot* ResourceManager::InspectSlot(ResourceId id) const
{
    return cache_.Find(id);
}

bool ResourceManager::IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle)
{
    return slot.id == handle.id && slot.generation == handle.generation;
}

foundation::Result<ResourceAcquisitionId> ResourceManager::PeekAcquisitionId() const
{
    if (!CanAllocateMonotonicId(next_acquisition_id_))
    {
        return ResourceFailureValue<ResourceAcquisitionId>("resource.acquisition_id_exhausted", "resource acquisition id allocator is exhausted");
    }
    return foundation::Result<ResourceAcquisitionId>::Success(next_acquisition_id_);
}

void ResourceManager::CommitAcquisitionId() noexcept
{
    if (next_acquisition_id_ == std::numeric_limits<ResourceAcquisitionId>::max())
    {
        next_acquisition_id_ = 0;
    }
    else if (next_acquisition_id_ != 0)
    {
        ++next_acquisition_id_;
    }
}

foundation::Result<ResourceGeneration> ResourceManager::NextGeneration(ResourceGeneration generation)
{
    if (generation == 0 || generation == std::numeric_limits<ResourceGeneration>::max())
    {
        return ResourceFailureValue<ResourceGeneration>("resource.generation_exhausted", "resource generation cannot be advanced without stale-handle ABA");
    }
    return foundation::Result<ResourceGeneration>::Success(static_cast<ResourceGeneration>(generation + 1));
}

foundation::Result<void> ResourceManager::ValidateRequest(const ResourceRequest& request)
{
    if (!request.resource_id.IsValid())
    {
        return ResourceFailure("resource.invalid_id", "resource id must be valid before requesting");
    }
    if (!request.type.IsValid())
    {
        return ResourceFailure("resource.invalid_type", "resource type must be valid before requesting");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ResourceManager::ReleaseDependencyHandles(ResourceSlot& slot)
{
    return ReleaseDependencyHandles(slot.dependency_handles);
}

foundation::Result<void> ResourceManager::ReleaseDependencyHandles(std::vector<OwnedResourceDependency>& handles)
{
    std::optional<foundation::Error> first_error;
    for (auto iterator = handles.begin(); iterator != handles.end();)
    {
        const auto released = Release(iterator->lease);
        if (released)
        {
            iterator = handles.erase(iterator);
            continue;
        }

        if (!first_error.has_value())
        {
            first_error = released.GetError();
        }
        ++iterator;
    }

    if (first_error.has_value())
    {
        RecordInvariantFailure();
        return foundation::Result<void>::Failure(*first_error);
    }
    return foundation::Result<void>::Success();
}

void ResourceManager::RecordInvariantFailure() noexcept
{
    if (invariant_failure_count_ != std::numeric_limits<std::size_t>::max())
    {
        ++invariant_failure_count_;
    }
}

foundation::Result<void> ResourceManager::RollbackLoadAttempt(ResourceSlot& slot)
{
    const auto released = ReleaseDependencyHandles(slot);
    dependency_graph_.RemoveDependencies(slot.id);
    RemovePayload(slot);
    slot.pending_artifact.reset();
    return released;
}

foundation::Result<void> ResourceManager::LoadSlot(ResourceSlot& slot, const ResourceRequest& request, ResourceProcessingStats& stats)
{
    if (!slot.dependency_handles.empty())
    {
        const auto released = ReleaseDependencyHandles(slot);
        if (!released)
        {
            slot.state = ResourceState::Failed;
            return ResourceFailure("resource.dependency_release_pending", "resource dependency lease cleanup must succeed before retrying load");
        }
    }

    if (loading_resources_.contains(slot.id))
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.dependency_cycle", "resource dependency cycle detected during load");
    }
    if (loader_registry_ == nullptr)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_registry_missing", "resource loader registry is required while processing resource loads");
    }

    IResourceLoader* loader = loader_registry_->FindLoader(request.type);
    if (loader == nullptr)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_not_found", "resource loader is not registered for the requested type");
    }

    loading_resources_.insert(slot.id);
    LoadingResourceGuard loading_guard{loading_resources_, slot.id};
    slot.state = ResourceState::Loading;

    try
    {
        const auto load_result = loader->Load(request);
        if (!load_result)
        {
            slot.state = ResourceState::Failed;
            (void)RollbackLoadAttempt(slot);
            return foundation::Result<void>::Failure(load_result.GetError());
        }
        return FinishLoadedArtifact(slot, load_result.Value(), stats);
    }
    catch (const std::exception&)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_exception", "resource loader threw an exception");
    }
    catch (...)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_exception", "resource loader threw an unknown exception");
    }
}

foundation::Result<void> ResourceManager::FinishLoadedArtifact(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats)
{
    if (artifact.resource_id != slot.id)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_mismatched_id", "resource loader returned an artifact for a different resource id");
    }
    if (artifact.type != slot.type)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.loader_mismatched_type", "resource loader returned an artifact for a different resource type");
    }
    if (!artifact.payload)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.invalid_payload", "resource loader returned an empty payload");
    }

    std::vector<ResourceDependency> blocking_dependencies;
    for (const ResourceDependency& dependency : artifact.dependencies)
    {
        if (!dependency.required)
        {
            continue;
        }
        if (dependency.resource_id == slot.id || HasDependencyPath(dependency.resource_id, slot.id))
        {
            slot.state = ResourceState::Failed;
            (void)RollbackLoadAttempt(slot);
            return ResourceFailure("resource.dependency_cycle", "resource dependency cycle detected during load");
        }
        blocking_dependencies.push_back(dependency);
    }

    dependency_graph_.RemoveDependencies(slot.id);
    if (!blocking_dependencies.empty())
    {
        dependency_graph_.SetDependencies(slot.id, std::move(blocking_dependencies));
    }
    const auto dependencies_ready = ResolveDependencies(slot, artifact);
    if (!dependencies_ready)
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return foundation::Result<void>::Failure(dependencies_ready.GetError());
    }
    if (!dependencies_ready.Value())
    {
        slot.state = ResourceState::WaitingForDependencies;
        slot.pending_artifact = std::move(artifact);
        load_queue_.Enqueue(ResourceLoadJob{ResourceRequest{slot.id, slot.type}, slot.generation});
        return foundation::Result<void>::Success();
    }

    return CommitReadyPayload(slot, std::move(artifact), stats);
}

foundation::Result<bool> ResourceManager::ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact)
{
    if (artifact.dependencies.empty())
    {
        return foundation::Result<bool>::Success(true);
    }

    if (slot.dependency_handles.empty())
    {
        for (const ResourceDependency& dependency : artifact.dependencies)
        {
            if (!dependency.resource_id.IsValid())
            {
                if (dependency.required)
                {
                    const auto released = ReleaseDependencyHandles(slot);
                    if (!released)
                    {
                        return foundation::Result<bool>::Failure(released.GetError());
                    }
                    return ResourceFailureValue<bool>("resource.dependency_invalid_id", "required resource dependency must have a valid resource id");
                }
                continue;
            }
            if (!dependency.type.IsValid())
            {
                if (dependency.required)
                {
                    const auto released = ReleaseDependencyHandles(slot);
                    if (!released)
                    {
                        return foundation::Result<bool>::Failure(released.GetError());
                    }
                    return ResourceFailureValue<bool>("resource.dependency_invalid_type", "required resource dependency must have a valid resource type");
                }
                continue;
            }
            if (dependency.resource_id == slot.id)
            {
                const auto released = ReleaseDependencyHandles(slot);
                if (!released)
                {
                    return foundation::Result<bool>::Failure(released.GetError());
                }
                return ResourceFailureValue<bool>("resource.dependency_cycle", "resource dependency cycle detected during load");
            }

            if (!dependency.required && (loader_registry_ == nullptr || loader_registry_->FindLoader(dependency.type) == nullptr))
            {
                continue;
            }
            if (dependency.required && loader_registry_ != nullptr && loader_registry_->FindLoader(dependency.type) == nullptr)
            {
                const auto released = ReleaseDependencyHandles(slot);
                if (!released)
                {
                    return foundation::Result<bool>::Failure(released.GetError());
                }
                return ResourceFailureValue<bool>("resource.loader_not_found", "resource loader is not registered for a required dependency");
            }

            const auto dependency_result = RequestLease(ResourceRequest{dependency.resource_id, dependency.type});
            if (!dependency_result)
            {
                if (!dependency.required)
                {
                    continue;
                }
                const auto released = ReleaseDependencyHandles(slot);
                if (!released)
                {
                    return foundation::Result<bool>::Failure(released.GetError());
                }
                return foundation::Result<bool>::Failure(dependency_result.GetError());
            }
            const ResourceLease dependency_lease = dependency_result.Value();
            try
            {
                if (fail_next_dependency_publish_for_testing_)
                {
                    fail_next_dependency_publish_for_testing_ = false;
                    throw std::bad_alloc{};
                }
                slot.dependency_handles.push_back(OwnedResourceDependency{dependency_lease, dependency.resource_id, dependency.required});
            }
            catch (...)
            {
                const auto released = Release(dependency_lease);
                if (!released)
                {
                    return foundation::Result<bool>::Failure(released.GetError());
                }
                return ResourceFailureValue<bool>("resource.allocation_failed", "resource dependency ownership could not be published");
            }
        }
    }

    for (auto iterator = slot.dependency_handles.begin(); iterator != slot.dependency_handles.end();)
    {
        const ResourceHandle dependency_handle = iterator->lease.resource;
        const ResourceState state = GetState(dependency_handle);
        if (state == ResourceState::Failed || state == ResourceState::Unknown || state == ResourceState::Evicted)
        {
            if (iterator->required)
            {
                return ResourceFailureValue<bool>("resource.dependency_failed", "resource dependency failed before root became ready");
            }
            const auto released = Release(iterator->lease);
            if (!released)
            {
                return foundation::Result<bool>::Failure(released.GetError());
            }
            iterator = slot.dependency_handles.erase(iterator);
            continue;
        }
        if (state != ResourceState::Ready)
        {
            if (iterator->required)
            {
                return foundation::Result<bool>::Success(false);
            }
            const auto released = Release(iterator->lease);
            if (!released)
            {
                return foundation::Result<bool>::Failure(released.GetError());
            }
            iterator = slot.dependency_handles.erase(iterator);
            continue;
        }
        ++iterator;
    }
    return foundation::Result<bool>::Success(true);
}

bool ResourceManager::HasDependencyPath(ResourceId from, ResourceId to) const
{
    if (from == to)
    {
        return true;
    }
    std::unordered_set<ResourceId> visited;
    std::vector<ResourceId> stack;
    stack.push_back(from);
    while (!stack.empty())
    {
        const ResourceId current = stack.back();
        stack.pop_back();
        if (current == to)
        {
            return true;
        }
        if (!visited.insert(current).second)
        {
            continue;
        }
        const auto dependencies = dependency_graph_.FindDependencies(current);
        if (!dependencies)
        {
            continue;
        }
        for (const ResourceDependency& dependency : dependencies->dependencies)
        {
            if (dependency.required)
            {
                stack.push_back(dependency.resource_id);
            }
        }
    }
    return false;
}

foundation::Result<void> ResourceManager::CommitReadyPayload(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats)
{
    const std::size_t bytes = artifact.payload->GetSizeBytes();
    if (bytes > std::numeric_limits<std::size_t>::max() - resident_bytes_ ||
        bytes > std::numeric_limits<std::size_t>::max() - stats.bytes_loaded ||
        stats.loaded_resources == std::numeric_limits<std::size_t>::max())
    {
        slot.state = ResourceState::Failed;
        (void)RollbackLoadAttempt(slot);
        return ResourceFailure("resource.accounting_overflow", "resource residency/statistics accounting cannot represent loaded payload");
    }
    if (!CanFit(bytes))
    {
        (void)EvictUnreferenced();
        if (!CanFit(bytes))
        {
            slot.state = ResourceState::Failed;
            (void)RollbackLoadAttempt(slot);
            return ResourceFailure("resource.memory_budget_exceeded", "resource payload would exceed resource memory budget");
        }
    }

    // All fallible/accounting validation is complete before payload publication.
    RemovePayload(slot);
    slot.payload = std::move(artifact.payload);
    slot.memory_bytes = bytes;
    resident_bytes_ += bytes;
    slot.pending_artifact.reset();
    slot.state = ResourceState::Ready;
    ++stats.loaded_resources;
    stats.bytes_loaded += bytes;
    return foundation::Result<void>::Success();
}

bool ResourceManager::CanFit(std::size_t bytes) const noexcept
{
    if (memory_budget_bytes_ == 0)
    {
        return true;
    }

    return resident_bytes_ <= memory_budget_bytes_ && bytes <= memory_budget_bytes_ - resident_bytes_;
}

void ResourceManager::RemovePayload(ResourceSlot& slot)
{
    if (slot.payload)
    {
        resident_bytes_ -= std::min(resident_bytes_, slot.memory_bytes);
    }
    slot.payload.reset();
    slot.memory_bytes = 0;
}
} // namespace epidemic::runtime


