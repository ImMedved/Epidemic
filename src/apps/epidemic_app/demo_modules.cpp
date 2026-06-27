#include "apps/epidemic_app/demo_modules.h"

#include "core/diagnostics/logger.h"
#include "core/events/event_bus.h"
#include "core/tasks/task_scheduler.h"
#include "layers/platform/interfaces/iplatform_runtime.h"

namespace epidemic::apps::epidemic_app
{
namespace
{
using epidemic::core::diagnostics::ILogger;
using epidemic::core::events::IEventBus;
using epidemic::core::tasks::ITaskScheduler;

void LogModuleStage(epidemic::core::ServiceContainer &services, const epidemic::core::ModuleManifest &manifest,
                    std::string_view stage)
{
    const auto logger = services.Get<ILogger>();
    std::string message(manifest.id);
    message += " -> ";
    message += stage;
    logger->Info("Module", message);
}
} // namespace

CoreBootstrapModule::CoreBootstrapModule()
    : manifest_{"core.bootstrap", "Core Bootstrap Module", {}}
{
}

const epidemic::core::ModuleManifest &CoreBootstrapModule::Manifest() const
{
    return manifest_;
}

void CoreBootstrapModule::OnBootstrap(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "bootstrap");
}

void CoreBootstrapModule::OnInitialize(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "initialize");
}

void CoreBootstrapModule::OnShutdown(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "shutdown");
}

EventPipelineModule::EventPipelineModule()
    : manifest_{"core.event_pipeline", "Event Pipeline Module", {"core.bootstrap"}}
{
}

const epidemic::core::ModuleManifest &EventPipelineModule::Manifest() const
{
    return manifest_;
}

void EventPipelineModule::OnBootstrap(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "bootstrap");
}

void EventPipelineModule::OnInitialize(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "initialize");

    const auto logger = services.Get<ILogger>();
    const auto event_bus = services.Get<IEventBus>();
    const auto platform_runtime = services.Get<epidemic::layers::platform::IPlatformRuntime>();

    logger->Info("Platform", "Using platform runtime: " + std::string(platform_runtime->Name()));
    logger->Info("Platform", "Executable: " + platform_runtime->GetProcessInfo().executable_path.GenericString());
    event_bus->SubscribeSync<DemoSyncEvent>(
        [logger](const DemoSyncEvent &event) { logger->Info("Event", "Sync event received: " + event.message); });

    event_bus->SubscribeQueued<DemoQueuedEvent>(
        [logger](const DemoQueuedEvent &event) { logger->Info("Event", "Queued event received: " + event.message); });
}

void EventPipelineModule::OnShutdown(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "shutdown");
}

RuntimeBootstrapModule::RuntimeBootstrapModule()
    : manifest_{"runtime.bootstrap", "Runtime Bootstrap Module", {"core.event_pipeline"}}
{
}

const epidemic::core::ModuleManifest &RuntimeBootstrapModule::Manifest() const
{
    return manifest_;
}

void RuntimeBootstrapModule::OnBootstrap(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "bootstrap");
}

void RuntimeBootstrapModule::OnInitialize(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "initialize");

    const auto logger = services.Get<ILogger>();
    const auto event_bus = services.Get<IEventBus>();
    const auto scheduler = services.Get<ITaskScheduler>();

    event_bus->PublishSync(DemoSyncEvent{"module initialization"});
    event_bus->Enqueue(DemoQueuedEvent{"deferred startup work"});

    scheduler->Schedule([logger] { logger->Info("Task", "Simple scheduled task executed"); });
}

void RuntimeBootstrapModule::OnShutdown(epidemic::core::ServiceContainer &services)
{
    LogModuleStage(services, manifest_, "shutdown");
}

std::vector<std::unique_ptr<epidemic::core::IModule>> CreateDemoModules()
{
    std::vector<std::unique_ptr<epidemic::core::IModule>> modules;
    modules.push_back(std::make_unique<RuntimeBootstrapModule>());
    modules.push_back(std::make_unique<EventPipelineModule>());
    modules.push_back(std::make_unique<CoreBootstrapModule>());
    return modules;
}
} // namespace epidemic::apps::epidemic_app
