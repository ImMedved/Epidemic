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

struct FramePlatformEvents
{
    std::vector<platform::PlatformEvent> events;
};

[[nodiscard]] std::shared_ptr<FramePlatformEvents> EnsureFramePlatformEvents(core::Application &application);
[[nodiscard]] std::shared_ptr<diagnostics::ILogger> RegisterEngineBase(core::Application &application,
                                                                       EngineBaseOptions options);
[[nodiscard]] std::shared_ptr<platform::IPlatformRuntime> RegisterWindowsRuntime(core::Application &application);
[[nodiscard]] std::shared_ptr<input::IInputSystem> RegisterInputRuntime(core::Application &application);
[[nodiscard]] foundation::Result<GraphicsRuntimeServices> RegisterGraphicsRuntime(core::Application &application,
                                                                                  GraphicsRuntimeOptions options);
[[nodiscard]] foundation::Result<std::shared_ptr<platform::IWindow>>
CreateMainWindow(core::Application &application, const platform::WindowCreateInfo &create_info);
[[nodiscard]] foundation::Result<std::shared_ptr<rhi::IRhiSwapChain>>
RegisterMainSwapChain(core::Application &application,
                      const std::shared_ptr<platform::IWindow> &window,
                      const rhi::RhiSwapChainDesc &desc);
void RegisterPlatformFrameLoop(core::Application &application);
void RegisterInputFrameLoop(core::Application &application);
void RegisterRhiFrameLoop(core::Application &application,
                          const std::shared_ptr<rhi::IRhiCommandContext> &command_context,
                          const std::shared_ptr<rhi::IRhiSwapChain> &swap_chain,
                          const std::shared_ptr<bool> &render_paused,
                          const std::shared_ptr<rhi::RhiClearDesc> &clear_desc);
void RegisterFrameThrottle(core::Application &application, std::chrono::milliseconds duration);
} // namespace epidemic::enginebase