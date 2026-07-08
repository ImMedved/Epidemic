#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_state.h"

#include <optional>

namespace epidemic::runtime
{
class IResourceManager
{
  public:
    virtual ~IResourceManager() = default;

    [[nodiscard]] virtual foundation::Result<ResourceHandle> Request(ResourceRequest request) = 0;
    virtual void Release(ResourceHandle handle) = 0;

    // Unknown or stale handles are treated as Evicted in this placeholder implementation.
    [[nodiscard]] virtual ResourceState GetState(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual bool IsReady(ResourceHandle handle) const = 0;
    [[nodiscard]] virtual std::optional<ResourceId> GetResourceId(ResourceHandle handle) const = 0;
};
} // namespace epidemic::runtime
