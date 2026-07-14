#include "resource_manager.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <string_view>
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
} // namespace

void ResourceLoadQueue::Enqueue(ResourceLoadJob job)
{
    jobs_.push_back(std::move(job));
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

foundation::Result<ResourceHandle> ResourceManager::Request(ResourceRequest request)
{
    const auto valid = ValidateRequest(request);
    if (!valid)
    {
        return foundation::Result<ResourceHandle>::Failure(valid.GetError());
    }

    ResourceSlot* existing_slot = cache_.Find(request.resource_id);
    if (existing_slot != nullptr && existing_slot->type.IsValid() && existing_slot->type != request.type)
    {
        return ResourceFailureValue<ResourceHandle>("resource.type_mismatch", "resource id is already associated with a different type");
    }

    ResourceSlot& slot = cache_.FindOrCreate(request.resource_id, request.type);
    slot.type = request.type;
    ++slot.reference_count;

    if (slot.state == ResourceState::Ready || slot.state == ResourceState::Queued || slot.state == ResourceState::Loading ||
        slot.state == ResourceState::WaitingForDependencies)
    {
        return foundation::Result<ResourceHandle>::Success(ResourceHandle{slot.id, slot.generation});
    }

    QueueSlot(load_queue_, slot, request);
    return foundation::Result<ResourceHandle>::Success(ResourceHandle{slot.id, slot.generation});
}

foundation::Result<ResourceProcessingStats> ResourceManager::ProcessPendingLoads(RuntimeBudget budget)
{
    ResourceProcessingStats stats{};
    const std::size_t max_jobs = budget.HasItemLimit() ? budget.max_items : std::numeric_limits<std::size_t>::max();

    while (!load_queue_.IsEmpty() && stats.processed_jobs < max_jobs)
    {
        std::optional<ResourceLoadJob> job = load_queue_.Dequeue();
        if (!job)
        {
            break;
        }

        ResourceSlot* slot = cache_.Find(job->request.resource_id);
        if (slot == nullptr || slot->generation != job->generation)
        {
            continue;
        }
        if (slot->reference_count == 0)
        {
            slot->state = ResourceState::Unloaded;
            slot->pending_artifact.reset();
            continue;
        }

        ++stats.processed_jobs;
        foundation::Result<void> result = slot->pending_artifact ? FinishLoadedArtifact(*slot, std::move(*slot->pending_artifact), stats)
                                                                 : LoadSlot(*slot, job->request, stats);
        if (!result)
        {
            ++stats.failed_resources;
            return foundation::Result<ResourceProcessingStats>::Failure(result.GetError());
        }
    }

    return foundation::Result<ResourceProcessingStats>::Success(stats);
}

void ResourceManager::Release(ResourceHandle handle)
{
    if (!handle.IsValid())
    {
        return;
    }

    ResourceSlot* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle) || slot->reference_count == 0)
    {
        return;
    }
    --slot->reference_count;
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

    slot->state = ResourceState::Evicting;
    ReleaseDependencyHandles(*slot);
    RemovePayload(*slot);
    slot->pending_artifact.reset();
    slot->generation = NextGeneration(slot->generation);
    slot->state = ResourceState::Evicted;
    return foundation::Result<void>::Success();
}

std::size_t ResourceManager::EvictUnreferenced()
{
    std::size_t evicted_count = 0;
    for (auto& [resource_id, slot] : cache_.Entries())
    {
        (void)resource_id;
        if (slot.reference_count == 0 && slot.state != ResourceState::Evicted)
        {
            slot.state = ResourceState::Evicting;
            ReleaseDependencyHandles(slot);
            RemovePayload(slot);
            slot.pending_artifact.reset();
            slot.generation = NextGeneration(slot.generation);
            slot.state = ResourceState::Evicted;
            ++evicted_count;
        }
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
        return ResourceFailure("resource.handle_stale", "resource handle is unknown or stale");
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

std::shared_ptr<IResourcePayload> ResourceManager::GetPayload(ResourceHandle handle) const
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

ResourceMemoryStats ResourceManager::GetMemoryStats() const
{
    ResourceMemoryStats stats{};
    stats.resident_bytes = resident_bytes_;
    stats.budget_bytes = memory_budget_bytes_;
    stats.slot_count = cache_.Entries().size();
    for (const auto& [resource_id, slot] : cache_.Entries())
    {
        (void)resource_id;
        if (slot.state == ResourceState::Ready)
        {
            ++stats.ready_count;
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

ResourceGeneration ResourceManager::NextGeneration(ResourceGeneration generation)
{
    if (generation == 0 || generation == std::numeric_limits<ResourceGeneration>::max())
    {
        return kInitialGeneration;
    }
    return generation + 1;
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

void ResourceManager::QueueSlot(ResourceLoadQueue& queue, ResourceSlot& slot, ResourceRequest request)
{
    slot.state = ResourceState::Queued;
    slot.pending_artifact.reset();
    queue.Enqueue(ResourceLoadJob{std::move(request), slot.generation});
}

void ResourceManager::ReleaseDependencyHandles(ResourceSlot& slot)
{
    ReleaseDependencyHandles(slot.dependency_handles);
}

void ResourceManager::ReleaseDependencyHandles(std::vector<ResourceHandle>& handles)
{
    for (ResourceHandle handle : handles)
    {
        Release(handle);
    }
    handles.clear();
}

foundation::Result<void> ResourceManager::LoadSlot(ResourceSlot& slot, const ResourceRequest& request, ResourceProcessingStats& stats)
{
    if (loading_resources_.contains(slot.id))
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.dependency_cycle", "resource dependency cycle detected during load");
    }
    if (loader_registry_ == nullptr)
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.loader_registry_missing", "resource loader registry is required while processing resource loads");
    }

    IResourceLoader* loader = loader_registry_->FindLoader(request.type);
    if (loader == nullptr)
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.loader_not_found", "resource loader is not registered for the requested type");
    }

    loading_resources_.insert(slot.id);
    slot.state = ResourceState::Loading;
    const auto load_result = loader->Load(request);
    loading_resources_.erase(slot.id);
    if (!load_result)
    {
        slot.state = ResourceState::Failed;
        slot.pending_artifact.reset();
        return foundation::Result<void>::Failure(load_result.GetError());
    }

    return FinishLoadedArtifact(slot, load_result.Value(), stats);
}

foundation::Result<void> ResourceManager::FinishLoadedArtifact(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats)
{
    if (artifact.resource_id != slot.id)
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.loader_mismatched_id", "resource loader returned an artifact for a different resource id");
    }
    if (artifact.type != slot.type)
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.loader_mismatched_type", "resource loader returned an artifact for a different resource type");
    }
    if (!artifact.payload)
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.invalid_payload", "resource loader returned an empty payload");
    }

    dependency_graph_.SetDependencies(slot.id, artifact.dependencies);
    const auto dependencies_ready = ResolveDependencies(slot, artifact);
    if (!dependencies_ready)
    {
        slot.state = ResourceState::Failed;
        slot.pending_artifact.reset();
        return foundation::Result<void>::Failure(dependencies_ready.GetError());
    }
    if (!dependencies_ready.Value())
    {
        slot.state = ResourceState::WaitingForDependencies;
        slot.pending_artifact = std::move(artifact);
        load_queue_.Enqueue(ResourceLoadJob{ResourceRequest{slot.id, slot.type, {}}, slot.generation});
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
        std::vector<ResourceHandle> acquired_handles;
        for (const ResourceDependency& dependency : artifact.dependencies)
        {
            if (!dependency.resource_id.IsValid())
            {
                if (dependency.required)
                {
                    ReleaseDependencyHandles(acquired_handles);
                    return ResourceFailureValue<bool>("resource.dependency_invalid_id", "required resource dependency must have a valid resource id");
                }
                continue;
            }
            if (!dependency.type.IsValid())
            {
                if (dependency.required)
                {
                    ReleaseDependencyHandles(acquired_handles);
                    return ResourceFailureValue<bool>("resource.dependency_invalid_type", "required resource dependency must have a valid resource type");
                }
                continue;
            }
            if (dependency.resource_id == slot.id)
            {
                ReleaseDependencyHandles(acquired_handles);
                return ResourceFailureValue<bool>("resource.dependency_cycle", "resource dependency cycle detected during load");
            }

            if (!dependency.required && (loader_registry_ == nullptr || loader_registry_->FindLoader(dependency.type) == nullptr))
            {
                continue;
            }
            if (dependency.required && loader_registry_ != nullptr && loader_registry_->FindLoader(dependency.type) == nullptr)
            {
                ReleaseDependencyHandles(acquired_handles);
                return ResourceFailureValue<bool>("resource.loader_not_found", "resource loader is not registered for a required dependency");
            }

            const auto dependency_result = Request(ResourceRequest{dependency.resource_id, dependency.type, {}});
            if (!dependency_result)
            {
                ReleaseDependencyHandles(acquired_handles);
                return foundation::Result<bool>::Failure(dependency_result.GetError());
            }
            acquired_handles.push_back(dependency_result.Value());
        }
        slot.dependency_handles = std::move(acquired_handles);
    }

    for (ResourceHandle dependency_handle : slot.dependency_handles)
    {
        const ResourceState state = GetState(dependency_handle);
        if (state == ResourceState::Failed || state == ResourceState::Unknown || state == ResourceState::Evicted)
        {
            return ResourceFailureValue<bool>("resource.dependency_failed", "resource dependency failed before root became ready");
        }
        if (state != ResourceState::Ready)
        {
            return foundation::Result<bool>::Success(false);
        }
    }
    return foundation::Result<bool>::Success(true);
}

foundation::Result<void> ResourceManager::CommitReadyPayload(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats)
{
    const std::size_t bytes = artifact.payload->GetSizeBytes();
    if (!CanFit(bytes))
    {
        slot.state = ResourceState::Failed;
        return ResourceFailure("resource.memory_budget_exceeded", "resource payload would exceed resource memory budget");
    }

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
    return memory_budget_bytes_ == 0 || resident_bytes_ + bytes <= memory_budget_bytes_;
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


