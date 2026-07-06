#pragma once

#include <Epidemic/Core/application.h>
#include <Epidemic/Core/basic_configuration.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/frame_phase.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/console_logger.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/thread_context.h>
#include <Epidemic/Input/iinput_system.h>
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_swap_chain.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace epidemic::apps
{
struct CoreServicesOptions
{
    std::string runtime_name{"EpidemicApp"};
    std::string log_module{"App"};
    std::size_t worker_count{1};
    bool memory_tracking_enabled{true};
    bool rhi_debug_enabled{false};
    std::int32_t default_window_width{1280};
    std::int32_t default_window_height{720};
};

struct FramePlatformEvents
{
    std::vector<platform::PlatformEvent> events;
};

[[nodiscard]] inline std::shared_ptr<diagnostics::ILogger>
RegisterCoreRuntimeServices(core::Application &application, CoreServicesOptions options)
{
    diagnostics::SetCurrentThreadName("Main");
    auto logger = application.Services().Emplace<diagnostics::ILogger, diagnostics::ConsoleLogger>();
    const auto configuration =
        application.Services().Emplace<core::config::IConfiguration, core::config::BasicConfiguration>();
    configuration->SetRuntimeName(std::move(options.runtime_name));
    configuration->SetWorkerCount(options.worker_count);
    configuration->SetMemoryTrackingEnabled(options.memory_tracking_enabled);
    configuration->SetRhiDebugEnabled(options.rhi_debug_enabled);
    configuration->SetDefaultWindowWidth(options.default_window_width);
    configuration->SetDefaultWindowHeight(options.default_window_height);
    application.Services().Emplace<core::events::IEventBus, core::events::EventBus>();
    application.Services().Emplace<core::tasks::ITaskScheduler, core::tasks::SimpleTaskScheduler>(options.worker_count);

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::MemoryUsed, 0);
    logger->Info(options.log_module, "Startup", "Diagnostics baseline initialized");
    logger->Info(options.log_module, "Startup", "Worker threads: " + std::to_string(options.worker_count));
    logger->Info(options.log_module, "Startup",
                 std::string("Memory tracking: ") + (options.memory_tracking_enabled ? "enabled" : "disabled"));
    return logger;
}

[[nodiscard]] inline std::shared_ptr<platform::WindowsPlatformRuntime>
RegisterWindowsPlatformServices(core::Application &application)
{
    auto platform_runtime = std::make_shared<platform::WindowsPlatformRuntime>();
    application.Services().RegisterInstance<platform::IPlatformRuntime>(platform_runtime);
    application.Services().RegisterInstance<platform::IWindowSystem>(platform_runtime);
    return platform_runtime;
}

[[nodiscard]] inline std::shared_ptr<input::InputSystem> RegisterInputServices(core::Application &application)
{
    auto input_system = std::make_shared<input::InputSystem>();
    application.Services().RegisterInstance<input::IInputSystem>(input_system);
    return input_system;
}

[[nodiscard]] inline std::shared_ptr<FramePlatformEvents>
RegisterFramePlatformEvents(core::Application &application)
{
    auto frame_platform_events = std::make_shared<FramePlatformEvents>();
    application.Services().RegisterInstance<FramePlatformEvents>(frame_platform_events);
    return frame_platform_events;
}

inline void RegisterPlatformFrameLoop(core::Application &application,
                                      std::shared_ptr<platform::WindowsPlatformRuntime> platform_runtime,
                                      std::shared_ptr<FramePlatformEvents> frame_platform_events,
                                      std::shared_ptr<input::IInputSystem> input_system = {})
{
    application.AddFramePhaseHandler(
        core::FramePhase::PumpPlatformEvents,
        [&application, platform_runtime, frame_platform_events, input_system](const core::FrameContext &) {
            platform_runtime->PumpEvents();
            frame_platform_events->events = platform_runtime->DrainEvents();
            diagnostics::GlobalCounters().Set(diagnostics::CounterId::QueuedEvents,
                                              static_cast<std::int64_t>(frame_platform_events->events.size()));

            if (input_system)
            {
                input_system->QueuePlatformEvents(frame_platform_events->events);
            }

            if (platform_runtime->IsExitRequested())
            {
                application.RequestStop();
            }
        },
        "AppSupport::PumpPlatformEvents");

    if (input_system)
    {
        application.AddFramePhaseHandler(
            core::FramePhase::UpdateInput,
            [input_system](const core::FrameContext &) { input_system->PublishSnapshot(); },
            "AppSupport::UpdateInput");
    }
}

inline void RegisterRhiFrameLoop(core::Application &application,
                                 std::shared_ptr<rhi::IRhiCommandContext> command_context,
                                 std::shared_ptr<rhi::IRhiSwapChain> swap_chain,
                                 std::shared_ptr<bool> render_paused,
                                 std::shared_ptr<rhi::RhiClearDesc> clear_desc)
{
    application.AddFramePhaseHandler(
        core::FramePhase::RhiBeginFrame,
        [command_context, render_paused, clear_desc](const core::FrameContext &) {
            if (*render_paused)
            {
                return;
            }

            const auto begin_result = command_context->BeginFrame();
            if (!begin_result.HasValue())
            {
                throw std::runtime_error(begin_result.GetError().message);
            }

            const auto clear_result = command_context->Clear(*clear_desc);
            if (!clear_result.HasValue())
            {
                throw std::runtime_error(clear_result.GetError().message);
            }
        },
        "AppSupport::RhiBeginFrame");
    application.AddFramePhaseHandler(
        core::FramePhase::RhiEndFrame,
        [command_context](const core::FrameContext &) {
            if (!command_context->IsFrameActive())
            {
                return;
            }

            const auto end_result = command_context->EndFrame();
            if (!end_result.HasValue())
            {
                throw std::runtime_error(end_result.GetError().message);
            }
        },
        "AppSupport::RhiEndFrame");
    application.AddFramePhaseHandler(
        core::FramePhase::Present,
        [command_context, swap_chain, render_paused](const core::FrameContext &) {
            if (*render_paused || command_context->IsFrameActive())
            {
                return;
            }

            const auto present_result = swap_chain->Present();
            if (!present_result.HasValue())
            {
                throw std::runtime_error(present_result.GetError().message);
            }
        },
        "AppSupport::Present");
}

inline void RegisterFrameThrottle(core::Application &application, std::chrono::milliseconds duration)
{
    application.AddFramePhaseHandler(
        core::FramePhase::EndFrame,
        [duration](const core::FrameContext &) { std::this_thread::sleep_for(duration); },
        "AppSupport::Throttle");
}
} // namespace epidemic::apps