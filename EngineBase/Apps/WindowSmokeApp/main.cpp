#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Platform/platform_event.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace
{
template <typename TValue>
TValue RequireValue(epidemic::foundation::Result<TValue> result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }

    return std::move(result).Value();
}
} // namespace

int main()
{
    try
    {
        epidemic::core::Application application;
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(
            application, {.runtime_name = "EpidemicWindowSmokeApp", .log_module = "WindowSmokeApp"}));

        static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));
        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("WindowSmokeApp", "Platform", "Platform runtime: WindowsPlatformRuntime");

        const auto window = RequireValue(epidemic::enginebase::CreateMainWindow(
            application,
            epidemic::platform::WindowCreateInfo{"Epidemic Window Smoke",
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowWidth().value_or(1280)),
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowHeight().value_or(720)),
                                                 true}));

        epidemic::enginebase::RegisterPlatformFrameLoop(application);
        epidemic::enginebase::RegisterFrameThrottle(application, std::chrono::milliseconds(16));
        const auto frame_platform_events = application.Services().Get<epidemic::enginebase::FramePlatformEvents>();

        logger->Info("WindowSmokeApp", "Window",
                     "Window created: " + std::to_string(window->ClientWidth()) + "x" +
                         std::to_string(window->ClientHeight()) + ", dpi=" + std::to_string(window->Dpi()));
        logger->Info("WindowSmokeApp", "Window", "Close the window to exit");

        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::PumpPlatformEvents,
            [&application, frame_platform_events, logger, window](const epidemic::core::FrameContext &) {
                for (const auto &event : frame_platform_events->events)
                {
                    switch (event.type)
                    {
                    case epidemic::platform::PlatformEventType::WindowCloseRequested:
                        if (event.window_id == window->Id())
                        {
                            logger->Info("WindowSmokeApp", "Window", "Close requested");
                            window->Close();
                            application.RequestStop();
                        }
                        break;
                    case epidemic::platform::PlatformEventType::WindowResized:
                        logger->Info("WindowSmokeApp", "Window",
                                     "Resized to " + std::to_string(event.client_width) + "x" +
                                         std::to_string(event.client_height));
                        break;
                    case epidemic::platform::PlatformEventType::WindowFocusChanged:
                        logger->Info("WindowSmokeApp", "Window",
                                     std::string("Focus changed: ") + (event.focused ? "focused" : "unfocused"));
                        break;
                    case epidemic::platform::PlatformEventType::WindowMinimized:
                        logger->Info("WindowSmokeApp", "Window", "Window minimized");
                        break;
                    case epidemic::platform::PlatformEventType::WindowRestored:
                        logger->Info("WindowSmokeApp", "Window", "Window restored");
                        break;
                    case epidemic::platform::PlatformEventType::None:
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
            "WindowSmokeApp::InspectPlatformEvents");

        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "EpidemicWindowSmokeApp failed: " << exception.what() << '\n';
        return 1;
    }
}