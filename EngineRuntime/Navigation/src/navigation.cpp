#include "navigation_runtime_impl.h"

namespace epidemic::runtime::navigation
{
std::unique_ptr<NavigationRuntime> CreateNavigationRuntime(NavigationOptions options)
{
    return std::make_unique<NavigationRuntime>(options);
}

NavigationServices CreateMockNavigationServices(NavigationOptions options)
{
    auto runtime = std::make_shared<NavigationRuntime>(options);

    NavigationServices services{};
    services.runtime = runtime;
    services.tiles = runtime;
    return services;
}
} // namespace epidemic::runtime::navigation
