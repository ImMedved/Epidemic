#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace epidemic::runtime
{
struct ResourceProcessingStats
{
    std::size_t processed_jobs = 0;
    std::size_t loaded_resources = 0;
    std::size_t failed_resources = 0;
    std::size_t bytes_loaded = 0;
};

struct ResourceMemoryStats
{
    std::size_t resident_bytes = 0;
    std::size_t budget_bytes = 0;
    std::size_t slot_count = 0;
    std::size_t ready_count = 0;
};

struct ResourceMemoryStatistics
{
    std::size_t ready_bytes = 0;
    std::size_t cached_unreferenced_bytes = 0;
    std::size_t resource_count = 0;
};

// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IResourceManager
{
  public:
    virtual ~IResourceManager() = default;

    [[nodiscard]] virtual foundation::Result<ResourceHandle> Request(ResourceRequest request) = 0;
    [[nodiscard]] virtual foundation::Result<ResourceProcessingStats> ProcessPendingLoads(RuntimeBudget budget = {}) = 0;
    [[nodiscard]] virtual foundation::Result<void> Release(ResourceHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> Evict(ResourceId id) = 0;
    [[nodiscard]] virtual std::size_t EvictUnreferenced() = 0;

    [[nodiscard]] virtual foundation::Result<void> ValidateHandle(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual ResourceState GetState(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual bool IsReady(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<ResourceId> GetResourceId(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual ResourcePayloadPtr GetPayload(ResourceHandle handle) const = 0;

    virtual void SetMemoryBudgetBytes(std::size_t bytes) = 0;
    [[nodiscard]] virtual ResourceMemoryStats GetMemoryStats() const = 0;
    [[nodiscard]] virtual ResourceMemoryStatistics GetMemoryStatistics() const = 0;
};
} // namespace epidemic::runtime
