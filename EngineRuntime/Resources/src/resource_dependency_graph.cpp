#include "resource_dependency_graph.h"

namespace epidemic::runtime
{
void ResourceDependencyGraph::SetDependencies(ResourceId root, std::vector<ResourceDependency> dependencies)
{
    dependencies_[root] = ResourceDependencySet{root, std::move(dependencies)};
}

std::optional<ResourceDependencySet> ResourceDependencyGraph::FindDependencies(ResourceId root) const
{
    const auto iterator = dependencies_.find(root);
    if (iterator == dependencies_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

bool ResourceDependencyGraph::HasDependencies(ResourceId root) const
{
    const auto entry = FindDependencies(root);
    return entry.has_value() && !entry->dependencies.empty();
}
} // namespace epidemic::runtime
