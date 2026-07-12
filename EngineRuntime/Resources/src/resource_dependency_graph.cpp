#include "resource_dependency_graph.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
namespace epidemic::runtime
{
// Function note: Sets dependencies.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void ResourceDependencyGraph::SetDependencies(ResourceId root, std::vector<ResourceDependency> dependencies)
{
    // Function note: Handles move.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    dependencies_[root] = ResourceDependencySet{root, std::move(dependencies)};
}

// Function note: Finds dependencies.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<ResourceDependencySet> ResourceDependencyGraph::FindDependencies(ResourceId root) const
{
    const auto iterator = dependencies_.find(root);
    if (iterator == dependencies_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Checks dependencies.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool ResourceDependencyGraph::HasDependencies(ResourceId root) const
{
    // Function note: Finds dependencies.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto entry = FindDependencies(root);
    return entry.has_value() && !entry->dependencies.empty();
}
} // namespace epidemic::runtime
