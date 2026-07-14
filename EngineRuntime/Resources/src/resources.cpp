#include "Epidemic/Runtime/Resources/resource_services.h"

#include "resource_loader_registry.h"
#include "resource_manager.h"

namespace epidemic::runtime
{
foundation::Result<ResourceServices> CreateResourceServices(const ResourceOptions& options)
{
    auto loaders = std::make_shared<ResourceLoaderRegistry>();
    auto manager = std::make_shared<ResourceManager>(loaders.get());
    manager->SetMemoryBudgetBytes(options.memory_budget_bytes);

    ResourceServices services{};
    services.loaders = loaders;
    services.manager = manager;
    return foundation::Result<ResourceServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
