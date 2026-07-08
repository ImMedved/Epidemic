#include "resource_manager.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
namespace
{
constexpr ResourceGeneration kInitialGeneration = 1;
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

    auto& slot = cache_.FindOrCreate(request.resource_id, request.type);
    ActivateSlot(slot, request.type);
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

void ResourceManager::ActivateSlot(ResourceSlot& slot, ResourceType type)
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

    slot.state = ResourceState::Ready;
}
} // namespace epidemic::runtime
