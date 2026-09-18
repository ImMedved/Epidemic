#include <Epidemic/EngineBase/engine_base_support.h>

#include <Epidemic/Core/basic_configuration.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/frame_phase.h>
#include <Epidemic/Core/main_thread_dispatcher.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/console_logger.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/thread_context.h>
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Memory/imemory_tracker.h>
#include <Epidemic/Memory/memory_tracker.h>
#include <Epidemic/Platform/windows_platform_runtime.h>
#include <Epidemic/RHI/null_rhi_device.h>
#include <Epidemic/RHI_D3D11/d3d11_rhi_device.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace epidemic::enginebase
{
// This file implements the convenience composition helpers declared in engine_base_support.h.
// It wires the baseline services and frame-loop hooks without introducing higher-level runtime systems.

namespace detail
{
// Throws before a composition helper starts mutating the service graph when one of the
// services in its bundle is already present or the graph has already been sealed.
// Support composition is owner-thread/external-serialization code; this preflight is not
// intended to turn ServiceContainer into a transactional concurrent registry.
template <typename... TServices>
void EnsureRegistrationBundleAvailable(const core::ServiceContainer &services, std::string_view bundle_name)
{
    if (services.IsSealed())
    {
        throw std::runtime_error(std::string(bundle_name) + " cannot be registered after the service container is sealed");
    }

    if ((services.Contains<TServices>() || ...))
    {
        throw std::runtime_error(std::string(bundle_name) + " conflicts with an already registered service");
    }
}

// Returns a Result-friendly composition error before a fallible runtime factory is invoked.
template <typename... TServices>
[[nodiscard]] std::optional<foundation::Error> ValidateResultRegistrationBundle(
    const core::ServiceContainer &services, std::string_view bundle_name)
{
    if (services.IsSealed())
    {
        return foundation::Error::Create("engine_base.support.service_container_sealed",
                                         "Cannot register " + std::string(bundle_name) +
                                             " after the service container is sealed",
                                         std::string(bundle_name));
    }

    if ((services.Contains<TServices>() || ...))
    {
        return foundation::Error::Create("engine_base.support.service_registration_conflict",
                                         "Cannot register " + std::string(bundle_name) +
                                             " because one of its service roles is already registered",
                                         std::string(bundle_name));
    }

    return std::nullopt;
}

// Converts a Result<void> failure into an exception for internal fail-fast frame-loop glue.
inline void ThrowIfFailed(const epidemic::foundation::Result<void> &result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }
}

[[nodiscard]] constexpr std::int64_t SaturatingCounterValue(std::size_t value) noexcept
{
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (value > static_cast<std::size_t>(maximum))
    {
        return maximum;
    }
    return static_cast<std::int64_t>(value);
}

[[nodiscard]] constexpr std::int64_t SaturatingCounterAdd(std::int64_t left, std::int64_t right) noexcept
{
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (right > maximum - left)
    {
        return maximum;
    }
    return left + right;
}

// Aggregates per-tag memory statistics into the global diagnostics counters without signed overflow.
inline void UpdateMemoryCounters(const std::shared_ptr<memory::IMemoryTracker> &memory_tracker)
{
    std::int64_t total_used_bytes = 0;
    std::int64_t total_peak_bytes = 0;

    for (std::size_t index = 0; index < static_cast<std::size_t>(memory::AllocationTag::Count); ++index)
    {
        const auto statistics = memory_tracker->GetStatistics(static_cast<memory::AllocationTag>(index));
        total_used_bytes = SaturatingCounterAdd(total_used_bytes, SaturatingCounterValue(statistics.allocated_bytes));
        total_peak_bytes =
            SaturatingCounterAdd(total_peak_bytes, SaturatingCounterValue(statistics.peak_allocated_bytes));
    }

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryUsedBytes, total_used_bytes);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryPeakBytes, total_peak_bytes);
}

// Commits a frame-handler batch together with lazy FramePlatformEvents publication.
// The service candidate is prepared first; handler insertion has a strong guarantee; service publication
// is then a no-allocation map swap. This is the common Support composition transaction primitive.
template <typename TBuildRegistrations>
std::shared_ptr<FramePlatformEvents> RegisterFrameHandlersWithEventsAtomic(core::Application &application,
                                                                          TBuildRegistrations &&build_registrations)
{
    auto &services = application.Services();
    if (services.Contains<FramePlatformEvents>())
    {
        auto frame_events = services.Get<FramePlatformEvents>();
        application.AddFramePhaseHandlersAtomic(std::forward<TBuildRegistrations>(build_registrations)(frame_events));
        return frame_events;
    }

    auto frame_events = std::make_shared<FramePlatformEvents>();
    auto registrations = std::forward<TBuildRegistrations>(build_registrations)(frame_events);
    services.RegisterInstancesAtomicWithPreCommit(
        [&application, registrations = std::move(registrations)]() mutable {
            application.AddFramePhaseHandlersAtomic(std::move(registrations));
        },
        frame_events);
    return frame_events;
}
} // namespace detail

// Returns the shared per-frame platform-event buffer, creating it on first use.
std::shared_ptr<FramePlatformEvents> EnsureFramePlatformEvents(core::Application &application)
{
    if (application.Services().Contains<FramePlatformEvents>())
    {
        return application.Services().Get<FramePlatformEvents>();
    }

    auto frame_platform_events = std::make_shared<FramePlatformEvents>();
    application.Services().RegisterInstance<FramePlatformEvents>(frame_platform_events);
    return frame_platform_events;
}

// Registers the baseline services needed by the EngineBase application shell.
std::shared_ptr<diagnostics::ILogger> RegisterEngineBase(core::Application &application, EngineBaseOptions options)
{
    auto &services = application.Services();
    detail::EnsureRegistrationBundleAvailable<diagnostics::ILogger, core::config::IConfiguration, core::events::IEventBus,
                                              core::tasks::ITaskScheduler, core::IMainThreadDispatcher,
                                              memory::IMemoryTracker>(services, "EngineBase baseline");

    const auto normalized_worker_count = std::max<std::size_t>(options.worker_count, 1);
    if (normalized_worker_count > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
    {
        throw std::invalid_argument("EngineBase worker count exceeds the configuration range");
    }
    if (options.default_window_width == 0 || options.default_window_height == 0 ||
        options.default_window_width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        options.default_window_height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
    {
        throw std::invalid_argument("EngineBase default window dimensions must fit positive int32 values");
    }

    // Build and configure the bundle off-state. Expected validation/construction failures therefore
    // cannot leave a partially composed Application service graph.
    auto logger = std::make_shared<diagnostics::ConsoleLogger>();
    auto configuration = std::make_shared<core::config::BasicConfiguration>();
    configuration->SetRuntimeName(std::move(options.runtime_name));
    configuration->SetWorkerCount(normalized_worker_count);
    configuration->SetMemoryTrackingEnabled(options.memory_tracking_enabled);
    configuration->SetRhiDebugEnabled(options.rhi_debug_enabled);
    configuration->SetDefaultWindowWidth(static_cast<std::int32_t>(options.default_window_width));
    configuration->SetDefaultWindowHeight(static_cast<std::int32_t>(options.default_window_height));

    auto event_bus = std::make_shared<core::events::EventBus>();
    auto task_scheduler = std::make_shared<core::tasks::SimpleTaskScheduler>(normalized_worker_count);
    auto main_thread_dispatcher = std::make_shared<core::MainThreadDispatcher>();
    auto memory_tracker = std::make_shared<memory::MemoryTracker>(options.memory_tracking_enabled);

    // Re-check immediately before publication so an externally serialized caller gets deterministic
    // duplicate/sealed rejection even if lengthy bundle construction preceded registration.
    detail::EnsureRegistrationBundleAvailable<diagnostics::ILogger, core::config::IConfiguration, core::events::IEventBus,
                                              core::tasks::ITaskScheduler, core::IMainThreadDispatcher,
                                              memory::IMemoryTracker>(services, "EngineBase baseline");
    services.RegisterInstancesAtomicWithPreCommit(
        [&application, memory_tracker] {
            std::vector<core::Application::FramePhaseHandlerRegistration> registrations;
            registrations.push_back(core::Application::FramePhaseHandlerRegistration{
                core::FramePhase::EndFrame,
                [memory_tracker](const core::FrameContext &) { detail::UpdateMemoryCounters(memory_tracker); },
                "EngineBaseSupport::UpdateMemoryCounters"});
            application.AddFramePhaseHandlersAtomic(std::move(registrations));
        },
        std::shared_ptr<diagnostics::ILogger>(logger), std::shared_ptr<core::config::IConfiguration>(configuration),
        std::shared_ptr<core::events::IEventBus>(event_bus), std::shared_ptr<core::tasks::ITaskScheduler>(task_scheduler),
        std::shared_ptr<core::IMainThreadDispatcher>(main_thread_dispatcher),
        std::shared_ptr<memory::IMemoryTracker>(memory_tracker));

    diagnostics::SetCurrentThreadName("Main");

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryUsedBytes, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryPeakBytes, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::EventBusQueuedEvents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::EventBusDispatchedEvents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::PlatformEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::InputEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiFrames, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiPresents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiResizeCount, 0);

    try
    {
        logger->Info(options.log_module, "Startup", "Diagnostics baseline initialized");
        logger->Info(options.log_module, "Startup", "Worker threads: " + std::to_string(normalized_worker_count));
        logger->Info(options.log_module, "Startup",
                     std::string("Memory tracking: ") + (options.memory_tracking_enabled ? "enabled" : "disabled"));
        logger->Info(options.log_module, "Startup", "Main-thread dispatcher: ready");
    }
    catch (...)
    {
        // Diagnostics is non-authoritative. A sink failure must not turn a committed composition into failure.
    }
    return logger;
}

// Registers the Win32 platform runtime as both runtime and window-system service.
std::shared_ptr<platform::IPlatformRuntime> RegisterWindowsRuntime(core::Application &application)
{
    auto &services = application.Services();
    detail::EnsureRegistrationBundleAvailable<platform::IPlatformRuntime, platform::IWindowSystem>(
        services, "Windows platform runtime");

    auto platform_runtime = std::make_shared<platform::WindowsPlatformRuntime>();
    detail::EnsureRegistrationBundleAvailable<platform::IPlatformRuntime, platform::IWindowSystem>(
        services, "Windows platform runtime");
    services.RegisterInstancesAtomic<platform::IPlatformRuntime, platform::IWindowSystem>(platform_runtime,
                                                                                          platform_runtime);
    return platform_runtime;
}

// Registers the normalized input system service.
std::shared_ptr<input::IInputSystem> RegisterInputRuntime(core::Application &application)
{
    auto &services = application.Services();
    detail::EnsureRegistrationBundleAvailable<input::IInputSystem>(services, "input runtime");
    auto input_system = std::make_shared<input::InputSystem>();
    services.RegisterInstance<input::IInputSystem>(input_system);
    return input_system;
}

// Creates and registers the requested graphics backend and its primary command context.
foundation::Result<GraphicsRuntimeServices> RegisterGraphicsRuntime(core::Application &application,
                                                                    GraphicsRuntimeOptions options)
{
    auto &services = application.Services();
    if (const auto registration_error =
            detail::ValidateResultRegistrationBundle<rhi::IRhiDevice, rhi::IRhiCommandContext>(
                services, "graphics runtime");
        registration_error.has_value())
    {
        return foundation::Result<GraphicsRuntimeServices>::Failure(*registration_error);
    }

    rhi::RhiDeviceDesc device_desc;
    device_desc.enable_debug_validation = options.enable_debug_validation;
    device_desc.debug_name = std::move(options.debug_name);

    foundation::Result<std::shared_ptr<rhi::IRhiDevice>> device_result =
        foundation::Result<std::shared_ptr<rhi::IRhiDevice>>::Failure(
            foundation::Error::Create("engine_base.support.invalid_graphics_backend",
                                      "Graphics backend value is not recognized"));
    switch (options.backend)
    {
    case GraphicsBackend::Null:
        device_result = rhi::CreateNullRhiDevice(device_desc);
        break;
    case GraphicsBackend::D3D11:
        device_result = rhi::d3d11::CreateD3D11RhiDevice(device_desc);
        break;
    default:
        return foundation::Result<GraphicsRuntimeServices>::Failure(device_result.GetError());
    }
    if (!device_result.HasValue())
    {
        return foundation::Result<GraphicsRuntimeServices>::Failure(device_result.GetError());
    }

    auto device = std::move(device_result).Value();
    auto command_context_result = device->CreateCommandContext();
    if (!command_context_result.HasValue())
    {
        return foundation::Result<GraphicsRuntimeServices>::Failure(command_context_result.GetError());
    }

    auto command_context = std::move(command_context_result).Value();
    if (const auto registration_error =
            detail::ValidateResultRegistrationBundle<rhi::IRhiDevice, rhi::IRhiCommandContext>(
                services, "graphics runtime");
        registration_error.has_value())
    {
        return foundation::Result<GraphicsRuntimeServices>::Failure(*registration_error);
    }

    try
    {
        services.RegisterInstancesAtomic<rhi::IRhiDevice, rhi::IRhiCommandContext>(device, command_context);
    }
    catch (const std::exception &exception)
    {
        return foundation::Result<GraphicsRuntimeServices>::Failure(
            foundation::Error::Create("engine_base.support.service_registration_failed", exception.what(),
                                      "graphics runtime"));
    }

    if (services.Contains<diagnostics::ILogger>())
    {
        try
        {
            const auto logger = services.Get<diagnostics::ILogger>();
            logger->Info("EngineBaseSupport", "RHI",
                         "Graphics runtime ready: backend=" + std::string(device->BackendName()));
        }
        catch (...)
        {
        }
    }

    return foundation::Result<GraphicsRuntimeServices>::Success(
        GraphicsRuntimeServices{std::move(device), std::move(command_context)});
}

// Creates the main window through the registered IWindowSystem service.
foundation::Result<std::shared_ptr<platform::IWindow>>
CreateMainWindow(core::Application &application, const platform::WindowCreateInfo &create_info)
{
    if (!application.Services().Contains<platform::IWindowSystem>())
    {
        return foundation::Result<std::shared_ptr<platform::IWindow>>::Failure(
            foundation::Error::Create("engine_base.support.window_system_required",
                                      "Main window creation requires a registered window system"));
    }

    const auto window_system = application.Services().Get<platform::IWindowSystem>();
    return window_system->CreateWindow(create_info);
}

// Creates and registers the application's main swap chain for the supplied window.
foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>
RegisterMainSwapChain(core::Application &application,
                      const std::shared_ptr<platform::IWindow> &window,
                      const rhi::RhiSwapChainDesc &desc)
{
    if (!window)
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(
            foundation::Error::Create("engine_base.support.window_required",
                                      "Main swap-chain registration requires a window"));
    }

    auto &services = application.Services();
    if (const auto registration_error = detail::ValidateResultRegistrationBundle<rhi::IRhiSwapChain>(
            services, "main swap chain");
        registration_error.has_value())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(*registration_error);
    }

    const auto native_handle = window->GetNativeHandle();
    if (!native_handle.IsValid())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(
            foundation::Error::Create("engine_base.support.invalid_window_handle",
                                      "Main swap-chain registration requires a valid native window handle"));
    }

    if (!services.Contains<rhi::IRhiDevice>())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(
            foundation::Error::Create("engine_base.support.rhi_device_required",
                                      "Main swap-chain registration requires a registered RHI device"));
    }

    auto swap_chain_desc = desc;
    swap_chain_desc.surface_handle = rhi::PresentationSurfaceHandle(native_handle.Value());

    const auto device = services.Get<rhi::IRhiDevice>();
    auto swap_chain_result = device->CreateSwapChain(swap_chain_desc);
    if (!swap_chain_result.HasValue())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(swap_chain_result.GetError());
    }

    auto swap_chain = std::move(swap_chain_result).Value();
    if (const auto registration_error = detail::ValidateResultRegistrationBundle<rhi::IRhiSwapChain>(
            services, "main swap chain");
        registration_error.has_value())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(*registration_error);
    }

    try
    {
        services.RegisterInstance<rhi::IRhiSwapChain>(swap_chain);
    }
    catch (const std::exception &exception)
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(
            foundation::Error::Create("engine_base.support.service_registration_failed", exception.what(),
                                      "main swap chain"));
    }

    if (services.Contains<diagnostics::ILogger>())
    {
        try
        {
            const auto logger = services.Get<diagnostics::ILogger>();
            logger->Info("EngineBaseSupport", "RHI",
                         "Main swap chain ready: " + std::to_string(swap_chain->Width()) + "x" +
                             std::to_string(swap_chain->Height()) + ", backend=" + std::string(device->BackendName()));
        }
        catch (...)
        {
        }
    }

    return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Success(std::move(swap_chain));
}

// Wires platform message pumping and platform-event drainage into the frame loop.
void RegisterPlatformFrameLoop(core::Application &application)
{
    const auto platform_runtime = application.Services().Get<platform::IPlatformRuntime>();
    const auto window_system = application.Services().Get<platform::IWindowSystem>();

    detail::RegisterFrameHandlersWithEventsAtomic(
        application,
        [&application, platform_runtime, window_system](const std::shared_ptr<FramePlatformEvents> &frame_platform_events) {
            std::vector<core::Application::FramePhaseHandlerRegistration> registrations;
            registrations.push_back(core::Application::FramePhaseHandlerRegistration{
                core::FramePhase::PumpPlatformEvents,
                [&application, frame_platform_events, platform_runtime, window_system](const core::FrameContext &) {
                    platform_runtime->PumpEvents();
                    frame_platform_events->events = window_system->DrainEvents();
                    diagnostics::GlobalCounters().Set(
                        diagnostics::CounterId::PlatformEventsThisFrame,
                        static_cast<std::int64_t>(frame_platform_events->events.size()));

                    if (platform_runtime->IsExitRequested())
                    {
                        application.RequestStop();
                    }
                },
                "EngineBaseSupport::PumpPlatformEvents"});
            return registrations;
        });
}

// Wires input normalization into the frame loop after platform events are drained.
void RegisterInputFrameLoop(core::Application &application)
{
    const auto input_system = application.Services().Get<input::IInputSystem>();

    detail::RegisterFrameHandlersWithEventsAtomic(
        application, [input_system](const std::shared_ptr<FramePlatformEvents> &frame_platform_events) {
            std::vector<core::Application::FramePhaseHandlerRegistration> registrations;
            registrations.push_back(core::Application::FramePhaseHandlerRegistration{
                core::FramePhase::UpdateInput,
                [frame_platform_events, input_system](const core::FrameContext &) {
                    input_system->QueuePlatformEvents(frame_platform_events->events);
                    input_system->PublishSnapshot();
                    diagnostics::GlobalCounters().Set(
                        diagnostics::CounterId::InputEventsThisFrame,
                        static_cast<std::int64_t>(input_system->CurrentEvents().size()));
                },
                "EngineBaseSupport::UpdateInput"});
            return registrations;
        });
}

// Wires the baseline RHI frame steps into the application frame phases.
void RegisterRhiFrameLoop(core::Application &application,
                          const std::shared_ptr<rhi::IRhiCommandContext> &command_context,
                          const std::shared_ptr<rhi::IRhiSwapChain> &swap_chain,
                          const std::shared_ptr<bool> &render_paused,
                          const std::shared_ptr<rhi::RhiClearDesc> &clear_desc)
{
    if (!command_context || !swap_chain || !render_paused || !clear_desc)
    {
        throw std::invalid_argument("RHI frame-loop registration requires non-null runtime services and state");
    }

    std::vector<core::Application::FramePhaseHandlerRegistration> registrations;
    registrations.reserve(3);
    registrations.push_back(core::Application::FramePhaseHandlerRegistration{
        core::FramePhase::RhiBeginFrame,
        [command_context, render_paused, clear_desc](const core::FrameContext &) {
            if (*render_paused)
            {
                return;
            }

            detail::ThrowIfFailed(command_context->BeginFrame());
            try
            {
                detail::ThrowIfFailed(command_context->Clear(*clear_desc));
            }
            catch (...)
            {
                // A failed clear aborts the frame before the normal RhiEndFrame phase can run.
                // Explicitly close the active frame so the next tick can recover.
                if (command_context->IsFrameActive())
                {
                    static_cast<void>(command_context->EndFrame());
                }
                throw;
            }
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::RhiFrames);
        },
        "EngineBaseSupport::RhiBeginFrame"});

    registrations.push_back(core::Application::FramePhaseHandlerRegistration{
        core::FramePhase::RhiEndFrame,
        [command_context](const core::FrameContext &) {
            if (!command_context->IsFrameActive())
            {
                return;
            }
            detail::ThrowIfFailed(command_context->EndFrame());
        },
        "EngineBaseSupport::RhiEndFrame"});

    registrations.push_back(core::Application::FramePhaseHandlerRegistration{
        core::FramePhase::Present,
        [command_context, swap_chain, render_paused](const core::FrameContext &) {
            if (*render_paused || command_context->IsFrameActive())
            {
                return;
            }
            detail::ThrowIfFailed(swap_chain->Present());
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::RhiPresents);
        },
        "EngineBaseSupport::Present"});

    application.AddFramePhaseHandlersAtomic(std::move(registrations));
}

// Adds a simple end-of-frame sleep handler for fixed pacing in smoke scenarios.
void RegisterFrameThrottle(core::Application &application, std::chrono::milliseconds duration)
{
    application.AddFramePhaseHandler(
        core::FramePhase::EndFrame,
        [duration](const core::FrameContext &) { std::this_thread::sleep_for(duration); },
        "EngineBaseSupport::Throttle");
}
} // namespace epidemic::enginebase