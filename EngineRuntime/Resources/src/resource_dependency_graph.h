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
    void ReplaceDependencies(ResourceId root, std::vector<ResourceDependency> dependencies);
    void RemoveDependencies(ResourceId root);
    [[nodiscard]] std::optional<ResourceDependencySet> FindDependencies(ResourceId root) const;
    [[nodiscard]] bool HasDependencies(ResourceId root) const;
    void FailNextReplaceForTesting() noexcept { fail_next_replace_for_testing_ = true; }

  private:
    std::unordered_map<ResourceId, ResourceDependencySet> dependencies_;
    bool fail_next_replace_for_testing_ = false;
};
} 
