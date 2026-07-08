#include "resource_loader_registry.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
foundation::Result<void> ResourceLoaderRegistry::RegisterLoader(IResourceLoader& loader)
{
    const ResourceType type = loader.GetResourceType();
    if (!type.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource_loader.invalid_type", "resource loader must declare a valid resource type"));
    }

    if (loaders_.contains(type.value))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource_loader.duplicate_type", "resource loader type is already registered"));
    }

    loaders_.emplace(type.value, &loader);
    return foundation::Result<void>::Success();
}

IResourceLoader* ResourceLoaderRegistry::FindLoader(ResourceType type)
{
    const auto iterator = loaders_.find(type.value);
    if (iterator == loaders_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

const IResourceLoader* ResourceLoaderRegistry::FindLoader(ResourceType type) const
{
    const auto iterator = loaders_.find(type.value);
    if (iterator == loaders_.end())
    {
        return nullptr;
    }

    return iterator->second;
}

bool ResourceLoaderRegistry::HasLoader(ResourceType type) const
{
    return FindLoader(type) != nullptr;
}
} // namespace epidemic::runtime
