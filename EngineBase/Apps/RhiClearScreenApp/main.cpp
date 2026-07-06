#include <Epidemic/Apps/runtime_app_support.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/RHI/descriptors.h>
#include <Epidemic/RHI/irhi_device.h>
#include <Epidemic/RHI/irhi_swap_chain.h>
#include <Epidemic/RHI/pixel_format.h>
#include <Epidemic/RHI_D3D11/d3d11_rhi_device.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
template <typename TValue> TValue UnwrapOrThrow(epidemic::foundation::Result<TValue> result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }

    return std::move(result).Value();
}

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
        static_cast<void>(epidemic::apps::RegisterCoreRuntimeServices(
            application, {.runtime_name = "EpidemicRhiClearScreenApp", .log_module = "RhiClearScreenApp"}));

        auto platform_runtime = epidemic::apps::RegisterWindowsPlatformServices(application);
        auto frame_platform_events = epidemic::apps::RegisterFramePlatformEvents(application);
        epidemic::apps::RegisterPlatformFrameLoop(application, platform_runtime, frame_platform_events);
        epidemic::apps::RegisterFrameThrottle(application, std::chrono::milliseconds(16));

        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("RhiClearScreenApp", "Platform", "Platform runtime: WindowsPlatformRuntime");
        logger->Info("RhiClearScreenApp", "RHI", "Using D3D11 backend through the abstract RHI contracts");

        const auto window = UnwrapOrThrow(platform_runtime->CreateWindow(
            epidemic::platform::WindowCreateInfo{"Epidemic RHI Clear Screen",
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowWidth().value_or(1280)),
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowHeight().value_or(720)),
                                                 true}));

        logger->Info("RhiClearScreenApp", "Window",
                     "Window created: " + std::to_string(window->ClientWidth()) + "x" +
                         std::to_string(window->ClientHeight()) + ", dpi=" + std::to_string(window->Dpi()));

        epidemic::rhi::RhiDeviceDesc device_desc;
        device_desc.enable_debug_validation = configuration->GetRhiDebugEnabled().value_or(false);
        device_desc.debug_name = "RhiClearScreenAppDevice";

        const auto rhi_device = UnwrapOrThrow(epidemic::rhi::d3d11::CreateD3D11RhiDevice(device_desc));
        application.Services().RegisterInstance<epidemic::rhi::IRhiDevice>(rhi_device);
        const auto command_context = UnwrapOrThrow(rhi_device->CreateCommandContext());

        epidemic::rhi::RhiSwapChainDesc swap_chain_desc;
        swap_chain_desc.surface_handle = epidemic::rhi::d3d11::CreatePresentationSurfaceHandle(window->GetNativeHandle());
        swap_chain_desc.width = window->ClientWidth();
        swap_chain_desc.height = window->ClientHeight();
        swap_chain_desc.buffer_count = 2;
        swap_chain_desc.color_format = epidemic::rhi::RhiPixelFormat::B8G8R8A8_UNorm;
        swap_chain_desc.vsync = true;
        swap_chain_desc.debug_name = "RhiClearScreenAppSwapChain";

        const auto swap_chain = UnwrapOrThrow(rhi_device->CreateSwapChain(swap_chain_desc));
        application.Services().RegisterInstance<epidemic::rhi::IRhiSwapChain>(swap_chain);
        logger->Info("RhiClearScreenApp", "RHI",
                     "Swap chain ready: " + std::to_string(swap_chain->Width()) + "x" +
                         std::to_string(swap_chain->Height()) + ", backend=" + std::string(rhi_device->BackendName()));
        logger->Info("RhiClearScreenApp", "Window", "Close the window to exit");

        auto render_paused = std::make_shared<bool>(window->IsMinimized());
        auto clear_desc = std::make_shared<epidemic::rhi::RhiClearDesc>();
        clear_desc->color = epidemic::rhi::RhiColor{0.08f, 0.17f, 0.33f, 1.0f};
        epidemic::apps::RegisterRhiFrameLoop(application, command_context, swap_chain, render_paused, clear_desc);

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