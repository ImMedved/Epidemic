// This file exercises the real Win32 platform runtime and its normalized input boundary.

#include "../test_assert.h"

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Input/input_system.h>
#include <Epidemic/Foundation/path.h>
#include <Epidemic/Input/key_code.h>
#include <Epidemic/Input/keyboard_state.h>
#include <Epidemic/Input/mouse_state.h>
#include <Epidemic/Platform/native_window_handle.h>
#include <Epidemic/Platform/platform_event.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include "Windows/window_id_policy.h"
#include "input_index_policy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#ifdef CreateWindow
#undef CreateWindow
#endif

#if !defined(_MSC_VER)
namespace platform_allocation_fault
{
thread_local bool enabled = false;
thread_local std::size_t allocation_index = 0;
thread_local std::size_t fail_index = std::numeric_limits<std::size_t>::max();

void BeginCount() noexcept
{
    enabled = true;
    allocation_index = 0;
    fail_index = std::numeric_limits<std::size_t>::max();
}

void BeginFailAt(std::size_t index) noexcept
{
    enabled = true;
    allocation_index = 0;
    fail_index = index;
}

[[nodiscard]] std::size_t End() noexcept
{
    const auto count = allocation_index;
    enabled = false;
    allocation_index = 0;
    fail_index = std::numeric_limits<std::size_t>::max();
    return count;
}

[[nodiscard]] bool ShouldFail() noexcept
{
    if (!enabled)
    {
        return false;
    }
    return allocation_index++ == fail_index;
}
} // namespace platform_allocation_fault

void *operator new(std::size_t size)
{
    if (platform_allocation_fault::ShouldFail())
    {
        throw std::bad_alloc{};
    }
    if (auto *memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
#endif

namespace
{
using epidemic::tests::Assert;
using epidemic::platform::PlatformEvent;
using epidemic::platform::PlatformEventType;

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

epidemic::platform::PlatformEvent MakeInputEvent(epidemic::platform::PlatformEventType type)
{
    epidemic::platform::PlatformEvent event;
    event.type = type;
    return event;
}

// Verifies publication boundaries, keyboard transitions, focus reset, and Reset semantics without Win32.
void TestInputKeyboardContracts()
{
    epidemic::input::InputSystem input;

    auto press = MakeInputEvent(epidemic::platform::PlatformEventType::KeyPressed);
    press.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::A);
    input.QueuePlatformEvent(press);

    Assert(input.CurrentSnapshot().update_index == 0, "Queued input must not mutate the published snapshot");
    Assert(!input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Queued key press must remain invisible until PublishSnapshot");

    input.PublishSnapshot();
    Assert(input.CurrentSnapshot().update_index == 0, "First publication must use update index zero");
    Assert(input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Key press must set held state");
    Assert(input.CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A),
           "Key press must set the transient pressed flag");
    Assert(!input.CurrentSnapshot().keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "Key press must not set the released flag");

    input.PublishSnapshot();
    Assert(input.CurrentSnapshot().update_index == 1, "Second publication must advance update index");
    Assert(input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Publish without events must preserve held state");
    Assert(!input.CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A) &&
               !input.CurrentSnapshot().keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "Publish without events must clear transient key transitions");
    Assert(input.CurrentEvents().empty(), "Publish without events must expose an empty event sequence");

    auto release = MakeInputEvent(epidemic::platform::PlatformEventType::KeyReleased);
    release.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::A);
    input.QueuePlatformEvents(std::array<epidemic::platform::PlatformEvent, 3>{press, release, press});
    input.PublishSnapshot();
    const auto events = input.CurrentEvents();
    Assert(events.size() == 3, "Multiple key transitions in one frame must preserve their full sequence");
    Assert(events[0].type == epidemic::input::InputEventType::KeyPressed &&
               events[1].type == epidemic::input::InputEventType::KeyReleased &&
               events[2].type == epidemic::input::InputEventType::KeyPressed,
           "Multiple key transitions must preserve platform order");
    Assert(input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Final held state must match the final transition");
    Assert(input.CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A) &&
               input.CurrentSnapshot().keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::A),
           "A press-release-press sequence must preserve both frame transition flags");

    auto focus_lost = MakeInputEvent(epidemic::platform::PlatformEventType::WindowFocusChanged);
    focus_lost.focused = false;
    input.QueuePlatformEvent(focus_lost);
    input.PublishSnapshot();
    Assert(!input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Focus loss must clear held key state");

    input.Reset();
    Assert(input.CurrentEvents().empty(), "Reset must not manufacture gameplay input events");
    Assert(input.CurrentSnapshot().update_index == 0 &&
               !input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
           "Reset must clear published input state and restart the publication index");
    input.PublishSnapshot();
    Assert(input.CurrentEvents().empty(), "First publish after Reset must not contain false transitions");
}

// Verifies mouse transition ordering, per-event motion, wheel accumulation, and focus-reset behavior.
void TestInputMouseContracts()
{
    epidemic::input::InputSystem input;

    auto move1 = MakeInputEvent(epidemic::platform::PlatformEventType::MouseMoved);
    move1.mouse_x = 10;
    move1.mouse_y = 5;
    auto move2 = MakeInputEvent(epidemic::platform::PlatformEventType::MouseMoved);
    move2.mouse_x = 13;
    move2.mouse_y = 1;
    auto press = MakeInputEvent(epidemic::platform::PlatformEventType::MouseButtonPressed);
    press.mouse_button = static_cast<std::uint8_t>(epidemic::input::MouseButton::Left);
    auto release = MakeInputEvent(epidemic::platform::PlatformEventType::MouseButtonReleased);
    release.mouse_button = static_cast<std::uint8_t>(epidemic::input::MouseButton::Left);
    auto wheel1 = MakeInputEvent(epidemic::platform::PlatformEventType::MouseWheel);
    wheel1.wheel_delta = 120;
    auto wheel2 = MakeInputEvent(epidemic::platform::PlatformEventType::MouseWheel);
    wheel2.wheel_delta = -40;

    const std::array events{move1, move2, press, release, press, wheel1, wheel2};
    input.QueuePlatformEvents(events);
    input.PublishSnapshot();

    const auto published = input.CurrentEvents();
    Assert(published.size() == events.size(), "Mouse motion/button/wheel events must not be lost during publication");
    Assert(published[0].mouse_delta_x == 10 && published[0].mouse_delta_y == 5,
           "First mouse event must report delta from the previous published position");
    Assert(published[1].mouse_delta_x == 3 && published[1].mouse_delta_y == -4,
           "Each mouse event must report its own delta rather than accumulated frame delta");
    Assert(input.CurrentSnapshot().mouse.PositionX() == 13 && input.CurrentSnapshot().mouse.PositionY() == 1,
           "Mouse snapshot must publish the final cursor position");
    Assert(input.CurrentSnapshot().mouse.DeltaX() == 13 && input.CurrentSnapshot().mouse.DeltaY() == 1,
           "Mouse snapshot must accumulate frame motion");
    Assert(input.CurrentSnapshot().mouse.WheelDelta() == 80, "Mouse wheel contributions must accumulate within a frame");
    Assert(input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Final mouse held state must match the final transition");
    Assert(input.CurrentSnapshot().mouse.WasPressedThisFrame(epidemic::input::MouseButton::Left) &&
               input.CurrentSnapshot().mouse.WasReleasedThisFrame(epidemic::input::MouseButton::Left),
           "Press-release-press must preserve both mouse transition flags");

    input.PublishSnapshot();
    Assert(input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Publish without mouse events must preserve held buttons");
    Assert(input.CurrentSnapshot().mouse.DeltaX() == 0 && input.CurrentSnapshot().mouse.DeltaY() == 0 &&
               input.CurrentSnapshot().mouse.WheelDelta() == 0,
           "Publish without mouse events must clear transient motion and wheel state");

    auto focus_lost = MakeInputEvent(epidemic::platform::PlatformEventType::WindowFocusChanged);
    focus_lost.focused = false;
    input.QueuePlatformEvent(focus_lost);
    input.PublishSnapshot();
    Assert(!input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Focus loss must clear held mouse buttons");
}

// Verifies malformed and duplicate raw transitions cannot corrupt normalized state.
void TestInputInvalidAndDuplicateTransitions()
{
    epidemic::input::KeyboardState keyboard;
    keyboard.SetKeyDown(epidemic::input::KeyCode::Unknown, true);
    keyboard.MarkPressed(epidemic::input::KeyCode::Unknown);
    keyboard.MarkReleased(static_cast<epidemic::input::KeyCode>(0xFFFFu));
    Assert(!keyboard.IsKeyDown(epidemic::input::KeyCode::Unknown) &&
               !keyboard.WasPressedThisFrame(epidemic::input::KeyCode::Unknown) &&
               !keyboard.WasReleasedThisFrame(static_cast<epidemic::input::KeyCode>(0xFFFFu)),
           "Invalid keyboard enums must be ignored instead of aliasing a valid storage slot");

    epidemic::input::MouseState mouse;
    mouse.SetButtonDown(epidemic::input::MouseButton::Unknown, true);
    mouse.MarkPressed(epidemic::input::MouseButton::Unknown);
    mouse.MarkReleased(static_cast<epidemic::input::MouseButton>(200u));
    Assert(!mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Invalid mouse enums must not alias the Left button storage slot");

    epidemic::input::InputSystem input;
    auto invalid_key = MakeInputEvent(epidemic::platform::PlatformEventType::KeyPressed);
    invalid_key.key_code = 0x100u;
    auto invalid_button = MakeInputEvent(epidemic::platform::PlatformEventType::MouseButtonPressed);
    invalid_button.mouse_button = 0xFFu;
    input.QueuePlatformEvents(std::array{invalid_key, invalid_button});
    input.PublishSnapshot();
    Assert(input.CurrentEvents().empty(), "Unknown raw key/button values must be rejected without emitted input events");
    Assert(!input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
           "Rejected raw mouse input must not damage normalized button state");

    auto release_key = MakeInputEvent(epidemic::platform::PlatformEventType::KeyReleased);
    release_key.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::B);
    auto release_button = MakeInputEvent(epidemic::platform::PlatformEventType::MouseButtonReleased);
    release_button.mouse_button = static_cast<std::uint8_t>(epidemic::input::MouseButton::Right);
    input.QueuePlatformEvents(std::array{release_key, release_button});
    input.PublishSnapshot();
    Assert(!input.CurrentSnapshot().keyboard.WasReleasedThisFrame(epidemic::input::KeyCode::B),
           "Release of a key that was not held must not create a false transition");
    Assert(!input.CurrentSnapshot().mouse.WasReleasedThisFrame(epidemic::input::MouseButton::Right),
           "Release of a mouse button that was not held must not create a false transition");
}

[[nodiscard]] std::size_t CountEvents(const std::vector<PlatformEvent> &events, PlatformEventType type)
{
    return static_cast<std::size_t>(std::count_if(events.begin(), events.end(),
                                                   [type](const PlatformEvent &event) { return event.type == type; }));
}

[[nodiscard]] std::filesystem::path CurrentExecutableDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    for (;;)
    {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
        {
            throw std::runtime_error("GetModuleFileNameW failed in platform integration test");
        }
        if (length < buffer.size())
        {
            return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
        }
        buffer.resize(buffer.size() * 2, L'\0');
    }
}


// Verifies neutral value wrappers and process metadata exposed by the Platform public API.
void TestNeutralPlatformValueContracts()
{
    epidemic::platform::NativeWindowHandle empty_handle;
    int marker = 0;
    epidemic::platform::NativeWindowHandle wrapped_handle(&marker);
    Assert(!empty_handle.IsValid(), "Default NativeWindowHandle must be invalid");
    Assert(wrapped_handle.IsValid() && wrapped_handle.Value() == &marker && wrapped_handle.As<void *>() == &marker,
           "NativeWindowHandle must preserve the wrapped opaque pointer value");
    Assert(wrapped_handle == epidemic::platform::NativeWindowHandle(&marker),
           "NativeWindowHandle equality must compare wrapped native pointer identity");

    epidemic::platform::WindowsPlatformRuntime runtime;
    Assert(runtime.Name() == "WindowsPlatformRuntime", "Platform runtime name must remain stable for diagnostics");
    const auto &process_info = runtime.GetProcessInfo();
    Assert(!process_info.executable_path.Empty(), "Platform runtime must capture the current executable path");
    Assert(!process_info.working_directory.Empty(), "Platform runtime must capture the current working directory");
    Assert(!process_info.arguments.empty(), "Platform runtime must capture at least the executable command-line argument");
    runtime.Shutdown();
}

// Verifies the private WindowId generator policy at invalid, last-valid, and exhausted boundaries.
void TestWindowIdBoundaryPolicy()
{
    using epidemic::platform::WindowId;
    using epidemic::platform::detail::AdvanceWindowId;
    using epidemic::platform::detail::CanAllocateWindowId;

    const auto exhausted = std::numeric_limits<WindowId>::max();
    const auto last_valid = exhausted - 1;
    Assert(!CanAllocateWindowId(epidemic::platform::kInvalidWindowId), "WindowId zero must remain the invalid sentinel");
    Assert(CanAllocateWindowId(1), "WindowId one must be allocatable on a fresh runtime");
    Assert(CanAllocateWindowId(last_valid), "The last non-sentinel WindowId must remain allocatable");
    Assert(AdvanceWindowId(last_valid) == exhausted, "Last valid WindowId must advance to the exhausted sentinel without wrap");
    Assert(!CanAllocateWindowId(exhausted), "Exhausted WindowId sentinel must reject further allocation before wrap");
}

// Verifies invalid creation, create/destroy bookkeeping, idempotent close, and terminal exit state.
void TestWindowLifecycleAndExitRequest()
{
    epidemic::platform::WindowsPlatformRuntime runtime;

    const auto invalid_width = runtime.CreateWindow({"Invalid Width", 0, 240, false});
    Assert(!invalid_width.HasValue() && invalid_width.GetError().code == "platform.invalid_window_size",
           "CreateWindow must reject zero client width with a controlled error");
    const auto invalid_height = runtime.CreateWindow({"Invalid Height", 320, 0, false});
    Assert(!invalid_height.HasValue() && invalid_height.GetError().code == "platform.invalid_window_size",
           "CreateWindow must reject zero client height with a controlled error");
    Assert(runtime.WindowCount() == 0, "Rejected window creation must not publish a tracked window");

    auto first = RequireValue(runtime.CreateWindow({"Platform Lifecycle A", 320, 240, false}));
    auto second = RequireValue(runtime.CreateWindow({"Platform Lifecycle B", 320, 240, false}));
    Assert(first->Id() != epidemic::platform::kInvalidWindowId && second->Id() != epidemic::platform::kInvalidWindowId &&
               first->Id() != second->Id(),
           "Created windows must receive distinct valid logical ids");
    Assert(first->GetNativeHandle().IsValid() && second->GetNativeHandle().IsValid(),
           "Created windows must expose valid native handles");
    Assert(first->Title() == "Platform Lifecycle A" && first->ClientWidth() == 320 && first->ClientHeight() == 240 &&
               first->Dpi() > 0 && !first->IsMinimized(),
           "Created window queries must expose the requested title, usable dimensions, DPI, and non-minimized state");
    Assert(runtime.DrainEvents().empty(),
           "Window construction must not publish native setup messages before tracking is committed");
    first->Show();
    Assert(first->GetNativeHandle().IsValid(), "Show must preserve a valid tracked native window");
    ShowWindow(first->GetNativeHandle().As<HWND>(), SW_HIDE);
    static_cast<void>(runtime.DrainEvents());
    Assert(runtime.WindowCount() == 2, "Runtime must track both created windows");
    Assert(!runtime.IsExitRequested(), "Runtime must not request exit while live windows remain");

    first->Close();
    first->Close();
    Assert(!first->GetNativeHandle().IsValid(), "Close must invalidate the wrapper native handle");
    Assert(first->IsCloseRequested(), "Close must leave close-request state observable on the wrapper");
    Assert(runtime.WindowCount() == 1, "Closing one of two windows must remove exactly one tracked window");
    Assert(!runtime.IsExitRequested(), "Closing a non-final window must not request process exit");

    second->Close();
    second->Close();
    Assert(!second->GetNativeHandle().IsValid(), "Repeated Close after destruction must remain a no-op");
    Assert(runtime.WindowCount() == 0, "Closing the final window must clear runtime bookkeeping");
    Assert(runtime.IsExitRequested(), "Closing the final window must request process exit");
    Assert(runtime.IsExitRequested(), "Exit request observation must be idempotent and sticky");

    const auto events = runtime.DrainEvents();
    Assert(CountEvents(events, PlatformEventType::WindowCloseRequested) == 2,
           "Each explicit window close must publish exactly one close-request event");
    Assert(runtime.DrainEvents().empty(), "Drained platform events must not be returned again");
    Assert(!runtime.HasPendingEvents(), "Event queue must report empty after drainage");

    const auto create_after_exit = runtime.CreateWindow({"After Exit", 64, 64, false});
    Assert(!create_after_exit.HasValue() && create_after_exit.GetError().code == "platform.exit_requested",
           "Terminal exit state must reject creation of new native windows");

    runtime.Shutdown();
    runtime.Shutdown();

    const auto create_after_shutdown = runtime.CreateWindow({"After Shutdown", 64, 64, false});
    Assert(!create_after_shutdown.HasValue() && create_after_shutdown.GetError().code == "platform.runtime_shutdown",
           "Terminal shutdown must reject new window creation with the shutdown error");
    bool pump_after_shutdown_rejected = false;
    try
    {
        runtime.PumpEvents();
    }
    catch (const std::runtime_error &)
    {
        pump_after_shutdown_rejected = true;
    }
    Assert(pump_after_shutdown_rejected, "PumpEvents must reject work after terminal runtime shutdown");
}

// Verifies a surviving wrapper remains inert and non-dangling after its runtime has completed teardown.
void TestWindowWrapperAfterRuntimeTeardown()
{
    epidemic::platform::WindowPtr survivor;
    {
        auto runtime = std::make_unique<epidemic::platform::WindowsPlatformRuntime>();
        survivor = RequireValue(runtime->CreateWindow({"Surviving Wrapper", 320, 240, false}));
        runtime->Shutdown();
        Assert(!survivor->GetNativeHandle().IsValid(), "Runtime shutdown must invalidate surviving wrapper native handles");
    }

    survivor->Show();
    survivor->Close();
    Assert(!survivor->GetNativeHandle().IsValid() && survivor->IsCloseRequested(),
           "Show/Close on a wrapper whose native window is already destroyed must remain inert after runtime teardown");
}

// Verifies PublishSnapshot preserves the complete previous frame and raw pending queue on allocation failure.
#if !defined(_MSC_VER)
void TestInputPublishAllocationFailureAtomicity()
{
    auto make_fixture = [] {
        epidemic::input::InputSystem input;
        PlatformEvent focus;
        focus.type = PlatformEventType::WindowFocusChanged;
        focus.focused = true;
        input.QueuePlatformEvent(focus);
        input.PublishSnapshot();
        return input;
    };

    PlatformEvent key;
    key.type = PlatformEventType::KeyPressed;
    key.key_code = static_cast<std::uint32_t>(epidemic::input::KeyCode::A);
    PlatformEvent button;
    button.type = PlatformEventType::MouseButtonPressed;
    button.mouse_button = static_cast<std::uint8_t>(epidemic::input::MouseButton::Left);
    button.mouse_x = 17;
    button.mouse_y = 23;

    std::size_t allocation_count = 0;
    epidemic::input::InputSnapshot expected_snapshot;
    std::vector<epidemic::input::InputEvent> expected_events;
    {
        auto input = make_fixture();
        input.QueuePlatformEvent(key);
        input.QueuePlatformEvent(button);
        platform_allocation_fault::BeginCount();
        input.PublishSnapshot();
        allocation_count = platform_allocation_fault::End();
        expected_snapshot = input.CurrentSnapshot();
        expected_events.assign(input.CurrentEvents().begin(), input.CurrentEvents().end());
    }
    Assert(allocation_count > 0, "PublishSnapshot must expose an injectable candidate-event allocation boundary");

    for (std::size_t fail_index = 0; fail_index < allocation_count; ++fail_index)
    {
        auto input = make_fixture();
        const auto before_snapshot = input.CurrentSnapshot();
        const std::vector<epidemic::input::InputEvent> before_events(input.CurrentEvents().begin(),
                                                                     input.CurrentEvents().end());
        input.QueuePlatformEvent(key);
        input.QueuePlatformEvent(button);

        bool failed = false;
        platform_allocation_fault::BeginFailAt(fail_index);
        try
        {
            input.PublishSnapshot();
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        static_cast<void>(platform_allocation_fault::End());
        if (!failed)
        {
            continue;
        }

        Assert(input.CurrentSnapshot().update_index == before_snapshot.update_index,
               "Failed PublishSnapshot must preserve the previous update index");
        Assert(!input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A),
               "Failed PublishSnapshot must not leak candidate keyboard state");
        Assert(!input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left),
               "Failed PublishSnapshot must not leak candidate mouse state");
        Assert(input.CurrentEvents().size() == before_events.size(),
               "Failed PublishSnapshot must preserve the previous frame event list");

        input.PublishSnapshot();
        Assert(input.CurrentSnapshot().update_index == expected_snapshot.update_index &&
                   input.CurrentSnapshot().keyboard.IsKeyDown(epidemic::input::KeyCode::A) &&
                   input.CurrentSnapshot().keyboard.WasPressedThisFrame(epidemic::input::KeyCode::A) &&
                   input.CurrentSnapshot().mouse.IsButtonDown(epidemic::input::MouseButton::Left) &&
                   input.CurrentSnapshot().mouse.WasPressedThisFrame(epidemic::input::MouseButton::Left),
               "Retry after failed PublishSnapshot must consume the preserved raw events exactly once");
        Assert(input.CurrentEvents().size() == expected_events.size(),
               "Retry after failed PublishSnapshot must reproduce the clean event count");
    }
}

void TestWin32CallbackAllocationFailurePreservesState()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Callback Allocation", 320, 240, false}));
    auto *hwnd = static_cast<HWND>(window->GetNativeHandle().Value());
    static_cast<void>(runtime.DrainEvents());

    platform_allocation_fault::BeginCount();
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    const auto allocation_count = platform_allocation_fault::End();
    static_cast<void>(runtime.DrainEvents());
    Assert(allocation_count > 0, "Win32 focus publication must expose an injectable event-queue allocation");

    // Recreate a clean pre-state because the counting pass committed focus.
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    static_cast<void>(runtime.DrainEvents());
    Assert(!window->HasFocus(), "Callback allocation fixture must start unfocused");

    platform_allocation_fault::BeginFailAt(0);
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    static_cast<void>(platform_allocation_fault::End());
    Assert(!window->HasFocus(), "Failed event publication must not commit cached focus state");
    Assert(runtime.DrainEvents().empty(), "Failed callback publication must not append a partial platform event");

    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    Assert(window->HasFocus(), "A later callback must recover after the allocation failure");
    const auto recovered_events = runtime.DrainEvents();
    Assert(std::count_if(recovered_events.begin(), recovered_events.end(), [](const PlatformEvent &event) {
               return event.type == PlatformEventType::WindowFocusChanged && event.focused;
           }) == 1,
           "Recovered focus callback must publish exactly one focus event");

    window->Close();
    runtime.Shutdown();
}
#endif

// Sweeps every C++ allocation boundary in CreateWindow and verifies no partial tracking/id commit survives failure.
#if !defined(_MSC_VER)
void TestWindowCreationAllocationFailureAtomicity()
{
    const epidemic::platform::WindowCreateInfo create_info{"Allocation Sweep Window", 320, 240, false};
    std::size_t allocation_count = 0;
    {
        epidemic::platform::WindowsPlatformRuntime runtime;
        platform_allocation_fault::BeginCount();
        auto result = runtime.CreateWindow(create_info);
        allocation_count = platform_allocation_fault::End();
        auto window = RequireValue(std::move(result));
        Assert(window->Id() == 1, "Fresh runtime must commit the first WindowId only after successful creation");
        window->Close();
        runtime.Shutdown();
    }
    Assert(allocation_count > 0, "CreateWindow allocation sweep must observe at least one fallible C++ allocation");

    for (std::size_t fail_index = 0; fail_index < allocation_count; ++fail_index)
    {
        epidemic::platform::WindowsPlatformRuntime runtime;
        bool bad_alloc_seen = false;
        platform_allocation_fault::BeginFailAt(fail_index);
        try
        {
            static_cast<void>(runtime.CreateWindow(create_info));
        }
        catch (const std::bad_alloc &)
        {
            bad_alloc_seen = true;
        }
        static_cast<void>(platform_allocation_fault::End());

        Assert(bad_alloc_seen, "Each observed CreateWindow allocation boundary must be reproducibly injectable");
        Assert(runtime.WindowCount() == 0, "Failed CreateWindow must leave both runtime window indexes empty");
        Assert(runtime.DrainEvents().empty(), "Failed CreateWindow must not publish construction or close events");

        auto recovered = RequireValue(runtime.CreateWindow(create_info));
        Assert(recovered->Id() == 1, "Failed CreateWindow must not consume the next logical WindowId");
        recovered->Close();
        runtime.Shutdown();
    }
}
#endif

// Verifies that public Input counters and accumulated motion never wrap at numeric boundaries.
void TestInputNumericBoundaries()
{
    const auto maximum_index = std::numeric_limits<std::uint64_t>::max();
    Assert(epidemic::input::detail::AdvanceUpdateIndex(maximum_index - 1) == maximum_index,
           "Input update index must reach the final representable value");
    Assert(epidemic::input::detail::AdvanceUpdateIndex(maximum_index) == maximum_index,
           "Input update index must saturate instead of wrapping to zero");

    epidemic::input::MouseState mouse;
    mouse.AddWheelDelta(std::numeric_limits<std::int32_t>::max());
    mouse.AddWheelDelta(1);
    Assert(mouse.WheelDelta() == std::numeric_limits<std::int32_t>::max(),
           "Positive wheel accumulation must saturate without signed overflow");
    mouse.ClearTransient();
    mouse.AddWheelDelta(std::numeric_limits<std::int32_t>::min());
    mouse.AddWheelDelta(-1);
    Assert(mouse.WheelDelta() == std::numeric_limits<std::int32_t>::min(),
           "Negative wheel accumulation must saturate without signed underflow");

    mouse.ClearTransient();
    mouse.SetPosition(std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
    mouse.SetPosition(std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min());
    Assert(mouse.DeltaX() == std::numeric_limits<std::int32_t>::max() &&
               mouse.DeltaY() == std::numeric_limits<std::int32_t>::min(),
           "Frame mouse deltas must clamp their exact 64-bit accumulation to the public 32-bit range");

    epidemic::input::InputSystem input;
    auto first_move = MakeInputEvent(epidemic::platform::PlatformEventType::MouseMoved);
    first_move.mouse_x = std::numeric_limits<std::int32_t>::min();
    first_move.mouse_y = std::numeric_limits<std::int32_t>::max();
    auto second_move = MakeInputEvent(epidemic::platform::PlatformEventType::MouseMoved);
    second_move.mouse_x = std::numeric_limits<std::int32_t>::max();
    second_move.mouse_y = std::numeric_limits<std::int32_t>::min();
    input.QueuePlatformEvents(std::array{first_move, second_move});
    input.PublishSnapshot();
    const auto events = input.CurrentEvents();
    Assert(events.size() == 2 && events[1].mouse_delta_x == std::numeric_limits<std::int32_t>::max() &&
               events[1].mouse_delta_y == std::numeric_limits<std::int32_t>::min(),
           "Published per-event mouse deltas must saturate without signed overflow");
}

// Verifies native close/destroy reflection and that duplicate WM_CLOSE does not duplicate events.
void TestNativeCloseReflectionAndWrapperLifetime()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Native Close", 320, 240, false}));
    auto ownerless_window = RequireValue(runtime.CreateWindow({"Ownerless Native Destroy", 320, 240, false}));
    const HWND ownerless_hwnd = ownerless_window->GetNativeHandle().As<HWND>();
    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    static_cast<void>(runtime.DrainEvents());

    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    const auto close_events = runtime.DrainEvents();
    Assert(CountEvents(close_events, PlatformEventType::WindowCloseRequested) == 1,
           "Repeated native WM_CLOSE must publish one close-request event");
    Assert(window->IsCloseRequested(), "Native WM_CLOSE must update the wrapper close-request state");
    Assert(window->GetNativeHandle().IsValid(), "WM_CLOSE is a request and must not destroy the native window implicitly");

    Assert(DestroyWindow(hwnd) != FALSE, "Native DestroyWindow must succeed for the integration-test window");
    Assert(!window->GetNativeHandle().IsValid(), "Native destruction must invalidate the wrapper handle immediately");
    Assert(window->IsCloseRequested(), "Native destruction must preserve terminal close-request state");
    Assert(runtime.WindowCount() == 1, "Native destruction must remove exactly the destroyed window bookkeeping");
    Assert(!runtime.IsExitRequested(), "Destroying one of multiple native windows must not request process exit");
    Assert(runtime.DrainEvents().empty(), "Native destruction after WM_CLOSE must not duplicate the close event");

    window->Close();
    ownerless_window.reset();
    Assert(DestroyWindow(ownerless_hwnd) != FALSE,
           "Native destruction must remain safe when the runtime owns the final WindowsWindow shared_ptr");
    const auto ownerless_events = runtime.DrainEvents();
    Assert(CountEvents(ownerless_events, PlatformEventType::WindowCloseRequested) == 1,
           "Native destruction without prior WM_CLOSE must publish one close-request event");
    Assert(runtime.WindowCount() == 0, "Ownerless native destruction must clear final runtime bookkeeping");
    Assert(runtime.IsExitRequested(), "Native destruction of the final window must request exit");
    runtime.Shutdown();
}

// Verifies resize/minimize/restore/zero-area semantics and duplicate suppression.
void TestWindowStateTransitionEvents()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Window State", 320, 240, false}));
    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    static_cast<void>(runtime.DrainEvents());

    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(400, 300));
    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(400, 300));
    SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
    SendMessageW(hwnd, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0));
    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(0, 0));

    const auto before_restore = runtime.DrainEvents();
    Assert(CountEvents(before_restore, PlatformEventType::WindowResized) == 1,
           "Repeated same-size WM_SIZE must not duplicate resize publication");
    Assert(CountEvents(before_restore, PlatformEventType::WindowMinimized) == 1,
           "Repeated minimized WM_SIZE must publish one minimize transition");
    Assert(CountEvents(before_restore, PlatformEventType::WindowRestored) == 0,
           "Zero client area must defer restoration instead of publishing an unusable restore");
    Assert(window->IsMinimized(), "Zero-area restore must keep the wrapper logically minimized until usable dimensions arrive");
    Assert(window->ClientWidth() == 0 && window->ClientHeight() == 0,
           "Zero-area WM_SIZE must update cached dimensions without publishing a drawable resize");

    SendMessageW(hwnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(400, 300));
    const auto restore_events = runtime.DrainEvents();
    Assert(CountEvents(restore_events, PlatformEventType::WindowRestored) == 1,
           "First positive client area after minimize must publish one restore transition");
    Assert(CountEvents(restore_events, PlatformEventType::WindowResized) == 1,
           "Restore to positive dimensions must publish the usable resize once");
    Assert(!window->IsMinimized() && window->ClientWidth() == 400 && window->ClientHeight() == 300,
           "Positive restore must update wrapper state and dimensions");

    window->Close();
    runtime.Shutdown();
}

// Verifies focus transitions are edge-triggered rather than duplicate message-triggered.
void TestFocusEventsAreNotDuplicated()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Focus Events", 320, 240, false}));
    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    static_cast<void>(runtime.DrainEvents());

    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
    SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);

    const auto events = runtime.DrainEvents();
    Assert(CountEvents(events, PlatformEventType::WindowFocusChanged) == 2,
           "Duplicate focus messages must collapse to one gain and one loss transition");
    Assert(!window->HasFocus(), "Final wrapper focus state must match the last transition");

    window->Close();
    runtime.Shutdown();
}

// Verifies mouse capture transitions do not duplicate under ReleaseCapture reentrancy.
void TestMouseCaptureEventsAreNotDuplicated()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Mouse Capture Events", 320, 240, false}));
    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    static_cast<void>(runtime.DrainEvents());

    SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 20));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(10, 20));

    const auto events = runtime.DrainEvents();
    Assert(CountEvents(events, PlatformEventType::MouseButtonPressed) == 1,
           "One native mouse press must publish exactly one normalized press event");
    Assert(CountEvents(events, PlatformEventType::MouseButtonReleased) == 1,
           "One native mouse release must publish exactly one normalized release event");
    Assert(CountEvents(events, PlatformEventType::MouseCaptureChanged) == 2,
           "Capture acquire/release must publish one event per edge without ReleaseCapture reentrant duplication");
    Assert(GetCapture() != hwnd, "Mouse release must leave the test window without native capture");

    window->Close();
    runtime.Shutdown();
}

// Verifies PumpEvents consumes queued Win32 messages once and DrainEvents has no stale replay.
void TestEventPumpDoesNotReplayStaleEvents()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto window = RequireValue(runtime.CreateWindow({"Pump Once", 320, 240, false}));
    const HWND hwnd = window->GetNativeHandle().As<HWND>();
    static_cast<void>(runtime.DrainEvents());

    Assert(PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(123, 45)) != FALSE,
           "PostMessageW must enqueue the integration-test mouse event");
    runtime.PumpEvents();
    const auto first = runtime.DrainEvents();
    Assert(CountEvents(first, PlatformEventType::MouseMoved) == 1,
           "First pump must publish the queued native mouse message exactly once");

    runtime.PumpEvents();
    Assert(runtime.DrainEvents().empty(), "Second pump without new native messages must not replay stale platform events");

    window->Close();
    runtime.Shutdown();
}

// Verifies the platform clock uses the Foundation steady clock contract and never moves backwards.
void TestPlatformClockIsMonotonic()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto previous = runtime.Now();
    for (int index = 0; index < 10000; ++index)
    {
        const auto current = runtime.Now();
        Assert(current >= previous, "Platform high-resolution clock must be monotonic");
        previous = current;
    }
    runtime.Shutdown();
}

// Verifies a fallible external cleanup can be retried without resurrecting tracked Platform state.
void TestShutdownRetryAfterExternalCleanupFailure()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    auto tracked = RequireValue(runtime.CreateWindow({"Tracked Before Shutdown Retry", 320, 240, false}));
    static_cast<void>(runtime.DrainEvents());

    const HWND foreign = CreateWindowExW(0, L"EpidemicEnginePlatformWindow", L"Foreign Class User", WS_OVERLAPPEDWINDOW,
                                          0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Assert(foreign != nullptr, "Test must create an untracked native window that keeps the Platform class busy");

    bool cleanup_failure_seen = false;
    try
    {
        runtime.Shutdown();
    }
    catch (const std::runtime_error &)
    {
        cleanup_failure_seen = true;
    }
    Assert(cleanup_failure_seen, "UnregisterClassW failure must surface as a controlled retryable shutdown failure");
    Assert(runtime.WindowCount() == 0 && !tracked->GetNativeHandle().IsValid(),
           "Failed shutdown after native window cleanup must not resurrect already-destroyed tracked windows");

    Assert(DestroyWindow(foreign) != FALSE, "Removing the external class user must make shutdown retry possible");
    runtime.Shutdown();
    runtime.Shutdown();
}

// Verifies load failure atomicity, controlled missing-symbol failure, and HMODULE ownership lifetime.
void TestDynamicLibraryContracts()
{
    epidemic::platform::WindowsPlatformRuntime runtime;
    const auto test_library_path = CurrentExecutableDirectory() / L"EpidemicPlatformTestLibrary.dll";
    const auto missing_library_path = CurrentExecutableDirectory() / L"EpidemicPlatformDefinitelyMissing.dll";

    Assert(std::filesystem::exists(test_library_path), "Platform test DLL must be built next to the integration executable");
    Assert(GetModuleHandleW(L"EpidemicPlatformTestLibrary.dll") == nullptr,
           "Platform test DLL must not be loaded before the dynamic-library test starts");

    const auto empty = runtime.LoadDynamicLibrary(epidemic::foundation::Path{});
    Assert(!empty.HasValue() && empty.GetError().code == "platform.empty_library_path",
           "Empty dynamic-library path must return a controlled failure");
    const auto relative = runtime.LoadDynamicLibrary(epidemic::foundation::Path(std::filesystem::path(L"relative.dll")));
    Assert(!relative.HasValue() && relative.GetError().code == "platform.relative_library_path",
           "Relative dynamic-library path must return a controlled failure");
    const auto missing = runtime.LoadDynamicLibrary(epidemic::foundation::Path(missing_library_path));
    Assert(!missing.HasValue() && missing.GetError().code == "platform.load_library_failed",
           "Missing dynamic library must return a controlled load failure");
    Assert(GetModuleHandleW(L"EpidemicPlatformDefinitelyMissing.dll") == nullptr,
           "Failed dynamic-library load must not leave a partial native module handle");

#if !defined(_MSC_VER)
    std::size_t load_allocation_count = 0;
    {
        platform_allocation_fault::BeginCount();
        auto observed_load = runtime.LoadDynamicLibrary(epidemic::foundation::Path(test_library_path));
        load_allocation_count = platform_allocation_fault::End();
        auto observed_library = RequireValue(std::move(observed_load));
        observed_library.reset();
        Assert(GetModuleHandleW(L"EpidemicPlatformTestLibrary.dll") == nullptr,
               "Baseline allocation observation must release its dynamic-library wrapper");
    }
    Assert(load_allocation_count > 0, "Dynamic-library load sweep must observe fallible C++ allocation boundaries");

    for (std::size_t fail_index = 0; fail_index < load_allocation_count; ++fail_index)
    {
        bool bad_alloc_seen = false;
        platform_allocation_fault::BeginFailAt(fail_index);
        try
        {
            static_cast<void>(runtime.LoadDynamicLibrary(epidemic::foundation::Path(test_library_path)));
        }
        catch (const std::bad_alloc &)
        {
            bad_alloc_seen = true;
        }
        static_cast<void>(platform_allocation_fault::End());
        Assert(bad_alloc_seen, "Each observed dynamic-library allocation boundary must be reproducibly injectable");
        Assert(GetModuleHandleW(L"EpidemicPlatformTestLibrary.dll") == nullptr,
               "Allocation failure during dynamic-library load must not leak a partial HMODULE");
    }
#endif

    auto library = RequireValue(runtime.LoadDynamicLibrary(epidemic::foundation::Path(test_library_path)));
    Assert(library != nullptr && GetModuleHandleW(L"EpidemicPlatformTestLibrary.dll") != nullptr,
           "Successful load must retain the native module while the wrapper is alive");
    Assert(!library->Name().empty(), "Loaded dynamic library must expose a stable diagnostic name");

    const auto empty_symbol = library->FindSymbol({});
    Assert(!empty_symbol.HasValue() && empty_symbol.GetError().code == "platform.empty_symbol_name",
           "Empty symbol name must return a controlled failure");
    const auto missing_symbol = library->FindSymbol("EpidemicPlatformMissingSymbol");
    Assert(!missing_symbol.HasValue() && missing_symbol.GetError().code == "platform.symbol_not_found",
           "Unknown symbol lookup must return a controlled failure");
    const auto symbol = library->FindSymbol("EpidemicPlatformTestSymbol");
    Assert(symbol.HasValue() && symbol.Value() != nullptr, "Known exported symbol must resolve successfully");

    library.reset();
    Assert(GetModuleHandleW(L"EpidemicPlatformTestLibrary.dll") == nullptr,
           "Destroying the final dynamic-library wrapper must release its HMODULE ownership");
    runtime.Shutdown();
    const auto load_after_shutdown = runtime.LoadDynamicLibrary(epidemic::foundation::Path(test_library_path));
    Assert(!load_after_shutdown.HasValue() && load_after_shutdown.GetError().code == "platform.runtime_shutdown",
           "Dynamic-library loading must reject new work after terminal runtime shutdown");
}

// Verifies the real Win32 -> PlatformEvent -> InputSystem path and wrong-thread guards.
void TestPlatformAndInputIntegration()
{
    epidemic::core::Application application({.application_name = "PlatformInputIntegration", .frame_limit = std::nullopt});
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
    std::atomic_bool wrong_thread_load_rejected{false};
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

        try
        {
            static_cast<void>(runtime->LoadDynamicLibrary(epidemic::foundation::Path{}));
        }
        catch (const std::runtime_error &)
        {
            wrong_thread_load_rejected.store(true, std::memory_order_release);
        }
    });
    wrong_thread.join();
    Assert(wrong_thread_create_rejected.load(std::memory_order_acquire),
           "Wrong-thread CreateWindow must fail before native window creation");
    Assert(wrong_thread_pump_rejected.load(std::memory_order_acquire),
           "Wrong-thread PumpEvents must fail at the public boundary");
    Assert(wrong_thread_load_rejected.load(std::memory_order_acquire),
           "Wrong-thread dynamic-library loading must fail before path or native loader work");
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

} // namespace

// Runs Platform contract and Platform/Input integration tests on real Win32 objects.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"InputKeyboardContracts", &TestInputKeyboardContracts},
        {"InputMouseContracts", &TestInputMouseContracts},
        {"InputInvalidAndDuplicateTransitions", &TestInputInvalidAndDuplicateTransitions},
        {"InputNumericBoundaries", &TestInputNumericBoundaries},
        {"NeutralPlatformValueContracts", &TestNeutralPlatformValueContracts},
        {"WindowIdBoundaryPolicy", &TestWindowIdBoundaryPolicy},
        {"WindowLifecycleAndExitRequest", &TestWindowLifecycleAndExitRequest},
        {"WindowWrapperAfterRuntimeTeardown", &TestWindowWrapperAfterRuntimeTeardown},
#if !defined(_MSC_VER)
        {"InputPublishAllocationFailureAtomicity", &TestInputPublishAllocationFailureAtomicity},
        {"Win32CallbackAllocationFailurePreservesState", &TestWin32CallbackAllocationFailurePreservesState},
        {"WindowCreationAllocationFailureAtomicity", &TestWindowCreationAllocationFailureAtomicity},
#endif
        {"NativeCloseReflectionAndWrapperLifetime", &TestNativeCloseReflectionAndWrapperLifetime},
        {"WindowStateTransitionEvents", &TestWindowStateTransitionEvents},
        {"FocusEventsAreNotDuplicated", &TestFocusEventsAreNotDuplicated},
        {"MouseCaptureEventsAreNotDuplicated", &TestMouseCaptureEventsAreNotDuplicated},
        {"EventPumpDoesNotReplayStaleEvents", &TestEventPumpDoesNotReplayStaleEvents},
        {"PlatformClockIsMonotonic", &TestPlatformClockIsMonotonic},
        {"ShutdownRetryAfterExternalCleanupFailure", &TestShutdownRetryAfterExternalCleanupFailure},
        {"DynamicLibraryContracts", &TestDynamicLibraryContracts},
        {"PlatformAndInputIntegration", &TestPlatformAndInputIntegration},
    });
}
