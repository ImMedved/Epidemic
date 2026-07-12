#include "resource_loader_registry.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
// Function note: Registers loader.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> ResourceLoaderRegistry::RegisterLoader(IResourceLoader& loader)
{
    const ResourceType type = loader.GetResourceType();
    if (!type.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource_loader.invalid_type", "resource loader must declare a valid resource type"));
    }

    if (loaders_.contains(type.value))
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("resource_loader.duplicate_type", "resource loader type is already registered"));
    }

    loaders_.emplace(type.value, &loader);
    return foundation::Result<void>::Success();
}

// Function note: Finds loader.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
IResourceLoader* ResourceLoaderRegistry::FindLoader(ResourceType type)
{
    const auto iterator = loaders_.find(type.value);
    if (iterator == loaders_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Finds loader.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const IResourceLoader* ResourceLoaderRegistry::FindLoader(ResourceType type) const
{
    const auto iterator = loaders_.find(type.value);
    if (iterator == loaders_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

// Function note: Checks loader.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool ResourceLoaderRegistry::HasLoader(ResourceType type) const
{
    return FindLoader(type) != nullptr;
}
} 
