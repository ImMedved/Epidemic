#include "navigation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <utility>

namespace epidemic::runtime::navigation
{
foundation::Result<NavigationServices> CreateNavigationServices(NavigationOptions options, NavigationDependencies dependencies)
{
    if (dependencies.backend == nullptr && !options.enable_mock_queries)
    {
        return foundation::Result<NavigationServices>::Failure(
            foundation::Error::Create("navigation.backend_missing", "navigation backend is required when reference queries are disabled"));
    }

    auto runtime = std::make_shared<NavigationRuntime>(options, std::move(dependencies));

    NavigationServices services{};
    services.runtime = runtime;
    services.tiles = runtime;
    return foundation::Result<NavigationServices>::Success(std::move(services));
}

NavigationServices CreateMockNavigationServices(NavigationOptions options)
{
    options.enable_mock_queries = true;
    auto runtime = std::make_shared<NavigationRuntime>(options);

    NavigationServices services{};
    services.runtime = runtime;
    services.tiles = runtime;
    return services;
}
} // namespace epidemic::runtime::navigation
