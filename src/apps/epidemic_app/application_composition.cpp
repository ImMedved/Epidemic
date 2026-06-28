#include "apps/epidemic_app/application_composition.h"

#include "apps/epidemic_app/demo_modules.h"
#include "layers/platform/interfaces/iplatform_runtime.h"
#include "layers/platform/windows/windows_platform_runtime.h"
#include "layers/runtime/interfaces/irenderer.h"
#include "layers/runtime/interfaces/iresource_manager.h"
#include "layers/runtime/interfaces/iscript_host.h"
#include "layers/runtime/interfaces/ivirtual_file_system.h"
#include "layers/runtime/placeholders/null_services.h"

namespace epidemic::apps::epidemic_app
{
void RegisterApplicationServices(core::ServiceContainer &services)
{
    if (!services.Contains<layers::platform::IPlatformRuntime>())
    {
        services.Emplace<layers::platform::IPlatformRuntime, layers::platform::WindowsPlatformRuntime>();
    }
    if (!services.Contains<layers::runtime::IVirtualFileSystem>())
    {
        services.Emplace<layers::runtime::IVirtualFileSystem, layers::runtime::NullVirtualFileSystem>();
    }
    if (!services.Contains<layers::runtime::IResourceManager>())
    {
        services.Emplace<layers::runtime::IResourceManager, layers::runtime::NullResourceManager>();
    }
    if (!services.Contains<layers::runtime::IRenderer>())
    {
        services.Emplace<layers::runtime::IRenderer, layers::runtime::NullRenderer>();
    }
    if (!services.Contains<layers::runtime::IScriptHost>())
    {
        services.Emplace<layers::runtime::IScriptHost, layers::runtime::NullScriptHost>();
    }
}

void RegisterApplicationModules(core::ModuleRegistry &modules)
{
    auto demo_modules = CreateDemoModules();
    for (auto &module : demo_modules)
    {
        modules.Register(std::move(module));
    }
}
} // namespace epidemic::apps::epidemic_app
