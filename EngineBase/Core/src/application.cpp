#include <Epidemic/Core/application.h>

#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/profiling.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace epidemic::core
{
namespace
{
template <typename TService>
void EnsureRegistered(const ServiceContainer &services, std::string_view service_name)
{
    if (!services.Contains<TService>())
    {
        throw std::runtime_error("Required service is not registered: " + std::string(service_name));
    }
}
} // namespace

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
        ValidateCoreServices();
        const auto configuration = services_.Get<config::IConfiguration>();
        if (!configuration->GetRuntimeName().has_value())
        {
            configuration->SetRuntimeName(options_.application_name);
        }

        auto logger = Logger();
        logger->Info("Application", "Lifecycle", "Bootstrap started");
        if (const auto runtime_name = configuration->GetRuntimeName(); runtime_name.has_value())
        {
            logger->Info("Application", "Config", "Runtime name: " + *runtime_name);
        }
        if (const auto worker_count = configuration->GetWorkerCount(); worker_count.has_value())
        {
            diagnostics::GlobalCounters().Set(diagnostics::CounterId::WorkerCount, static_cast<std::int64_t>(*worker_count));
            logger->Info("Application", "Config", "Worker threads: " + std::to_string(*worker_count));
        }
        if (const auto memory_tracking = configuration->GetMemoryTrackingEnabled(); memory_tracking.has_value())
        {
            logger->Info("Application", "Config",
                         std::string("Memory tracking: ") + (*memory_tracking ? "enabled" : "disabled"));
        }

        modules_.BootstrapAll(services_, *logger);
        logger->Info("Application", "Lifecycle", "Bootstrap finished");
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
        logger->Info("Application", "Lifecycle", "Initialization started");
        modules_.InitializeAll(services_, *logger);
        logger->Info("Application", "Lifecycle", "Initialization finished");
        state_ = State::Initialized;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
}

int Application::Tick()
{
    if (state_ != State::Initialized)
    {
        throw std::runtime_error("Application tick called in invalid state");
    }

    EPIDEMIC_PROFILE_SCOPE("Application::Tick");
    const auto tick_start = std::chrono::steady_clock::now();
    state_ = State::Running;

    try
    {
        auto logger = Logger();
        logger->Debug("Application", "Lifecycle", "Tick started");

        modules_.TickAll(services_, *logger);

        const auto event_bus = services_.Get<events::IEventBus>();
        const auto scheduler = services_.Get<tasks::ITaskScheduler>();
        static_cast<void>(event_bus->DrainQueued());
        scheduler->WaitIdle();

        diagnostics::GlobalCounters().Increment(diagnostics::CounterId::Frames);
        diagnostics::GlobalCounters().Set(
            diagnostics::CounterId::FrameTimeMicros,
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tick_start).count());

        logger->Debug("Application", "Lifecycle", "Tick finished");
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

    auto logger = Logger();
    logger->Info("Application", "Lifecycle", "Run started");
    const auto result = Tick();
    logger->Info("Application", "Lifecycle", "Run finished");
    return result;
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

    try
    {
        ValidateCoreServices();
        auto logger = Logger();
        logger->Info("Application", "Lifecycle", "Shutdown started");

        modules_.ShutdownAll(services_, *logger);

        const auto scheduler = services_.Get<tasks::ITaskScheduler>();
        scheduler->Shutdown();
        scheduler->WaitIdle();

        logger->Info("Application", "Lifecycle", "Shutdown finished");
        state_ = State::ShutDown;
        return 0;
    }
    catch (...)
    {
        state_ = State::Failed;
        throw;
    }
}

void Application::ValidateCoreServices() const
{
    EnsureRegistered<diagnostics::ILogger>(services_, "diagnostics::ILogger");
    EnsureRegistered<config::IConfiguration>(services_, "config::IConfiguration");
    EnsureRegistered<events::IEventBus>(services_, "events::IEventBus");
    EnsureRegistered<tasks::ITaskScheduler>(services_, "tasks::ITaskScheduler");
}

std::shared_ptr<diagnostics::ILogger> Application::Logger() const
{
    return services_.Get<diagnostics::ILogger>();
}
} // namespace epidemic::core
