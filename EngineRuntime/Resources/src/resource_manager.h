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

struct OwnedResourceDependency
{
    ResourceLease lease{};
    ResourceId id{};
    bool required = true;
};

struct ResourceSlot
{
    ResourceId id{};
    ResourceType type{};
    ResourceGeneration generation = 1;
    ResourceState state = ResourceState::Unknown;
    std::uint32_t reference_count = 0;
    std::unordered_set<ResourceAcquisitionId> active_acquisitions;
    ResourcePayloadPtr payload{};
    std::size_t memory_bytes = 0;
    std::vector<OwnedResourceDependency> dependency_handles;
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
    void PopBack() noexcept;
    void FailNextEnqueueForTesting() noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] std::optional<ResourceLoadJob> Dequeue();
    [[nodiscard]] std::size_t Size() const noexcept;

  private:
    std::deque<ResourceLoadJob> jobs_;
    bool fail_next_enqueue_for_testing_ = false;
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

    [[nodiscard]] foundation::Result<ResourceLease> RequestLease(ResourceRequest request) override;
    [[nodiscard]] foundation::Result<ResourceProcessingStats> ProcessPendingLoads(RuntimeBudget budget = {}) override;
    [[nodiscard]] foundation::Result<void> Release(ResourceLease lease) override;
    [[nodiscard]] foundation::Result<void> Evict(ResourceId id) override;
    [[nodiscard]] std::size_t EvictUnreferenced() override;

    [[nodiscard]] foundation::Result<void> ValidateHandle(ResourceHandle handle) const override;
    [[nodiscard]] ResourceState GetState(ResourceHandle handle) const override;
    [[nodiscard]] bool IsReady(ResourceHandle handle) const override;
    [[nodiscard]] std::optional<ResourceId> GetResourceId(ResourceHandle handle) const override;
    [[nodiscard]] ResourcePayloadPtr GetPayload(ResourceHandle handle) const override;

    void SetMemoryBudgetBytes(std::size_t bytes) override;
    [[nodiscard]] ResourceMemoryStats GetMemoryStatistics() const override;

    [[nodiscard]] const ResourceSlot* InspectSlot(ResourceId id) const;

    // Internal deterministic seams used only by the module contract tests.
    void FailNextQueueEnqueueForTesting() noexcept { load_queue_.FailNextEnqueueForTesting(); }
    void SetNextAcquisitionIdForTesting(ResourceAcquisitionId value) noexcept { next_acquisition_id_ = value; }
    [[nodiscard]] ResourceAcquisitionId NextAcquisitionIdForTesting() const noexcept { return next_acquisition_id_; }
    void SetSlotReferenceCountForTesting(ResourceId id, std::uint32_t value) { if (auto* slot = cache_.Find(id)) slot->reference_count = value; }
    void SetSlotGenerationForTesting(ResourceId id, ResourceGeneration value) { if (auto* slot = cache_.Find(id)) slot->generation = value; }
    void SetResidentBytesForTesting(std::size_t value) noexcept { resident_bytes_ = value; }
    void FailNextDependencyPublishForTesting() noexcept { fail_next_dependency_publish_for_testing_ = true; }
    void FailNextAcquisitionPublishForTesting() noexcept { fail_next_acquisition_publish_for_testing_ = true; }
    void SetDependenciesForTesting(ResourceId id, std::vector<ResourceDependency> dependencies) { dependency_graph_.SetDependencies(id, std::move(dependencies)); }
    [[nodiscard]] bool HasDependencyPathForTesting(ResourceId from, ResourceId to) const { return HasDependencyPath(from, to); }

  private:
    [[nodiscard]] foundation::Result<ResourceHandle> Acquire(ResourceRequest request, ResourceAcquisitionId acquisition);
    [[nodiscard]] foundation::Result<ResourceAcquisitionId> PeekAcquisitionId() const;
    void CommitAcquisitionId() noexcept;
    [[nodiscard]] static bool IsHandleCurrent(const ResourceSlot& slot, ResourceHandle handle);
    [[nodiscard]] static foundation::Result<ResourceGeneration> NextGeneration(ResourceGeneration generation);
    [[nodiscard]] static foundation::Result<void> ValidateRequest(const ResourceRequest& request);

    [[nodiscard]] foundation::Result<void> ReleaseDependencyHandles(ResourceSlot& slot);
    [[nodiscard]] foundation::Result<void> ReleaseDependencyHandles(std::vector<OwnedResourceDependency>& handles);
    void RecordInvariantFailure() noexcept;
    [[nodiscard]] foundation::Result<void> RollbackLoadAttempt(ResourceSlot& slot);
    [[nodiscard]] foundation::Result<void> LoadSlot(ResourceSlot& slot, const ResourceRequest& request, ResourceProcessingStats& stats);
    [[nodiscard]] foundation::Result<void> FinishLoadedArtifact(ResourceSlot& slot, ResourceLoadArtifact artifact, ResourceProcessingStats& stats);
    [[nodiscard]] foundation::Result<bool> ResolveDependencies(ResourceSlot& slot, const ResourceLoadArtifact& artifact);
    [[nodiscard]] bool HasDependencyPath(ResourceId from, ResourceId to) const;
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
    std::size_t invariant_failure_count_ = 0;
    ResourceAcquisitionId next_acquisition_id_ = 1;
    bool fail_next_dependency_publish_for_testing_ = false;
    bool fail_next_acquisition_publish_for_testing_ = false;
};
} // namespace epidemic::runtime
