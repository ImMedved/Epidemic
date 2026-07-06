#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Core/application.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Input/iinput_system.h>
#include <Epidemic/Input/input_event.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Input/mouse_button.h>
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
                        ", dx=" + std::to_string(event.mouse_delta_x) + ", dy=" +
                        std::to_string(event.mouse_delta_y));
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
        logger.Info("InputSmokeApp", "Input",
                    std::string("Focus changed: ") + (event.focused ? "focused" : "unfocused"));
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
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(
            application, {.runtime_name = "EpidemicInputSmokeApp", .log_module = "InputSmokeApp"}));

        static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));
        auto input_system = epidemic::enginebase::RegisterInputRuntime(application);
        const auto configuration = application.Services().Get<epidemic::core::config::IConfiguration>();
        const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
        logger->Info("InputSmokeApp", "Platform", "Platform runtime: WindowsPlatformRuntime");
        logger->Info("InputSmokeApp", "Input", "Input snapshot pipeline enabled");

        const auto window = RequireValue(epidemic::enginebase::CreateMainWindow(
            application,
            epidemic::platform::WindowCreateInfo{"Epidemic Input Smoke",
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowWidth().value_or(1280)),
                                                 static_cast<std::uint32_t>(configuration->GetDefaultWindowHeight().value_or(720)),
                                                 true}));

        epidemic::enginebase::RegisterPlatformFrameLoop(application);
        epidemic::enginebase::RegisterInputFrameLoop(application);
        epidemic::enginebase::RegisterFrameThrottle(application, std::chrono::milliseconds(16));
        const auto frame_platform_events = application.Services().Get<epidemic::enginebase::FramePlatformEvents>();

        logger->Info("InputSmokeApp", "Window",
                     "Window created: " + std::to_string(window->ClientWidth()) + "x" +
                         std::to_string(window->ClientHeight()) + ", dpi=" + std::to_string(window->Dpi()));
        logger->Info("InputSmokeApp", "Window", "Interact with the window. Press Escape or close the window to exit.");

        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::PumpPlatformEvents,
            [&application, frame_platform_events, logger, window](const epidemic::core::FrameContext &) {
                for (const auto &event : frame_platform_events->events)
                {
                    if (event.type == epidemic::platform::PlatformEventType::WindowCloseRequested &&
                        event.window_id == window->Id())
                    {
                        logger->Info("InputSmokeApp", "Window", "Close requested");
                        window->Close();
                        application.RequestStop();
                    }
                }
            },
            "InputSmokeApp::HandleClose");

        application.AddFramePhaseHandler(
            epidemic::core::FramePhase::UpdateInput,
            [&application, input_system, logger](const epidemic::core::FrameContext &) {
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
            "InputSmokeApp::InspectInputSnapshot");

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