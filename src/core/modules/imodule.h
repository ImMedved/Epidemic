#pragma once

#include "core/modules/module_manifest.h"
#include "core/services/service_container.h"

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
