#pragma once

#include "Epidemic/Runtime/Resources/resource_loader_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class ResourceLoaderRegistry final : public IResourceLoaderRegistry
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterLoader(IResourceLoader& loader) override;
    [[nodiscard]] IResourceLoader* FindLoader(ResourceType type) override;
    [[nodiscard]] const IResourceLoader* FindLoader(ResourceType type) const override;
    [[nodiscard]] bool HasLoader(ResourceType type) const override;

  private:
    std::unordered_map<foundation::StringId, IResourceLoader*> loaders_;
};
} // namespace epidemic::runtime
