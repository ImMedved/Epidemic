#pragma once

#include "Epidemic/Runtime/Resources/resource_loader_registry.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>

namespace epidemic::runtime
{
class ResourceLoaderRegistry final : public IResourceLoaderRegistry
{
  public:
    // Function note: Registers loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> RegisterLoader(IResourceLoader& loader) override;
    // Function note: Finds loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] IResourceLoader* FindLoader(ResourceType type) override;
    // Function note: Finds loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const IResourceLoader* FindLoader(ResourceType type) const override;
    // Function note: Checks loader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool HasLoader(ResourceType type) const override;

  private:
    std::unordered_map<foundation::StringId, IResourceLoader*> loaders_;
};
} 
