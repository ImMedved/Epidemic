#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"

#include <cstddef>
#include <memory>

namespace epidemic::runtime
{
struct ResourceOptions
{
    std::size_t memory_budget_bytes = 0;
};

struct ResourceServices
{
    std::shared_ptr<IResourceLoaderRegistry> loaders;
    std::shared_ptr<IResourceManager> manager;
};

[[nodiscard]] foundation::Result<ResourceServices> CreateResourceServices(const ResourceOptions& options = {});
} // namespace epidemic::runtime

