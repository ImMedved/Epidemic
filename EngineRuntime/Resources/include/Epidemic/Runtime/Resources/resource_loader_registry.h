#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IResourceLoaderRegistry
{
  public:
    virtual ~IResourceLoaderRegistry() = default;

    // The loader registry does not own loaders. Registered loaders must outlive the
    // registry and any resource manager that resolves loaders through it.
    [[nodiscard]] virtual foundation::Result<void> RegisterLoader(IResourceLoader& loader) = 0;
    [[nodiscard]] virtual IResourceLoader* FindLoader(ResourceType type) = 0;
    [[nodiscard]] virtual const IResourceLoader* FindLoader(ResourceType type) const = 0;
    [[nodiscard]] virtual bool HasLoader(ResourceType type) const = 0;
    [[nodiscard]] virtual foundation::Result<void> Freeze() = 0;
    [[nodiscard]] virtual bool IsFrozen() const noexcept = 0;
};
} // namespace epidemic::runtime
