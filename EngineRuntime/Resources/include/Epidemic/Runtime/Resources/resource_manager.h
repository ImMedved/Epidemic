#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_state.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <cstddef>
#include <optional>

namespace epidemic::runtime
{
class IResourceManager
{
  public:
    // Function note: Handles ~iresource manager.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IResourceManager() = default;

    // Function note: Handles request.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<ResourceHandle> Request(ResourceRequest request) = 0;

    // Releases the caller-owned handle reference. When the slot reaches zero references,
    // it becomes Unloaded and releases tracked dependency handles. Eviction is explicit.
    virtual void Release(ResourceHandle handle) = 0;

    // Function note: Handles evict.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> Evict(ResourceId id) = 0;
    // Function note: Handles evict unreferenced.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::size_t EvictUnreferenced() = 0;

    // Invalid, unknown, and stale handles report Unknown.
    [[nodiscard]] virtual ResourceState GetState(ResourceHandle handle) const = 0;
    // Function note: Checks ready.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual bool IsReady(ResourceHandle handle) const = 0;
    // Function note: Gets resource id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual std::optional<ResourceId> GetResourceId(ResourceHandle handle) const = 0;
};
} 
