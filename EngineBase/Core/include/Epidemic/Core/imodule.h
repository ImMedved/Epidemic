#pragma once

#include <Epidemic/Core/module_manifest.h>
#include <Epidemic/Core/service_container.h>

namespace epidemic::core
{
class IModule
{
  public:
    virtual ~IModule() = default;

    [[nodiscard]] virtual const ModuleManifest &Manifest() const = 0;
    virtual void OnBootstrap(ServiceContainer &services) = 0;
    virtual void OnInitialize(ServiceContainer &services) = 0;
    virtual void OnShutdown(ServiceContainer &services) = 0;
};
} // namespace epidemic::core