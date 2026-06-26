#pragma once

#include "core/modules/imodule.h"

#include <memory>
#include <string>
#include <vector>

namespace epidemic::apps::epidemic_app
{
struct DemoSyncEvent
{
    std::string message;
};

struct DemoQueuedEvent
{
    std::string message;
};

class CoreBootstrapModule final : public core::IModule
{
  public:
    CoreBootstrapModule();

    [[nodiscard]] const core::ModuleManifest &Manifest() const override;
    void OnBootstrap(core::ServiceContainer &services) override;
    void OnInitialize(core::ServiceContainer &services) override;
    void OnShutdown(core::ServiceContainer &services) override;

  private:
    core::ModuleManifest manifest_;
};

class EventPipelineModule final : public core::IModule
{
  public:
    EventPipelineModule();

    [[nodiscard]] const core::ModuleManifest &Manifest() const override;
    void OnBootstrap(core::ServiceContainer &services) override;
    void OnInitialize(core::ServiceContainer &services) override;
    void OnShutdown(core::ServiceContainer &services) override;

  private:
    core::ModuleManifest manifest_;
};

class RuntimeBootstrapModule final : public core::IModule
{
  public:
    RuntimeBootstrapModule();

    [[nodiscard]] const core::ModuleManifest &Manifest() const override;
    void OnBootstrap(core::ServiceContainer &services) override;
    void OnInitialize(core::ServiceContainer &services) override;
    void OnShutdown(core::ServiceContainer &services) override;

  private:
    core::ModuleManifest manifest_;
};

[[nodiscard]] std::vector<std::unique_ptr<core::IModule>> CreateDemoModules();
} // namespace epidemic::apps::epidemic_app
