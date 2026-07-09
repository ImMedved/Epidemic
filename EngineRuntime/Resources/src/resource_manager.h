#pragma once

#include "resource_dependency_graph.h"

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
    void Enqueue(ResourceLoadJob job);
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] std::optional<ResourceLoadJob> Dequeue();

  private:
    std::deque<ResourceLoadJob> jobs_;
};

class ResourceCache
{
  public:
    [[nodiscard]] ResourceSlot* Find(ResourceId id);
    [[nodiscard]] const ResourceSlot* Find(ResourceId id) const;
    [[nodiscard]] ResourceSlot& FindOrCreate(ResourceId id, ResourceType type);
    [[nodiscard]] std::unordered_map<ResourceId, ResourceSlot>& Entries();
    [[nodiscard]] const std::unordered_map<ResourceId, ResourceSlot>& Entries() const;

  private:
    std::unordered_map<ResourceId, ResourceSlot> slots_;
};

class ResourceManager final : public IResourceManager
{
  public:
    ResourceManager() = default;
    explicit ResourceManager(IResourceLoaderRegistry* loader_registry);

    [[nodiscard]] foundation::Result<ResourceHandle> Request(ResourceRequest request) override;
    void Release(ResourceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Evict(ResourceId id) override;
    [[nodiscard]] std::size_t EvictUnreferenced() override;

    [[nodiscard]] ResourceState GetState(ResourceHandle handle) const override;
    [[nodiscard]] bool IsReady(ResourceHandle handle) const override;
    [[nodiscard]] std::optional<ResourceId> GetResourceId(ResourceHandle handle) const override;

    [[nodiscard]] const ResourceSlot* InspectSlot(ResourceId id) const;

  private:
    [[nodiscard]] static bool IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle);
    [[nodiscard]] static ResourceGeneration NextGeneration(ResourceGeneration generation);
    static void PrepareSlotForLoad(ResourceSlot& slot, ResourceType type);
    void ReleaseDependencyHandles(ResourceSlot& slot);
    void ReleaseDependencyHandles(std::vector<ResourceHandle>& handles);
    [[nodiscard]] foundation::Result<void> LoadSlot(ResourceSlot& slot, ResourceRequest request);
    [[nodiscard]] foundation::Result<void> ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact);

    IResourceLoaderRegistry* loader_registry_ = nullptr;
    ResourceCache cache_;
    ResourceLoadQueue load_queue_;
    ResourceDependencyGraph dependency_graph_;
    std::unordered_set<ResourceId> loading_resources_;
};
} // namespace epidemic::runtime
