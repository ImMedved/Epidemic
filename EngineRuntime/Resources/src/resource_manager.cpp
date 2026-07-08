#include "resource_manager.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
namespace
{
constexpr ResourceGeneration kInitialGeneration = 1;
}

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
        iterator->second.reference_count = 0;
    }

    return iterator->second;
}

ResourceManager::ResourceManager(IResourceLoaderRegistry* loader_registry) : loader_registry_(loader_registry)
{
}

foundation::Result<ResourceHandle> ResourceManager::Request(ResourceRequest request)
{
    if (!request.resource_id.IsValid())
    {
        return foundation::Result<ResourceHandle>::Failure(
            foundation::Error::Create("resource.invalid_id", "resource id must be valid before requesting"));
    }

    if (!request.type.IsValid())
    {
        return foundation::Result<ResourceHandle>::Failure(
            foundation::Error::Create("resource.invalid_type", "resource type must be valid before requesting"));
    }

    if (loader_registry_ == nullptr)
    {
        return foundation::Result<ResourceHandle>::Failure(
            foundation::Error::Create("resource.loader_registry_missing", "resource loader registry is required before requesting resources"));
    }

    auto* loader = loader_registry_->FindLoader(request.type);
    if (loader == nullptr)
    {
        return foundation::Result<ResourceHandle>::Failure(
            foundation::Error::Create("resource.loader_not_found", "resource loader is not registered for the requested type"));
    }

    auto& slot = cache_.FindOrCreate(request.resource_id, request.type);
    if (slot.state != ResourceState::Ready)
    {
        PrepareSlotForLoad(slot, request.type);
        const auto load_result = LoadSlot(slot, request);
        if (!load_result)
        {
            return foundation::Result<ResourceHandle>::Failure(load_result.GetError());
        }
    }

    ++slot.reference_count;
    return foundation::Result<ResourceHandle>::Success(ResourceHandle{slot.id, slot.generation});
}

void ResourceManager::Release(ResourceHandle handle)
{
    if (!handle.IsValid())
    {
        return;
    }

    auto* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle) || slot->reference_count == 0)
    {
        return;
    }

    --slot->reference_count;
    if (slot->reference_count == 0)
    {
        slot->state = ResourceState::Evicted;
    }
}

ResourceState ResourceManager::GetState(ResourceHandle handle) const
{
    if (!handle.IsValid())
    {
        return ResourceState::Evicted;
    }

    const auto* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return ResourceState::Evicted;
    }

    return slot->state;
}

bool ResourceManager::IsReady(ResourceHandle handle) const
{
    return GetState(handle) == ResourceState::Ready;
}

std::optional<ResourceId> ResourceManager::GetResourceId(ResourceHandle handle) const
{
    if (!handle.IsValid())
    {
        return std::nullopt;
    }

    const auto* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return std::nullopt;
    }

    return slot->id;
}

bool ResourceManager::IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle)
{
    return slot.id == handle.id && slot.generation == handle.generation;
}

void ResourceManager::PrepareSlotForLoad(ResourceSlot& slot, ResourceType type)
{
    slot.type = type;
    if (slot.state == ResourceState::Evicted)
    {
        ++slot.generation;
    }

    if (slot.generation == 0)
    {
        slot.generation = kInitialGeneration;
    }

    slot.state = ResourceState::Queued;
}

foundation::Result<void> ResourceManager::LoadSlot(ResourceSlot& slot, ResourceRequest request)
{
    load_queue_.Enqueue(ResourceLoadJob{request, slot.generation});

    const auto job = load_queue_.Dequeue();
    if (!job)
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource.load_queue_empty", "resource load queue failed to return a queued job"));
    }

    auto* loader = loader_registry_->FindLoader(job->request.type);
    if (loader == nullptr)
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource.loader_not_found", "resource loader is not registered for the requested type"));
    }

    slot.state = ResourceState::Loading;
    const auto load_result = loader->Load(job->request);
    if (!load_result)
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(load_result.GetError());
    }

    const ResourceLoadArtifact& artifact = load_result.Value();
    if (artifact.resource_id != slot.id)
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource.loader_mismatched_id", "resource loader returned an artifact for a different resource id"));
    }

    if (artifact.type != slot.type)
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource.loader_mismatched_type", "resource loader returned an artifact for a different resource type"));
    }

    slot.state = ResourceState::Ready;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime
