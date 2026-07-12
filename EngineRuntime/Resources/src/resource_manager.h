#pragma once

#include "resource_dependency_graph.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_type.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime
{
using ResourceGeneration = std::uint32_t;

struct ResourceSlot
{
    ResourceId id{};
    ResourceType type{};
    ResourceGeneration generation = 1;
    ResourceState state = ResourceState::Unknown;
    std::uint32_t reference_count = 0;
    std::vector<ResourceHandle> dependency_handles;
};

struct ResourceLoadJob
{
    ResourceRequest request{};
    ResourceGeneration generation = 0;
};

class ResourceLoadQueue
{
  public:
    // Function note: Handles enqueue.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void Enqueue(ResourceLoadJob job);
    // Function note: Checks empty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsEmpty() const noexcept;
    // Function note: Handles dequeue.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<ResourceLoadJob> Dequeue();

  private:
    std::deque<ResourceLoadJob> jobs_;
};

class ResourceCache
{
  public:
    // Function note: Finds the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ResourceSlot* Find(ResourceId id);
    // Function note: Finds the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const ResourceSlot* Find(ResourceId id) const;
    // Function note: Finds or create.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ResourceSlot& FindOrCreate(ResourceId id, ResourceType type);
    // Function note: Handles entries.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::unordered_map<ResourceId, ResourceSlot>& Entries();
    // Function note: Handles entries.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const std::unordered_map<ResourceId, ResourceSlot>& Entries() const;

  private:
    std::unordered_map<ResourceId, ResourceSlot> slots_;
};

class ResourceManager final : public IResourceManager
{
  public:
    // Function note: Handles resource manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ResourceManager() = default;
    // Function note: Handles resource manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    explicit ResourceManager(IResourceLoaderRegistry* loader_registry);

    // Function note: Handles request.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<ResourceHandle> Request(ResourceRequest request) override;
    // Function note: Releases the associated runtime state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void Release(ResourceHandle handle) override;
    // Function note: Handles evict.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> Evict(ResourceId id) override;
    // Function note: Handles evict unreferenced.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::size_t EvictUnreferenced() override;

    // Function note: Gets state.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] ResourceState GetState(ResourceHandle handle) const override;
    // Function note: Checks ready.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsReady(ResourceHandle handle) const override;
    // Function note: Gets resource id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<ResourceId> GetResourceId(ResourceHandle handle) const override;

    // Function note: Handles inspect slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const ResourceSlot* InspectSlot(ResourceId id) const;

  private:
    // Function note: Checks handle current.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] static bool IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle);
    // Function note: Handles next generation.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] static ResourceGeneration NextGeneration(ResourceGeneration generation);
    // Function note: Prepares slot for load.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static void PrepareSlotForLoad(ResourceSlot& slot, ResourceType type);
    // Function note: Releases dependency handles.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void ReleaseDependencyHandles(ResourceSlot& slot);
    // Function note: Releases dependency handles.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void ReleaseDependencyHandles(std::vector<ResourceHandle>& handles);
    // Function note: Loads slot.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> LoadSlot(ResourceSlot& slot, ResourceRequest request);
    // Function note: Resolves dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact);

    IResourceLoaderRegistry* loader_registry_ = nullptr;
    ResourceCache cache_;
    ResourceLoadQueue load_queue_;
    ResourceDependencyGraph dependency_graph_;
    std::unordered_set<ResourceId> loading_resources_;
};
} 
