#pragma once

#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <optional>
#include <unordered_map>

namespace epidemic::runtime
{
class ResourceDependencyGraph
{
  public:
    // Function note: Sets dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void SetDependencies(ResourceId root, std::vector<ResourceDependency> dependencies);
    // Function note: Finds dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<ResourceDependencySet> FindDependencies(ResourceId root) const;
    // Function note: Checks dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool HasDependencies(ResourceId root) const;

  private:
    std::unordered_map<ResourceId, ResourceDependencySet> dependencies_;
};
} // namespace epidemic::runtime
