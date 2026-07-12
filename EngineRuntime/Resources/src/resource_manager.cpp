#include "resource_manager.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

#include <limits>

namespace epidemic::runtime
{
namespace
{
constexpr ResourceGeneration kInitialGeneration = 1;
}

// Function note: Handles enqueue.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void ResourceLoadQueue::Enqueue(ResourceLoadJob job)
{
    jobs_.push_back(std::move(job));
}

// Function note: Checks empty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool ResourceLoadQueue::IsEmpty() const noexcept
{
    return jobs_.empty();
}

// Function note: Handles dequeue.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Finds the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceSlot* ResourceCache::Find(ResourceId id)
{
    const auto iterator = slots_.find(id);
    if (iterator == slots_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

// Function note: Finds the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const ResourceSlot* ResourceCache::Find(ResourceId id) const
{
    const auto iterator = slots_.find(id);
    if (iterator == slots_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

// Function note: Finds or create.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles entries.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::unordered_map<ResourceId, ResourceSlot>& ResourceCache::Entries()
{
    return slots_;
}

// Function note: Handles entries.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const std::unordered_map<ResourceId, ResourceSlot>& ResourceCache::Entries() const
{
    return slots_;
}

// Function note: Handles resource manager.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceManager::ResourceManager(IResourceLoaderRegistry* loader_registry) : loader_registry_(loader_registry)
{
}

// Function note: Handles request.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<ResourceHandle> ResourceManager::Request(ResourceRequest request)
{
    if (!request.resource_id.IsValid())
    {
        return foundation::Result<ResourceHandle>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.invalid_id", "resource id must be valid before requesting"));
    }

    if (!request.type.IsValid())
    {
        return foundation::Result<ResourceHandle>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.invalid_type", "resource type must be valid before requesting"));
    }

    auto* existing_slot = cache_.Find(request.resource_id);
    if (existing_slot != nullptr && existing_slot->type.IsValid() && existing_slot->type != request.type)
    {
        return foundation::Result<ResourceHandle>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.type_mismatch", "resource id is already associated with a different type"));
    }

    if (loader_registry_ == nullptr)
    {
        return foundation::Result<ResourceHandle>::Failure(foundation::Error::Create(
            "resource.loader_registry_missing", "resource loader registry is required before requesting resources"));
    }

    auto* loader = loader_registry_->FindLoader(request.type);
    if (loader == nullptr)
    {
        return foundation::Result<ResourceHandle>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.loader_not_found", "resource loader is not registered for the requested type"));
    }

    auto& slot = cache_.FindOrCreate(request.resource_id, request.type);
    if (slot.state != ResourceState::Ready)
    {
        // Function note: Prepares slot for load.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        PrepareSlotForLoad(slot, request.type);
        // Function note: Loads slot.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        const auto load_result = LoadSlot(slot, request);
        if (!load_result)
        {
            return foundation::Result<ResourceHandle>::Failure(load_result.GetError());
        }
    }

    ++slot.reference_count;
    return foundation::Result<ResourceHandle>::Success(ResourceHandle{slot.id, slot.generation});
}

// Function note: Releases the associated runtime state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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
        // Function note: Releases dependency handles.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        ReleaseDependencyHandles(*slot);
        slot->state = ResourceState::Unloaded;
        // Function note: Handles next generation.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        slot->generation = NextGeneration(slot->generation);
    }
}

// Function note: Handles evict.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> ResourceManager::Evict(ResourceId id)
{
    if (!id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.invalid_id", "resource id must be valid before eviction"));
    }

    auto* slot = cache_.Find(id);
    if (slot == nullptr)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.not_found", "resource slot was not found for eviction"));
    }

    if (slot->reference_count != 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.in_use", "resource cannot be evicted while references are held"));
    }

    // Function note: Releases dependency handles.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ReleaseDependencyHandles(*slot);
    slot->state = ResourceState::Evicted;
    return foundation::Result<void>::Success();
}

// Function note: Handles evict unreferenced.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::size_t ResourceManager::EvictUnreferenced()
{
    std::size_t evicted_count = 0;
    for (auto& [resource_id, slot] : cache_.Entries())
    {
        (void)resource_id;
        if (slot.reference_count == 0 && slot.state != ResourceState::Evicted)
        {
            // Function note: Releases dependency handles.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            ReleaseDependencyHandles(slot);
            slot.state = ResourceState::Evicted;
            ++evicted_count;
        }
    }

    return evicted_count;
}

// Function note: Gets state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceState ResourceManager::GetState(ResourceHandle handle) const
{
    if (!handle.IsValid())
    {
        return ResourceState::Unknown;
    }

    const auto* slot = cache_.Find(handle.id);
    if (slot == nullptr || !IsHandleCurrent(*slot, handle))
    {
        return ResourceState::Unknown;
    }

    return slot->state;
}

// Function note: Checks ready.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool ResourceManager::IsReady(ResourceHandle handle) const
{
    return GetState(handle) == ResourceState::Ready;
}

// Function note: Gets resource id.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles inspect slot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const ResourceSlot* ResourceManager::InspectSlot(ResourceId id) const
{
    return cache_.Find(id);
}

// Function note: Checks handle current.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool ResourceManager::IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle)
{
    return slot.id == handle.id && slot.generation == handle.generation;
}

// Function note: Handles next generation.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ResourceGeneration ResourceManager::NextGeneration(ResourceGeneration generation)
{
    if (generation == 0 || generation == std::numeric_limits<ResourceGeneration>::max())
    {
        return kInitialGeneration;
    }

    return generation + 1;
}

// Function note: Prepares slot for load.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void ResourceManager::PrepareSlotForLoad(ResourceSlot& slot, ResourceType type)
{
    slot.type = type;
    if (slot.generation == 0)
    {
        slot.generation = kInitialGeneration;
    }

    slot.state = ResourceState::Queued;
}

// Function note: Releases dependency handles.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void ResourceManager::ReleaseDependencyHandles(ResourceSlot& slot)
{
    // Function note: Releases dependency handles.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ReleaseDependencyHandles(slot.dependency_handles);
}

// Function note: Releases dependency handles.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void ResourceManager::ReleaseDependencyHandles(std::vector<ResourceHandle>& handles)
{
    for (const ResourceHandle handle : handles)
    {
        // Function note: Releases the associated runtime state.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        Release(handle);
    }

    handles.clear();
}

// Function note: Loads slot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> ResourceManager::LoadSlot(ResourceSlot& slot, ResourceRequest request)
{
    if (loading_resources_.contains(slot.id))
    {
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.dependency_cycle", "resource dependency cycle detected during load"));
    }

    loading_resources_.insert(slot.id);
    load_queue_.Enqueue(ResourceLoadJob{request, slot.generation});

    const auto job = load_queue_.Dequeue();
    if (!job)
    {
        loading_resources_.erase(slot.id);
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.load_queue_empty", "resource load queue failed to return a queued job"));
    }

    auto* loader = loader_registry_->FindLoader(job->request.type);
    if (loader == nullptr)
    {
        loading_resources_.erase(slot.id);
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource.loader_not_found", "resource loader is not registered for the requested type"));
    }

    slot.state = ResourceState::Loading;
    const auto load_result = loader->Load(job->request);
    if (!load_result)
    {
        loading_resources_.erase(slot.id);
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(load_result.GetError());
    }

    const ResourceLoadArtifact& artifact = load_result.Value();
    if (artifact.resource_id != slot.id)
    {
        loading_resources_.erase(slot.id);
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "resource.loader_mismatched_id", "resource loader returned an artifact for a different resource id"));
    }

    if (artifact.type != slot.type)
    {
        loading_resources_.erase(slot.id);
        slot.state = ResourceState::Failed;
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "resource.loader_mismatched_type", "resource loader returned an artifact for a different resource type"));
    }

    dependency_graph_.SetDependencies(slot.id, artifact.dependencies);
    // Function note: Resolves dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto dependencies_result = ResolveDependencies(slot, artifact);
    loading_resources_.erase(slot.id);
    if (!dependencies_result)
    {
        return dependencies_result;
    }

    slot.state = ResourceState::Ready;
    return foundation::Result<void>::Success();
}

// Function note: Resolves dependencies.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> ResourceManager::ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact)
{
    // Function note: Releases dependency handles.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ReleaseDependencyHandles(slot);

    if (artifact.dependencies.empty())
    {
        return foundation::Result<void>::Success();
    }

    slot.state = ResourceState::WaitingForDependencies;
    std::vector<ResourceHandle> acquired_handles;
    for (const ResourceDependency& dependency : artifact.dependencies)
    {
        if (!dependency.resource_id.IsValid())
        {
            if (dependency.required)
            {
                slot.state = ResourceState::Failed;
                // Function note: Releases dependency handles.
                // Inputs/outputs: see the signature; the method consumes caller-provided values and
                // returns either a value, status flag or Result according to the surrounding API.
                // Relations: this member is part of the local runtime workflow and pairs with
                // neighboring query/update helpers defined in the same class or file.
                ReleaseDependencyHandles(acquired_handles);
                return foundation::Result<void>::Failure(foundation::Error::Create(
                    "resource.dependency_invalid_id", "required resource dependency must have a valid resource id"));
            }

            continue;
        }

        if (!dependency.type.IsValid())
        {
            if (dependency.required)
            {
                slot.state = ResourceState::Failed;
                // Function note: Releases dependency handles.
                // Inputs/outputs: see the signature; the method consumes caller-provided values and
                // returns either a value, status flag or Result according to the surrounding API.
                // Relations: this member is part of the local runtime workflow and pairs with
                // neighboring query/update helpers defined in the same class or file.
                ReleaseDependencyHandles(acquired_handles);
                return foundation::Result<void>::Failure(foundation::Error::Create(
                    "resource.dependency_invalid_type", "required resource dependency must have a valid resource type"));
            }

            continue;
        }

        // Function note: Handles request.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        const auto dependency_result = Request(ResourceRequest{dependency.resource_id, dependency.type, {}});
        if (!dependency_result)
        {
            if (dependency.required)
            {
                slot.state = ResourceState::Failed;
                // Function note: Releases dependency handles.
                // Inputs/outputs: see the signature; the method consumes caller-provided values and
                // returns either a value, status flag or Result according to the surrounding API.
                // Relations: this member is part of the local runtime workflow and pairs with
                // neighboring query/update helpers defined in the same class or file.
                ReleaseDependencyHandles(acquired_handles);
                return foundation::Result<void>::Failure(dependency_result.GetError());
            }

            continue;
        }

        acquired_handles.push_back(dependency_result.Value());
    }

    // Function note: Handles move.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    slot.dependency_handles = std::move(acquired_handles);
    return foundation::Result<void>::Success();
}
} 
