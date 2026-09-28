#include "resource_loader_registry.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
foundation::Result<void> ResourceLoaderRegistry::RegisterLoader(IResourceLoader& loader)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource_loader.frozen", "resource loader registry is frozen"));
    }
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

    try
    {
        loaders_.emplace(type.value, &loader);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("resource_loader.allocation_failed", "resource loader registration could not be published"));
    }
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

foundation::Result<void> ResourceLoaderRegistry::Freeze()
{
    frozen_ = true;
    return foundation::Result<void>::Success();
}

bool ResourceLoaderRegistry::IsFrozen() const noexcept
{
    return frozen_;
}
} // namespace epidemic::runtime
