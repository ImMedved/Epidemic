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
    ResourcePayloadPtr payload{};
    std::size_t memory_bytes = 0;
    std::vector<ResourceHandle> dependency_handles;
    std::optional<ResourceLoadArtifact> pending_artifact{};
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
    [[nodiscard]] std::size_t Size() const noexcept;

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
    [[nodiscard]] foundation::Result<ResourceProcessingStats> ProcessPendingLoads(RuntimeBudget budget = {}) override;
    [[nodiscard]] foundation::Result<void> Release(ResourceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Evict(ResourceId id) override;
    [[nodiscard]] std::size_t EvictUnreferenced() override;

    [[nodiscard]] foundation::Result<void> ValidateHandle(ResourceHandle handle) const override;
    [[nodiscard]] ResourceState GetState(ResourceHandle handle) const override;
    [[nodiscard]] bool IsReady(ResourceHandle handle) const override;
    [[nodiscard]] std::optional<ResourceId> GetResourceId(ResourceHandle handle) const override;
    [[nodiscard]] ResourcePayloadPtr GetPayload(ResourceHandle handle) const override;

    void SetMemoryBudgetBytes(std::size_t bytes) override;
    [[nodiscard]] ResourceMemoryStats GetMemoryStats() const override;
    [[nodiscard]] ResourceMemoryStatistics GetMemoryStatistics() const override;

    [[nodiscard]] const ResourceSlot* InspectSlot(ResourceId id) const;

  private:
    [[nodiscard]] static bool IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle);
    [[nodiscard]] static ResourceGeneration NextGeneration(ResourceGeneration generation);
    [[nodiscard]] static foundation::Result<void> ValidateRequest(const ResourceRequest& request);
    static void QueueSlot(ResourceLoadQueue& queue, ResourceSlot& slot, ResourceRequest request);

    void ReleaseDependencyHandles(ResourceSlot& slot);
    void ReleaseDependencyHandles(std::vector<ResourceHandle>& handles);
    [[nodiscard]] foundation::Result<void> LoadSlot(ResourceSlot& slot, const ResourceRequest& request, ResourceProcessingStats& stats);
    [[nodiscard]] foundation::Result<void> FinishLoadedArtifact(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats);
    [[nodiscard]] foundation::Result<bool> ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact);
    [[nodiscard]] bool HasDependencyPath(ResourceId from, ResourceId to, std::unordered_set<ResourceId>& visited) const;
    [[nodiscard]] foundation::Result<void> CommitReadyPayload(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats);
    [[nodiscard]] bool CanFit(std::size_t bytes) const noexcept;
    void RemovePayload(ResourceSlot& slot);

    IResourceLoaderRegistry* loader_registry_ = nullptr;
    ResourceCache cache_;
    ResourceLoadQueue load_queue_;
    ResourceDependencyGraph dependency_graph_;
    std::unordered_set<ResourceId> loading_resources_;
    std::size_t resident_bytes_ = 0;
    std::size_t memory_budget_bytes_ = 0;
};
} // namespace epidemic::runtime
