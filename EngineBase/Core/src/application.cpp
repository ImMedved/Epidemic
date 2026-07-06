#include <Epidemic/Core/application.h>

#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/profiling.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
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

[[nodiscard]] constexpr std::size_t ToIndex(FramePhase phase) noexcept
{
    return static_cast<std::size_t>(phase);
}

void UpdatePhaseDiagnostics(FramePhase phase, std::int64_t duration_micros) noexcept
{
    using diagnostics::CounterId;

    switch (phase)
    {
    case FramePhase::PumpPlatformEvents:
        diagnostics::GlobalCounters().Set(CounterId::PlatformPumpTimeMicros, duration_micros);
        break;
    case FramePhase::UpdateInput:
        diagnostics::GlobalCounters().Set(CounterId::InputUpdateTimeMicros, duration_micros);
        break;
    case FramePhase::TickModules:
        diagnostics::GlobalCounters().Set(CounterId::ModuleTickTimeMicros, duration_micros);
        break;
    case FramePhase::Present:
        diagnostics::GlobalCounters().Set(CounterId::PresentTimeMicros, duration_micros);
        break;
    default:
        break;
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
        if (options_.frame_limit.has_value())
        {
            logger->Info("Application", "Config", "Frame limit: " + std::to_string(*options_.frame_limit));
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

    state_ = State::Running;

    try
    {
        ExecuteFrame();
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
    state_ = State::Running;

    try
    {
        while (!StopRequested() && !ReachedFrameLimit())
        {
            ExecuteFrame();
        }

        if (StopRequested())
        {
            logger->Info("Application", "Lifecycle", "Run finished (stop requested)");
        }
        else if (ReachedFrameLimit())
        {
            logger->Info("Application", "Lifecycle", "Run finished (frame limit reached)");
        }
        else
        {
            logger->Info("Application", "Lifecycle", "Run finished");
        }

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

    try
    {
        ValidateCoreServices();
        auto logger = Logger();
        logger->Info("Application", "Lifecycle", "Shutdown started");

        modules_.ShutdownAll(services_, *logger);

        const auto scheduler = services_.Get<tasks::ITaskScheduler>();
        try
        {
            scheduler->Shutdown();
        }
        catch (const std::exception &exception)
        {
            logger->Error("Application", "Shutdown",
                          "Task scheduler shutdown reported an error: " + std::string(exception.what()));
        }
        catch (...)
        {
            logger->Error("Application", "Shutdown",
                          "Task scheduler shutdown reported an unknown error");
        }

        try
        {
            scheduler->WaitIdle();
        }
        catch (const std::exception &exception)
        {
            logger->Error("Application", "Shutdown",
                          "Task scheduler drain reported an error during shutdown: " + std::string(exception.what()));
        }
        catch (...)
        {
            logger->Error("Application", "Shutdown",
                          "Task scheduler drain reported an unknown error during shutdown");
        }

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

void Application::RequestStop() noexcept
{
    stop_requested_.store(true, std::memory_order_relaxed);
}

void Application::ResetStopRequest() noexcept
{
    stop_requested_.store(false, std::memory_order_relaxed);
}

bool Application::StopRequested() const noexcept
{
    return stop_requested_.load(std::memory_order_relaxed);
}

void Application::SetFrameLimit(std::optional<std::uint64_t> frame_limit) noexcept
{
    options_.frame_limit = frame_limit;
}

std::optional<std::uint64_t> Application::FrameLimit() const noexcept
{
    return options_.frame_limit;
}

void Application::AddFramePhaseHandler(FramePhase phase, FramePhaseCallback callback, std::string debug_name)
{
    if (!callback)
    {
        throw std::invalid_argument("Frame phase handler must not be empty");
    }

    phase_handlers_[ToIndex(phase)].push_back(PhaseHandler{std::move(callback), std::move(debug_name)});
}

void Application::ScheduleMainThreadTask(MainThreadTask task, std::string debug_name)
{
    if (!task)
    {
        throw std::invalid_argument("Main thread task must not be empty");
    }

    services_.Get<IMainThreadDispatcher>()->Post(std::move(task), std::move(debug_name));
}

const FrameContext &Application::CurrentFrameContext() const noexcept
{
    return current_frame_context_;
}

void Application::ValidateCoreServices() const
{
    EnsureRegistered<diagnostics::ILogger>(services_, "diagnostics::ILogger");
    EnsureRegistered<config::IConfiguration>(services_, "config::IConfiguration");
    EnsureRegistered<events::IEventBus>(services_, "events::IEventBus");
    EnsureRegistered<tasks::ITaskScheduler>(services_, "tasks::ITaskScheduler");
    EnsureRegistered<IMainThreadDispatcher>(services_, "IMainThreadDispatcher");
}

std::shared_ptr<diagnostics::ILogger> Application::Logger() const
{
    return services_.Get<diagnostics::ILogger>();
}

void Application::ExecuteFrame()
{
    EPIDEMIC_PROFILE_SCOPE("Application::Tick");
    const auto tick_start = std::chrono::steady_clock::now();
    auto logger = Logger();
    const auto frame_context = BuildNextFrameContext();

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::CurrentFrameIndex,
                                      static_cast<std::int64_t>(frame_context.frame_index.Value()));
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::PlatformPumpTimeMicros, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::InputUpdateTimeMicros, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::ModuleTickTimeMicros, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::PresentTimeMicros, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MainThreadTasksExecuted, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::PlatformEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::InputEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::EventBusDispatchedEvents, 0);

    logger->Debug("Application", "Frame", "Frame " + std::to_string(frame_context.frame_index.Value()) + " started");

    for (const auto phase : FramePhaseOrder())
    {
        ExecutePhase(phase, frame_context, *logger);
    }

    const auto scheduler = services_.Get<tasks::ITaskScheduler>();
    scheduler->WaitIdle();

    diagnostics::GlobalCounters().Increment(diagnostics::CounterId::Frames);
    const auto frame_time_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tick_start).count();
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::FrameTimeMicros, frame_time_micros);

    logger->Debug("Application", "Frame",
                  "Frame " + std::to_string(frame_context.frame_index.Value()) + " finished in " +
                      std::to_string(frame_time_micros) + " us");
    ++executed_frame_count_;
}

void Application::ExecutePhase(FramePhase phase, const FrameContext &frame_context, diagnostics::ILogger &logger)
{
    const auto phase_start = std::chrono::steady_clock::now();

    switch (phase)
    {
    case FramePhase::BeginFrame:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::BeginFrame");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::PumpPlatformEvents:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::PumpPlatformEvents");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::UpdateInput:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::UpdateInput");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::DrainEvents:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::DrainEvents");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        const auto event_bus = services_.Get<events::IEventBus>();
        static_cast<void>(event_bus->DrainQueued());
        break;
    }
    case FramePhase::RunScheduledMainThreadTasks:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::RunScheduledMainThreadTasks");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        static_cast<void>(RunScheduledMainThreadTasks());
        break;
    }
    case FramePhase::TickModules:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::TickModules");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        modules_.TickAll(services_, logger, frame_context);
        break;
    }
    case FramePhase::RhiBeginFrame:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::RhiBeginFrame");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::RhiEndFrame:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::RhiEndFrame");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::Present:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::Present");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::EndFrame:
    {
        EPIDEMIC_PROFILE_SCOPE("Application::Phase::EndFrame");
        ExecuteRegisteredPhaseHandlers(phase, frame_context);
        break;
    }
    case FramePhase::Count:
        break;
    }

    const auto duration_micros =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - phase_start).count();
    UpdatePhaseDiagnostics(phase, duration_micros);
}

void Application::ExecuteRegisteredPhaseHandlers(FramePhase phase, const FrameContext &frame_context)
{
    for (const auto &handler : phase_handlers_[ToIndex(phase)])
    {
        handler.callback(frame_context);
    }
}

FrameContext Application::BuildNextFrameContext()
{
    const auto now = foundation::Clock::now();
    if (!frame_clock_initialized_)
    {
        run_start_time_ = now;
        previous_frame_time_ = now;
        frame_clock_initialized_ = true;
    }

    auto raw_delta_time = now - previous_frame_time_;
    if (executed_frame_count_ == 0)
    {
        raw_delta_time = foundation::Duration::zero();
    }

    previous_frame_time_ = now;

    current_frame_context_ = FrameContext{
        foundation::FrameIndex(executed_frame_count_),
        foundation::FrameTime::FromDuration(raw_delta_time),
        foundation::FrameTime::FromDuration(now - run_start_time_),
        foundation::FrameTime::FromDuration(raw_delta_time),
    };
    return current_frame_context_;
}

bool Application::ReachedFrameLimit() const noexcept
{
    return options_.frame_limit.has_value() && executed_frame_count_ >= *options_.frame_limit;
}

std::size_t Application::RunScheduledMainThreadTasks()
{
    return services_.Get<IMainThreadDispatcher>()->Drain();
}
} // namespace epidemic::core