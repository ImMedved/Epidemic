#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_command_context.h>
#include <Epidemic/RHI/irhi_device.h>
#include <Epidemic/RHI/irhi_swap_chain.h>
#include <Epidemic/RHI/pixel_format.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

namespace
{
void ThrowIfFailed(const epidemic::foundation::Result<void> &result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }
}
} // namespace

int main()
{
    try
    {
        epidemic::core::Application application;
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(
            application, {.runtime_name = "EpidemicRhiClearScreenApp", .log_module = "RhiClearScreenApp"}));

        static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));
        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("RhiClearScreenApp", "Platform", "Platform runtime: WindowsPlatformRuntime");

        auto graphics_runtime = epidemic::enginebase::RegisterGraphicsRuntime(
            application,
            {.backend = epidemic::enginebase::GraphicsBackend::D3D11,
             .enable_debug_validation = configuration->GetRhiDebugEnabled().value_or(false),
             .debug_name = "RhiClearScreenAppDevice"});
        logger->Info("RhiClearScreenApp", "RHI", "Using D3D11 backend through EngineBase support composition");

        const auto window = epidemic::enginebase::CreateMainWindow(
            application,
            epidemic::platform::WindowCreateInfo{"Epidemic RHI Clear Screen",
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowWidth().value_or(1280)),
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowHeight().value_or(720)),
                                                 true});

        logger->Info("RhiClearScreenApp", "Window",
                     "Window created: " + std::to_string(window->ClientWidth()) + "x" +
                         std::to_string(window->ClientHeight()) + ", dpi=" + std::to_string(window->Dpi()));

        epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
        swap_chain_desc.width = window->ClientWidth();
        swap_chain_desc.height = window->ClientHeight();
        swap_chain_desc.buffer_count = 2;
        swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
        swap_chain_desc.vsync = true;
        swap_chain_desc.debug_name = "RhiClearScreenAppSwapChain";
        const auto swap_chain = epidemic::enginebase::RegisterMainSwapChain(application, window, swap_chain_desc);

        epidemic::enginebase::RegisterPlatformFrameLoop(application);
        epidemic::enginebase::RegisterFrameThrottle(application, std::chrono::milliseconds(16));
        const auto frame_platform_events = application.Services().Get<epidemic::enginebase::FramePlatformEvents>();

        logger->Info("RhiClearScreenApp", "Window", "Close the window to exit");

        auto render_paused = std::make_shared<bool>(window->IsMinimized());
        auto clear_desc = std::make_shared<epidemic::rhi::RhiClearDesc>();
        clear_desc->color = epidemic::rhi::RhiColor{0.08f, 0.17f, 0.33f, 1.0f};
        epidemic::enginebase::RegisterRhiFrameLoop(application, graphics_runtime.command_context, swap_chain,
                                                   render_paused, clear_desc);

        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::PumpPlatformEvents,
            [&application, frame_platform_events, logger, window, swap_chain, render_paused](const epidemic::core::FrameContext &) {
                for (const auto &event : frame_platform_events->events)
                {
                    switch (event.type)
                    {
                    case epidemic::platform::PlatformEventType::WindowCloseRequested:
                        if (event.window_id == window->Id())
                        {
                            logger->Info("RhiClearScreenApp", "Window", "Close requested");
                            *render_paused = true;
                            window->Close();
                            application.RequestStop();
                        }
                        break;
                    case epidemic::platform::PlatformEventType::WindowMinimized:
                        if (event.window_id == window->Id())
                        {
                            logger->Info("RhiClearScreenApp", "Window", "Window minimized, rendering paused");
                            *render_paused = true;
                        }
                        break;
                    case epidemic::platform::PlatformEventType::WindowRestored:
                        if (event.window_id == window->Id())
                        {
                            logger->Info("RhiClearScreenApp", "Window", "Window restored");
                            *render_paused = false;
                        }
                        break;
                    case epidemic::platform::PlatformEventType::WindowResized:
                        if (event.window_id == window->Id() && event.client_width > 0 && event.client_height > 0)
                        {
                            logger->Info("RhiClearScreenApp", "Window",
                                         "Resize: " + std::to_string(event.client_width) + "x" +
                                             std::to_string(event.client_height));
                            ThrowIfFailed(swap_chain->Resize(event.client_width, event.client_height));
                            *render_paused = false;
                        }
                        break;
                    case epidemic::platform::PlatformEventType::None:
                    case epidemic::platform::PlatformEventType::WindowFocusChanged:
                    case epidemic::platform::PlatformEventType::KeyPressed:
                    case epidemic::platform::PlatformEventType::KeyReleased:
                    case epidemic::platform::PlatformEventType::MouseMoved:
                    case epidemic::platform::PlatformEventType::MouseButtonPressed:
                    case epidemic::platform::PlatformEventType::MouseButtonReleased:
                    case epidemic::platform::PlatformEventType::MouseWheel:
                    case epidemic::platform::PlatformEventType::MouseCaptureChanged:
                        break;
                    }
                }
            },
            "RhiClearScreenApp::InspectPlatformEvents");

        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "EpidemicRhiClearScreenApp failed: " << exception.what() << '\n';
        return 1;
    }
}