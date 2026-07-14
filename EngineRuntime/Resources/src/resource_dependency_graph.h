#pragma once

#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"


#include <optional>
#include <unordered_map>

namespace epidemic::runtime
{
class ResourceDependencyGraph
{
  public:
    void SetDependencies(ResourceId root, std::vector<ResourceDependency> dependencies);
    [[nodiscard]] std::optional<ResourceDependencySet> FindDependencies(ResourceId root) const;
    [[nodiscard]] bool HasDependencies(ResourceId root) const;

  private:
    std::unordered_map<ResourceId, ResourceDependencySet> dependencies_;
};
} 
