#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_state.h"

#include <cstddef>
#include <optional>

namespace epidemic::runtime
{
class IResourceManager
{
  public:
    virtual ~IResourceManager() = default;

    [[nodiscard]] virtual foundation::Result<ResourceHandle> Request(ResourceRequest request) = 0;

    // Releases the caller-owned handle reference. When the slot reaches zero references,
    // it becomes Unloaded and releases tracked dependency handles. Eviction is explicit.
    virtual void Release(ResourceHandle handle) = 0;

    [[nodiscard]] virtual foundation::Result<void> Evict(ResourceId id) = 0;
    [[nodiscard]] virtual std::size_t EvictUnreferenced() = 0;

    // Invalid, unknown, and stale handles report Unknown.
    [[nodiscard]] virtual ResourceState GetState(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual bool IsReady(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<ResourceId> GetResourceId(ResourceHandle handle) const = 0;
};
} // namespace epidemic::runtime
