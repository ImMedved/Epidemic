#include "resource_dependency_graph.h"

#include <new>
#include <utility>

namespace epidemic::runtime
{
void ResourceDependencyGraph::SetDependencies(ResourceId root, std::vector<ResourceDependency> dependencies)
{
    ReplaceDependencies(root, std::move(dependencies));
}

void ResourceDependencyGraph::ReplaceDependencies(ResourceId root, std::vector<ResourceDependency> dependencies)
{
    ResourceDependencySet candidate{root, std::move(dependencies)};
    if (fail_next_replace_for_testing_)
    {
        fail_next_replace_for_testing_ = false;
        throw std::bad_alloc{};
    }

    const auto existing = dependencies_.find(root);
    if (candidate.dependencies.empty())
    {
        if (existing != dependencies_.end())
        {
            dependencies_.erase(existing);
        }
        return;
    }

    if (existing == dependencies_.end())
    {
        dependencies_.emplace(root, std::move(candidate));
        return;
    }

    // The complete replacement has already been staged. Swapping vectors is
    // allocation-free with std::allocator, so the live dependency set changes
    // only at this final commit point.
    existing->second.root = root;
    existing->second.dependencies.swap(candidate.dependencies);
}

void ResourceDependencyGraph::RemoveDependencies(ResourceId root)
{
    dependencies_.erase(root);
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
} 
