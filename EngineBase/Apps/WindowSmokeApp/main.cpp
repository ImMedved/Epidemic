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
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow_system.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>

namespace
{
void RegisterCoreServices(epidemic::core::Application &application, std::string runtime_name)
{
    epidemic::diagnostics::SetCurrentThreadName("Main");
    application.Services().Emplace<epidemic::diagnostics::ILogger, epidemic::diagnostics::ConsoleLogger>();
    const auto configuration =
        application.Services().Emplace<epidemic::core::config::IConfiguration, epidemic::core::config::BasicConfiguration>();
    configuration->SetRuntimeName(std::move(runtime_name));
    configuration->SetWorkerCount(1);
    configuration->SetMemoryTrackingEnabled(true);
    configuration->SetRhiDebugEnabled(false);
    configuration->SetDefaultWindowWidth(1280);
    configuration->SetDefaultWindowHeight(720);
    application.Services().Emplace<epidemic::core::events::IEventBus, epidemic::core::events::EventBus>();
    application.Services().Emplace<epidemic::core::tasks::ITaskScheduler, epidemic::core::tasks::SimpleTaskScheduler>(1);

    epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::MemoryUsed, 0);
    const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
    logger->Info("WindowSmokeApp", "Startup", "Diagnostics baseline initialized");
    logger->Info("WindowSmokeApp", "Startup", "Memory tracking: enabled");
}
} // namespace

int main()
{
    try
    {
        epidemic::core::Application application;
        RegisterCoreServices(application, "EpidemicWindowSmokeApp");

        auto platform_runtime = std::make_shared<epidemic::platform::WindowsPlatformRuntime>();
        application.Services().RegisterInstance<epidemic::platform::IPlatformRuntime>(platform_runtime);
        application.Services().RegisterInstance<epidemic::platform::IWindowSystem>(platform_runtime);

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

        application.AddFramePhaseHandler(epidemic::core::FramePhase::PumpPlatformEvents,
                                         [&application, platform_runtime, window, logger](const epidemic::core::FrameContext &) {
                                             platform_runtime->PumpEvents();
                                             for (const auto &event : platform_runtime->DrainEvents())
                                             {
                                                 switch (event.type)
                                                 {
                                                 case epidemic::platform::PlatformEventType::WindowCloseRequested:
                                                     logger->Info("WindowSmokeApp", "Window", "Close requested");
                                                     application.RequestStop();
                                                     break;
                                                 case epidemic::platform::PlatformEventType::WindowResized:
                                                     logger->Info("WindowSmokeApp", "Window",
                                                                  "Resized to " + std::to_string(event.client_width) +
                                                                      "x" + std::to_string(event.client_height));
                                                     break;
                                                 case epidemic::platform::PlatformEventType::WindowFocusChanged:
                                                     logger->Info("WindowSmokeApp", "Window",
                                                                  std::string("Focus changed: ") +
                                                                      (event.focused ? "focused" : "unfocused"));
                                                     break;
                                                 case epidemic::platform::PlatformEventType::WindowMinimized:
                                                     logger->Info("WindowSmokeApp", "Window", "Window minimized");
                                                     break;
                                                 case epidemic::platform::PlatformEventType::WindowRestored:
                                                     logger->Info("WindowSmokeApp", "Window", "Window restored");
                                                     break;
                                                 case epidemic::platform::PlatformEventType::None:
                                                     break;
                                                 }
                                             }

                                             if (platform_runtime->IsExitRequested())
                                             {
                                                 application.RequestStop();
                                             }
                                         },
                                         "WindowSmokeApp::PumpPlatformEvents");
        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::EndFrame,
            [](const epidemic::core::FrameContext &) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); },
            "WindowSmokeApp::Throttle");

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

