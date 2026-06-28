#include "core/application/application.h"

#include "core/config/memory_configuration.h"
#include "core/diagnostics/console_logger.h"
#include "core/events/event_bus.h"
#include "core/tasks/task_scheduler.h"

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

    try
    {
        RegisterCoreServices();
        auto logger = Logger();
        logger->Info("Application", "Bootstrap started");
        modules_.BootstrapAll(services_, *logger);
        logger->Info("Application", "Bootstrap finished");
        state_ = State::Bootstrapped;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
}

int Application::Initialize()
{
    if (state_ != State::Bootstrapped)
    {
        throw std::runtime_error("Application initialize called in invalid state");
    }

    try
    {
        auto logger = Logger();
        logger->Info("Application", "Initialization started");
        modules_.InitializeAll(services_, *logger);
        logger->Info("Application", "Initialization finished");
        state_ = State::Initialized;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
}

int Application::Run()
{
    if (state_ != State::Initialized)
    {
        throw std::runtime_error("Application run called in invalid state");
    }

    state_ = State::Running;

    try
    {
        auto logger = Logger();
        logger->Info("Application", "Run started");

        const auto event_bus = services_.Get<events::IEventBus>();
        const auto scheduler = services_.Get<tasks::ITaskScheduler>();

        const auto drained = event_bus->DrainQueued();
        scheduler->WaitIdle();

        logger->Info("Application", "Queued events drained: " + std::to_string(drained));
        logger->Info("Application", "Run finished");
        state_ = State::Initialized;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
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
    try
    {
        auto logger = Logger();
        logger->Info("Application", "Shutdown started");

        if (state_ == State::Bootstrapped || state_ == State::Initialized || state_ == State::Running ||
            state_ == State::Failed)
        {
            modules_.ShutdownAll(services_, *logger);
        }

        const auto scheduler = services_.Get<tasks::ITaskScheduler>();
        scheduler->WaitIdle();

        logger->Info("Application", "Shutdown finished");
        state_ = State::ShutDown;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
}

void Application::RegisterCoreServices()
{
    if (services_registered_)
    {
        return;
    }

    if (!services_.Contains<diagnostics::ILogger>())
    {
        services_.Emplace<diagnostics::ILogger, diagnostics::ConsoleLogger>();
    }
    if (!services_.Contains<config::IConfiguration>())
    {
        services_.Emplace<config::IConfiguration, config::MemoryConfiguration>();
    }
    if (!services_.Contains<events::IEventBus>())
    {
        services_.Emplace<events::IEventBus, events::EventBus>();
    }
    if (!services_.Contains<tasks::ITaskScheduler>())
    {
        services_.Emplace<tasks::ITaskScheduler, tasks::SimpleTaskScheduler>(options_.worker_count);
    }

    const auto configuration = services_.Get<config::IConfiguration>();
    configuration->SetString("application.name", options_.application_name);

    services_registered_ = true;
}

std::shared_ptr<diagnostics::ILogger> Application::Logger() const
{
    return services_.Get<diagnostics::ILogger>();
}
} // namespace epidemic::core
