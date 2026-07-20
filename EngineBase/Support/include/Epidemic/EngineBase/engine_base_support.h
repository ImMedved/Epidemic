#pragma once

#include <Epidemic/Core/application.h>
#include <Epidemic/EngineBase/graphics_backend.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Input/iinput_system.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow.h>
#include <Epidemic/Platform/iwindow_system.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_device.h>
#include <Epidemic/RHI/irhi_swap_chain.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace epidemic::diagnostics
{
class ILogger;
}

namespace epidemic::enginebase
{
// This file declares the convenience composition helpers used by EngineBase apps and tests.
// The functions wire common baseline services into Application while keeping expected runtime setup
// failures on Result-returning boundaries instead of requiring callers to catch exceptions.

struct EngineBaseOptions
{
    std::string runtime_name{"EpidemicApp"};
    std::string log_module{"App"};
    std::size_t worker_count{1};
    bool memory_tracking_enabled{true};
    bool rhi_debug_enabled{false};
    std::uint32_t default_window_width{1280};
    std::uint32_t default_window_height{720};
};

struct GraphicsRuntimeOptions
{
    GraphicsBackend backend{GraphicsBackend::Null};
    bool enable_debug_validation{false};
    std::string debug_name{"EngineBaseGraphicsDevice"};
};

struct GraphicsRuntimeServices
{
    std::shared_ptr<rhi::IRhiDevice> device;
    std::shared_ptr<rhi::IRhiCommandContext> command_context;
};

// Shared per-frame storage for platform events between the platform and input frame phases.
struct FramePlatformEvents
{
    std::vector<platform::PlatformEvent> events;
};

// Returns the shared per-frame platform-event buffer, creating and registering it if needed.
[[nodiscard]] std::shared_ptr<FramePlatformEvents> EnsureFramePlatformEvents(core::Application &application);

// Registers the baseline diagnostics, configuration, scheduler, dispatcher, event bus, and memory services.
[[nodiscard]] std::shared_ptr<diagnostics::ILogger> RegisterEngineBase(core::Application &application,
                                                                       EngineBaseOptions options);

// Registers the Windows platform runtime as both IPlatformRuntime and IWindowSystem.
[[nodiscard]] std::shared_ptr<platform::IPlatformRuntime> RegisterWindowsRuntime(core::Application &application);

// Registers the baseline normalized input system.
[[nodiscard]] std::shared_ptr<input::IInputSystem> RegisterInputRuntime(core::Application &application);

// Creates and registers the requested graphics runtime services.
[[nodiscard]] foundation::Result<GraphicsRuntimeServices> RegisterGraphicsRuntime(core::Application &application,
                                                                                  GraphicsRuntimeOptions options);

// Creates the main window through the registered window system.
[[nodiscard]] foundation::Result<std::shared_ptr<platform::IWindow>>
CreateMainWindow(core::Application &application, const platform::WindowCreateInfo &create_info);

// Creates and registers the main swap chain for a window using the registered RHI device.
[[nodiscard]] foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>
RegisterMainSwapChain(core::Application &application,
                      const std::shared_ptr<platform::IWindow> &window,
                      const rhi::RhiSwapChainDesc &desc);

// Adds the frame-phase handler that pumps the platform runtime and drains window-system events.
void RegisterPlatformFrameLoop(core::Application &application);

// Adds the frame-phase handler that converts drained platform events into input snapshots.
void RegisterInputFrameLoop(core::Application &application);

// Adds the frame-phase handlers that run BeginFrame/Clear/EndFrame/Present around the supplied render services.
void RegisterRhiFrameLoop(core::Application &application,
                          const std::shared_ptr<rhi::IRhiCommandContext> &command_context,
                          const std::shared_ptr<rhi::IRhiSwapChain> &swap_chain,
                          const std::shared_ptr<bool> &render_paused,
                          const std::shared_ptr<rhi::RhiClearDesc> &clear_desc);

// Adds a simple end-of-frame sleep phase for deterministic smoke-app pacing.
void RegisterFrameThrottle(core::Application &application, std::chrono::milliseconds duration);
} // namespace epidemic::enginebase