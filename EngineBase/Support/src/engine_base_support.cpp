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

#include <thread>
#include <utility>

namespace epidemic::enginebase
{
namespace detail
{
inline void ThrowIfFailed(const epidemic::foundation::Result<void> &result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }
}

inline void UpdateMemoryCounters(const std::shared_ptr<memory::IMemoryTracker> &memory_tracker)
{
    std::int64_t total_used_bytes = 0;
    std::int64_t total_peak_bytes = 0;

    for (std::size_t index = 0; index < static_cast<std::size_t>(memory::AllocationTag::Count); ++index)
    {
        const auto statistics = memory_tracker->GetStatistics(static_cast<memory::AllocationTag>(index));
        total_used_bytes += static_cast<std::int64_t>(statistics.allocated_bytes);
        total_peak_bytes += static_cast<std::int64_t>(statistics.peak_allocated_bytes);
    }

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryUsedBytes, total_used_bytes);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryPeakBytes, total_peak_bytes);
}
} // namespace detail

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

std::shared_ptr<diagnostics::ILogger> RegisterEngineBase(core::Application &application, EngineBaseOptions options)
{
    diagnostics::SetCurrentThreadName("Main");
    auto logger = application.Services().Emplace<diagnostics::ILogger, diagnostics::ConsoleLogger>();
    const auto configuration =
        application.Services().Emplace<core::config::IConfiguration, core::config::BasicConfiguration>();
    configuration->SetRuntimeName(std::move(options.runtime_name));
    configuration->SetWorkerCount(options.worker_count);
    configuration->SetMemoryTrackingEnabled(options.memory_tracking_enabled);
    configuration->SetRhiDebugEnabled(options.rhi_debug_enabled);
    configuration->SetDefaultWindowWidth(static_cast<std::int32_t>(options.default_window_width));
    configuration->SetDefaultWindowHeight(static_cast<std::int32_t>(options.default_window_height));

    application.Services().Emplace<core::events::IEventBus, core::events::EventBus>();
    application.Services().Emplace<core::tasks::ITaskScheduler, core::tasks::SimpleTaskScheduler>(options.worker_count);
    application.Services().Emplace<core::IMainThreadDispatcher, core::MainThreadDispatcher>();
    auto memory_tracker = application.Services().Emplace<memory::IMemoryTracker, memory::MemoryTracker>(options.memory_tracking_enabled);

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryUsedBytes, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryPeakBytes, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::EventBusQueuedEvents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::EventBusDispatchedEvents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::PlatformEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::InputEventsThisFrame, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiFrames, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiPresents, 0);
    diagnostics::GlobalCounters().Set(diagnostics::CounterId::RhiResizeCount, 0);

    application.AddFramePhaseHandler(
        core::FramePhase::EndFrame,
        [memory_tracker](const core::FrameContext &) { detail::UpdateMemoryCounters(memory_tracker); },
        "EngineBaseSupport::UpdateMemoryCounters");

    logger->Info(options.log_module, "Startup", "Diagnostics baseline initialized");
    logger->Info(options.log_module, "Startup", "Worker threads: " + std::to_string(options.worker_count));
    logger->Info(options.log_module, "Startup",
                 std::string("Memory tracking: ") + (options.memory_tracking_enabled ? "enabled" : "disabled"));
    logger->Info(options.log_module, "Startup", "Main-thread dispatcher: ready");
    return logger;
}

std::shared_ptr<platform::IPlatformRuntime> RegisterWindowsRuntime(core::Application &application)
{
    auto platform_runtime = std::make_shared<platform::WindowsPlatformRuntime>();
    application.Services().RegisterInstance<platform::IPlatformRuntime>(platform_runtime);
    application.Services().RegisterInstance<platform::IWindowSystem>(platform_runtime);
    return platform_runtime;
}

std::shared_ptr<input::IInputSystem> RegisterInputRuntime(core::Application &application)
{
    auto input_system = std::make_shared<input::InputSystem>();
    application.Services().RegisterInstance<input::IInputSystem>(input_system);
    return input_system;
}

foundation::Result<GraphicsRuntimeServices> RegisterGraphicsRuntime(core::Application &application,
                                                                    GraphicsRuntimeOptions options)
{
    rhi::RhiDeviceDesc device_desc;
    device_desc.enable_debug_validation = options.enable_debug_validation;
    device_desc.debug_name = std::move(options.debug_name);

    foundation::Result<std::shared_ptr<rhi::IRhiDevice>> device_result =
        options.backend == GraphicsBackend::D3D11 ? rhi::d3d11::CreateD3D11RhiDevice(device_desc)
                                                  : rhi::CreateNullRhiDevice(device_desc);
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
    application.Services().RegisterInstance<rhi::IRhiDevice>(device);
    application.Services().RegisterInstance<rhi::IRhiCommandContext>(command_context);

    if (application.Services().Contains<diagnostics::ILogger>())
    {
        const auto logger = application.Services().Get<diagnostics::ILogger>();
        logger->Info("EngineBaseSupport", "RHI", "Graphics runtime ready: backend=" + std::string(device->BackendName()));
    }

    return foundation::Result<GraphicsRuntimeServices>::Success(GraphicsRuntimeServices{std::move(device), std::move(command_context)});
}

foundation::Result<std::shared_ptr<platform::IWindow>>
CreateMainWindow(core::Application &application, const platform::WindowCreateInfo &create_info)
{
    const auto window_system = application.Services().Get<platform::IWindowSystem>();
    return window_system->CreateWindow(create_info);
}

foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>
RegisterMainSwapChain(core::Application &application,
                      const std::shared_ptr<platform::IWindow> &window,
                      const rhi::RhiSwapChainDesc &desc)
{
    auto swap_chain_desc = desc;
    swap_chain_desc.surface_handle = rhi::PresentationSurfaceHandle(window->GetNativeHandle().Value());

    const auto device = application.Services().Get<rhi::IRhiDevice>();
    auto swap_chain_result = device->CreateSwapChain(swap_chain_desc);
    if (!swap_chain_result.HasValue())
    {
        return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Failure(swap_chain_result.GetError());
    }

    auto swap_chain = std::move(swap_chain_result).Value();
    application.Services().RegisterInstance<rhi::IRhiSwapChain>(swap_chain);

    if (application.Services().Contains<diagnostics::ILogger>())
    {
        const auto logger = application.Services().Get<diagnostics::ILogger>();
        logger->Info("EngineBaseSupport", "RHI",
                     "Main swap chain ready: " + std::to_string(swap_chain->Width()) + "x" +
                         std::to_string(swap_chain->Height()) + ", backend=" + std::string(device->BackendName()));
    }

    return foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>::Success(std::move(swap_chain));
}

void RegisterPlatformFrameLoop(core::Application &application)
{
    auto frame_platform_events = EnsureFramePlatformEvents(application);

    application.AddFramePhaseHandler(
        core::FramePhase::PumpPlatformEvents,
        [&application, frame_platform_events](const core::FrameContext &) {
            const auto platform_runtime = application.Services().Get<platform::IPlatformRuntime>();
            const auto window_system = application.Services().Get<platform::IWindowSystem>();
            platform_runtime->PumpEvents();
            frame_platform_events->events = window_system->DrainEvents();
            diagnostics::GlobalCounters().Set(diagnostics::CounterId::PlatformEventsThisFrame,
                                              static_cast<std::int64_t>(frame_platform_events->events.size()));

            if (platform_runtime->IsExitRequested())
            {
                application.RequestStop();
            }
        },
        "EngineBaseSupport::PumpPlatformEvents");
}

void RegisterInputFrameLoop(core::Application &application)
{
    auto frame_platform_events = EnsureFramePlatformEvents(application);

    application.AddFramePhaseHandler(
        core::FramePhase::UpdateInput,
        [&application, frame_platform_events](const core::FrameContext &) {
            const auto input_system = application.Services().Get<input::IInputSystem>();
            input_system->QueuePlatformEvents(frame_platform_events->events);
            input_system->PublishSnapshot();
            diagnostics::GlobalCounters().Set(diagnostics::CounterId::InputEventsThisFrame,
                                              static_cast<std::int64_t>(input_system->CurrentEvents().size()));
        },
        "EngineBaseSupport::UpdateInput");
}

void RegisterRhiFrameLoop(core::Application &application,
                          const std::shared_ptr<rhi::IRhiCommandContext> &command_context,
                          const std::shared_ptr<rhi::IRhiSwapChain> &swap_chain,
                          const std::shared_ptr<bool> &render_paused,
                          const std::shared_ptr<rhi::RhiClearDesc> &clear_desc)
{
    application.AddFramePhaseHandler(
        core::FramePhase::RhiBeginFrame,
        [command_context, render_paused, clear_desc](const core::FrameContext &) {
            if (*render_paused)
            {
                return;
            }

            detail::ThrowIfFailed(command_context->BeginFrame());
            detail::ThrowIfFailed(command_context->Clear(*clear_desc));
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::RhiFrames);
        },
        "EngineBaseSupport::RhiBeginFrame");

    application.AddFramePhaseHandler(
        core::FramePhase::RhiEndFrame,
        [command_context](const core::FrameContext &) {
            if (!command_context->IsFrameActive())
            {
                return;
            }

            detail::ThrowIfFailed(command_context->EndFrame());
        },
        "EngineBaseSupport::RhiEndFrame");

    application.AddFramePhaseHandler(
        core::FramePhase::Present,
        [command_context, swap_chain, render_paused](const core::FrameContext &) {
            if (*render_paused || command_context->IsFrameActive())
            {
                return;
            }

            detail::ThrowIfFailed(swap_chain->Present());
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::RhiPresents);
        },
        "EngineBaseSupport::Present");
}

void RegisterFrameThrottle(core::Application &application, std::chrono::milliseconds duration)
{
    application.AddFramePhaseHandler(
        core::FramePhase::EndFrame,
        [duration](const core::FrameContext &) { std::this_thread::sleep_for(duration); },
        "EngineBaseSupport::Throttle");
}
} // namespace epidemic::enginebase