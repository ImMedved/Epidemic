#include "core/application/application.h"

#include "core/config/memory_configuration.h"
#include "core/diagnostics/console_logger.h"
#include "core/events/event_bus.h"
#include "core/tasks/task_scheduler.h"
#include "layers/runtime/interfaces/irenderer.h"
#include "layers/runtime/interfaces/iresource_manager.h"
#include "layers/runtime/interfaces/iscript_host.h"
#include "layers/runtime/interfaces/ivirtual_file_system.h"
#include "layers/runtime/placeholders/null_services.h"
#include "layers/platform/interfaces/iplatform_runtime.h"
#include "layers/platform/placeholders/windows_platform_runtime_stub.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace epidemic::core
{
Application::Application(ApplicationOptions options) : options_(std::move(options))
{
}

Application::~Application()
{
    if (state_ != State::ShutDown)
    {
        try
        {
            Shutdown();
        }
        catch (...)
        {
        }
    }
}

ModuleRegistry &Application::Modules() noexcept
{
    return modules_;
}

ServiceContainer &Application::Services() noexcept
{
    return services_;
}

int Application::Bootstrap()
{
    if (state_ != State::Constructed)
    {
        throw std::runtime_error("Application bootstrap called in invalid state");
    }

    RegisterCoreServices();
    auto logger = Logger();
    logger->Info("Application", "Bootstrap started");
    modules_.BootstrapAll(services_, *logger);
    logger->Info("Application", "Bootstrap finished");
    state_ = State::Bootstrapped;
    return 0;
}

int Application::Initialize()
{
    if (state_ != State::Bootstrapped)
    {
        throw std::runtime_error("Application initialize called in invalid state");
    }

    auto logger = Logger();
    logger->Info("Application", "Initialization started");
    modules_.InitializeAll(services_, *logger);
    logger->Info("Application", "Initialization finished");
    state_ = State::Initialized;
    return 0;
}

int Application::Run()
{
    if (state_ != State::Initialized)
    {
        throw std::runtime_error("Application run called in invalid state");
    }

    state_ = State::Running;

    auto logger = Logger();
    logger->Info("Application", "Run started");

    const auto event_bus = services_.Get<events::IEventBus>();
    const auto platform_runtime = services_.Get<layers::platform::IPlatformRuntime>();
    const auto scheduler = services_.Get<tasks::ITaskScheduler>();

    platform_runtime->PumpEvents();
    const auto drained = event_bus->DrainQueued();
    scheduler->WaitIdle();

    logger->Info("Application", "Queued events drained: " + std::to_string(drained));
    logger->Info("Application", "Run finished");
    state_ = State::Initialized;
    return 0;
}

int Application::Shutdown()
{
    if (state_ == State::ShutDown)
    {
        return 0;
    }

    if (state_ == State::Constructed)
    {
        state_ = State::ShutDown;
        return 0;
    }

    RegisterCoreServices();
    auto logger = Logger();
    logger->Info("Application", "Shutdown started");

    if (state_ == State::Bootstrapped || state_ == State::Initialized || state_ == State::Running)
    {
        modules_.ShutdownAll(services_, *logger);
    }

    const auto scheduler = services_.Get<tasks::ITaskScheduler>();
    scheduler->WaitIdle();

    logger->Info("Application", "Shutdown finished");
    state_ = State::ShutDown;
    return 0;
}

void Application::RegisterCoreServices()
{
    if (services_registered_)
    {
        return;
    }

    services_.Emplace<diagnostics::ILogger, diagnostics::ConsoleLogger>();
    services_.Emplace<config::IConfiguration, config::MemoryConfiguration>();
    services_.Emplace<events::IEventBus, events::EventBus>();
    services_.Emplace<tasks::ITaskScheduler, tasks::SimpleTaskScheduler>(options_.worker_count);
    services_.Emplace<layers::platform::IPlatformRuntime, layers::platform::WindowsPlatformRuntimeStub>();
    services_.Emplace<layers::runtime::IVirtualFileSystem, layers::runtime::NullVirtualFileSystem>();
    services_.Emplace<layers::runtime::IResourceManager, layers::runtime::NullResourceManager>();
    services_.Emplace<layers::runtime::IRenderer, layers::runtime::NullRenderer>();
    services_.Emplace<layers::runtime::IScriptHost, layers::runtime::NullScriptHost>();

    const auto configuration = services_.Get<config::IConfiguration>();
    configuration->SetString("application.name", options_.application_name);

    services_registered_ = true;
}

std::shared_ptr<diagnostics::ILogger> Application::Logger() const
{
    return services_.Get<diagnostics::ILogger>();
}
} // namespace epidemic::core
