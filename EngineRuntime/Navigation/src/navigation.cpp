#include "navigation_runtime_impl.h"

namespace epidemic::runtime::navigation
{
std::unique_ptr<NavigationRuntime> CreateNavigationRuntime(NavigationOptions options)
{
    return std::make_unique<NavigationRuntime>(options);
}
} // namespace epidemic::runtime::navigation
