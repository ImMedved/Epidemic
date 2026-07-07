#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Platform/platform_event.h>

#include <stdexcept>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif

namespace
{
using epidemic::tests::Assert;

template <typename TValue>
TValue RequireValue(epidemic::foundation::Result<TValue> result)
{
    if (!result.HasValue())
    {
        throw std::runtime_error(result.GetError().message);
    }

    return std::move(result).Value();
}

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

    const auto window = RequireValue(epidemic::enginebase::CreateMainWindow(
        application, epidemic::platform::WindowCreateInfo{"Hidden Platform/Input Test", 320, 240, false}));
    Assert(window->GetNativeHandle().IsValid(), "Created window must expose a valid native handle");

    epidemic::platform::PlatformEvent focus_event;
    focus_event.type = epidemic::platform::PlatformEventType::WindowFocusChanged;
    focus_event.focused = true;

    epidemic::platform::PlatformEvent key_event;
    key_event.type = epidemic::platform::PlatformEventType::KeyPressed;
    key_event.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::Escape);

    input_system->QueuePlatformEvent(focus_event);
    input_system->QueuePlatformEvent(key_event);
    input_system->PublishSnapshot();

    Assert(input_system->CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::Escape),
           "Synthetic input events must update the snapshot");
    Assert(!input_system->CurrentEvents().empty(), "Synthetic input events must populate CurrentEvents");

    SendMessageW(window->GetNativeHandle().As<HWND>(), WM_CLOSE, 0, 0);
    const auto runtime = application.Services().Get<epidemic::platform::IPlatformRuntime>();
    const auto window_system = application.Services().Get<epidemic::platform::IWindowSystem>();
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
}
}

int main()
{
    return epidemic::tests::RunNamedTests({{"PlatformAndInputIntegration", &TestPlatformAndInputIntegration}});
}