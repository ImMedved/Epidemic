// This file exercises integration between the Win32 platform runtime and the normalized input pipeline.

#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <atomic>
#include <stdexcept>
#include <thread>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif

namespace
{
using epidemic::tests::Assert;

// Unwraps a Result inside the test and throws with the contained error message on failure.
template <typename TValue>
TValue RequireValue(epidemic::foundation::Result<TValue> result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }

    return std::move(result).Value();
}

// Verifies the real Win32 -> PlatformEvent -> InputSystem path and WM_CLOSE handling.
void TestPlatformAndInputIntegration()
{
    epidemic::core::Application application({"PlatformInputIntegration"});
    static_cast<void>(epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "PlatformInputIntegration", .log_module = "PlatformInputIntegration"}));
    static_cast<void>(epidemic::enginebase::RegisterWindowsRuntime(application));
    auto input_system = epidemic::enginebase::RegisterInputRuntime(application);

    Assert(application.Services().Contains<epidemic::platform::IPlatformRuntime>(),
           "RegisterWindowsRuntime must register IPlatformRuntime");
    Assert(application.Services().Contains<epidemic::platform::IWindowSystem>(),
           "RegisterWindowsRuntime must register IWindowSystem");
    Assert(application.Services().Contains<epidemic::input::IInputSystem>(),
           "RegisterInputRuntime must register IInputSystem");

    const auto runtime = application.Services().Get<epidemic::platform::IPlatformRuntime>();
    const auto window_system = application.Services().Get<epidemic::platform::IWindowSystem>();
    std::atomic_bool wrong_thread_create_rejected{false};
    std::atomic_bool wrong_thread_pump_rejected{false};
    std::thread wrong_thread([&] {
        try
        {
            static_cast<void>(window_system->CreateWindow(
                epidemic::platform::WindowCreateInfo{"Wrong Thread Window", 64, 64, false}));
        }
        catch (const std::runtime_error &)
        {
            wrong_thread_create_rejected.store(true, std::memory_order_release);
        }

        try
        {
            runtime->PumpEvents();
        }
        catch (const std::runtime_error &)
        {
            wrong_thread_pump_rejected.store(true, std::memory_order_release);
        }
    });
    wrong_thread.join();
    Assert(wrong_thread_create_rejected.load(std::memory_order_acquire),
           "Wrong-thread CreateWindow must fail before native window creation");
    Assert(wrong_thread_pump_rejected.load(std::memory_order_acquire),
           "Wrong-thread PumpEvents must fail at the public boundary");
    Assert(window_system->WindowCount() == 0, "Rejected wrong-thread creation must not add a native window");

    const auto window = RequireValue(epidemic::enginebase::CreateMainWindow(
        application, epidemic::platform::WindowCreateInfo{"Hidden Platform/Input Test", 320, 240, false}));
    Assert(window->GetNativeHandle().IsValid(), "Created window must expose a valid native handle");

    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(123, 45));
    SendMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 1);
    runtime->PumpEvents();

    const auto first_events = window_system->DrainEvents();
    bool platform_key_seen = false;
    bool platform_mouse_seen = false;
    for (const auto &event : first_events)
    {
        platform_key_seen |= event.type == epidemic::platform::PlatformEventType::KeyPressed;
        platform_mouse_seen |= event.type == epidemic::platform::PlatformEventType::MouseMoved;
        input_system->QueuePlatformEvent(event);
    }
    input_system->PublishSnapshot();

    Assert(platform_key_seen, "WM_KEYDOWN must be translated into a platform key event");
    Assert(platform_mouse_seen, "WM_MOUSEMOVE must be translated into a platform mouse event");
    Assert(input_system->CurrentSnapshot().HasFocus(), "WM_SETFOCUS must propagate into InputSnapshot");
    Assert(input_system->CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::Escape),
           "WM_KEYDOWN(VK_ESCAPE) must produce an Escape press in InputSnapshot");
    Assert(input_system->CurrentSnapshot().mouse.PositionX() == 123 &&
               input_system->CurrentSnapshot().mouse.PositionY() == 45,
           "WM_MOUSEMOVE coordinates must reach InputSnapshot");

    SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 1);
    runtime->PumpEvents();
    for (const auto &event : window_system->DrainEvents())
    {
        input_system->QueuePlatformEvent(event);
    }
    input_system->PublishSnapshot();
    Assert(input_system->CurrentSnapshot().keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::Escape),
           "WM_KEYUP(VK_ESCAPE) must produce an Escape release in InputSnapshot");

    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    runtime->PumpEvents();
    bool close_event_seen = false;
    for (const auto &event : window_system->DrainEvents())
    {
        if (event.type == epidemic::platform::PlatformEventType::WindowCloseRequested && event.window_id == window->Id())
        {
            close_event_seen = true;
        }
    }
    Assert(close_event_seen, "WM_CLOSE must emit a close-requested platform event");
    window->Close();

    const auto windows_runtime = std::dynamic_pointer_cast<epidemic::platform::WindowsPlatformRuntime>(runtime);
    Assert(windows_runtime != nullptr, "Registered Windows runtime must expose its concrete lifecycle API");
    windows_runtime->Shutdown();
    windows_runtime->Shutdown();
}

}

// Runs the platform/input integration-test group.
int main()
{
    return epidemic::tests::RunNamedTests({{"PlatformAndInputIntegration", &TestPlatformAndInputIntegration}});
}
