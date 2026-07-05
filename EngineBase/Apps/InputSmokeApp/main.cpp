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
#include <Epidemic/Input/input_event.h>
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Input/mouse_button.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow_system.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
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
    logger->Info("InputSmokeApp", "Startup", "Diagnostics baseline initialized");
    logger->Info("InputSmokeApp", "Window", "Window creation info: 1280x720 baseline");
    logger->Info("InputSmokeApp", "Startup", "Memory tracking: enabled");
}

void LogInputEvent(epidemic::diagnostics::ILogger &logger, const epidemic::input::InputEvent &event)
{
    using epidemic::input::InputEventType;

    switch (event.type)
    {
    case InputEventType::KeyPressed:
        logger.Info("InputSmokeApp", "Input",
                    "Key pressed: " + std::string(epidemic::input::ToString(event.key_code)) +
                        (event.repeated ? " (repeat)" : ""));
        break;
    case InputEventType::KeyReleased:
        logger.Info("InputSmokeApp", "Input",
                    "Key released: " + std::string(epidemic::input::ToString(event.key_code)));
        break;
    case InputEventType::MouseMoved:
        logger.Info("InputSmokeApp", "Input",
                    "Mouse moved: x=" + std::to_string(event.mouse_x) + ", y=" + std::to_string(event.mouse_y) +
                        ", dx=" + std::to_string(event.mouse_delta_x) + ", dy=" + std::to_string(event.mouse_delta_y));
        break;
    case InputEventType::MouseButtonPressed:
        logger.Info("InputSmokeApp", "Input",
                    "Mouse button pressed: " + std::string(epidemic::input::ToString(event.mouse_button)));
        break;
    case InputEventType::MouseButtonReleased:
        logger.Info("InputSmokeApp", "Input",
                    "Mouse button released: " + std::string(epidemic::input::ToString(event.mouse_button)));
        break;
    case InputEventType::MouseWheel:
        logger.Info("InputSmokeApp", "Input", "Mouse wheel: " + std::to_string(event.wheel_delta));
        break;
    case InputEventType::FocusChanged:
        logger.Info("InputSmokeApp", "Input", std::string("Focus changed: ") + (event.focused ? "focused" : "unfocused"));
        break;
    case InputEventType::CaptureChanged:
        logger.Info("InputSmokeApp", "Input",
                    std::string("Mouse capture: ") + (event.captured ? "captured" : "released"));
        break;
    case InputEventType::None:
        break;
    }
}
} // namespace

int main()
{
    try
    {
        epidemic::core::Application application;
        RegisterCoreServices(application, "EpidemicInputSmokeApp");

        auto platform_runtime = std::make_shared<epidemic::platform::WindowsPlatformRuntime>();
        auto input_system = std::make_shared<epidemic::input::InputSystem>();
        application.Services().RegisterInstance<epidemic::platform::IPlatformRuntime>(platform_runtime);
        application.Services().RegisterInstance<epidemic::platform::IWindowSystem>(platform_runtime);
        application.Services().RegisterInstance<epidemic::input::IInputSystem>(input_system);

        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("InputSmokeApp", "Platform", "Platform runtime: WindowsPlatformRuntime");
        logger->Info("InputSmokeApp", "Input", "Input snapshot pipeline enabled");

        const auto window_result = platform_runtime->CreateWindow(
            epidemic::platform::WindowCreateInfo{"Epidemic Input Smoke",
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowWidth().value_or(1280)),
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowHeight().value_or(720)),
                                                 true});
        if (!window_result.HasValue())
        {
            throw std::runtime_error(window_result.GetError().message);
        }

        const auto window = window_result.Value();
        logger->Info("InputSmokeApp", "Window",
                     "Window created: " + std::to_string(window->ClientWidth()) + "x" +
                         std::to_string(window->ClientHeight()) + ", dpi=" + std::to_string(window->Dpi()));
        logger->Info("InputSmokeApp", "Window", "Interact with the window. Press Escape or close the window to exit.");

        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::PumpPlatformEvents,
            [&application, platform_runtime, input_system, window, logger](const epidemic::core::FrameContext &) {
                platform_runtime->PumpEvents();
                const auto platform_events = platform_runtime->DrainEvents();
                input_system->QueuePlatformEvents(platform_events);

                for (const auto &event : platform_events)
                {
                    if (event.type == epidemic::platform::PlatformEventType::WindowCloseRequested)
                    {
                        logger->Info("InputSmokeApp", "Window", "Close requested");
                        application.RequestStop();
                    }
                }

                if (platform_runtime->IsExitRequested())
                {
                    application.RequestStop();
                }
            },
            "InputSmokeApp::PumpPlatformEvents");
        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::UpdateInput,
            [&application, input_system, logger](const epidemic::core::FrameContext &) {
                input_system->PublishSnapshot();
                for (const auto &event : input_system->CurrentEvents())
                {
                    LogInputEvent(*logger, event);
                }

                if (input_system->CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::Escape))
                {
                    logger->Info("InputSmokeApp", "Input", "Escape pressed, stopping application");
                    application.RequestStop();
                }
            },
            "InputSmokeApp::UpdateInput");
        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::EndFrame,
            [](const epidemic::core::FrameContext &) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); },
            "InputSmokeApp::Throttle");

        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "EpidemicInputSmokeApp failed: " << exception.what() << '\n';
        return 1;
    }
}

