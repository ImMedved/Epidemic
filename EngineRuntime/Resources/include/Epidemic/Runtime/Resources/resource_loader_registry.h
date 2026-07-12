#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
class IResourceLoaderRegistry
{
  public:
    // Function note: Handles ~iresource loader registry.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IResourceLoaderRegistry() = default;

    // The loader registry does not own loaders. Registered loaders must outlive the
    // registry and any resource manager that resolves loaders through it.
    [[nodiscard]] virtual foundation::Result<void> RegisterLoader(IResourceLoader& loader) = 0;
    // Function note: Finds loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual IResourceLoader* FindLoader(ResourceType type) = 0;
    // Function note: Finds loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual const IResourceLoader* FindLoader(ResourceType type) const = 0;
    // Function note: Checks loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual bool HasLoader(ResourceType type) const = 0;
};
} 
