#include <Epidemic/Apps/runtime_app_support.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Platform/platform_event.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    try
    {
        epidemic::core::Application application;
        static_cast<void>(epidemic::apps::RegisterCoreRuntimeServices(
            application, {.runtime_name = "EpidemicWindowSmokeApp", .log_module = "WindowSmokeApp"}));

        auto platform_runtime = epidemic::apps::RegisterWindowsPlatformServices(application);
        auto frame_platform_events = epidemic::apps::RegisterFramePlatformEvents(application);
        epidemic::apps::RegisterPlatformFrameLoop(application, platform_runtime, frame_platform_events);
        epidemic::apps::RegisterFrameThrottle(application, std::chrono::milliseconds(16));

        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto width = configuration->GetDefaultWindowWidth().value_or(1280);
        const auto height = configuration->GetDefaultWindowHeight().value_or(720);
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("WindowSmokeApp", "Platform", "Platform runtime: WindowsPlatformRuntime");

        const auto window_result = platform_runtime->CreateWindow(
            epidemic::platform::WindowCreateInfo{"Epidemic Window Smoke", static_cast<std::uint32_t>(width),
                                                 static_cast<std::uint32_t>(height), true});
        if (!window_result.HasValue())
        {
            throw std::runtime_error(window_result.GetError().message);
        }

        const auto window = window_result.Value();
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