#include <Epidemic/Platform/windows_platform_runtime.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#ifdef CreateWindow
#undef CreateWindow
#endif

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace epidemic::platform
{
// This file implements the Win32 platform runtime and window system.
// Public contracts live in the Platform headers; the helpers and private classes below translate those contracts into Win32 behavior.

namespace
{
constexpr std::uint8_t kMouseButtonLeft = 0;
constexpr std::uint8_t kMouseButtonRight = 1;
constexpr std::uint8_t kMouseButtonMiddle = 2;
constexpr std::uint8_t kMouseButtonX1 = 3;
constexpr std::uint8_t kMouseButtonX2 = 4;

// Extracts the signed client-space X coordinate from a Win32 LPARAM.
[[nodiscard]] constexpr std::int32_t ExtractMouseX(LPARAM lparam) noexcept
{
    return static_cast<std::int32_t>(static_cast<short>(LOWORD(static_cast<DWORD_PTR>(lparam))));
}

// Extracts the signed client-space Y coordinate from a Win32 LPARAM.
[[nodiscard]] constexpr std::int32_t ExtractMouseY(LPARAM lparam) noexcept
{
    return static_cast<std::int32_t>(static_cast<short>(HIWORD(static_cast<DWORD_PTR>(lparam))));
}

// Returns the current process executable path using a growable Win32 buffer.
[[nodiscard]] std::filesystem::path GetExecutablePath()
{
    std::vector<wchar_t> executable_buffer(MAX_PATH, L'\0');

    while (true)
    {
        const auto executable_length =
            GetModuleFileNameW(nullptr, executable_buffer.data(), static_cast<DWORD>(executable_buffer.size()));
        if (executable_length == 0)
        {
            return {};
        }

        if (executable_length < executable_buffer.size())
        {
            return std::filesystem::path(std::wstring(executable_buffer.data(), executable_length)).lexically_normal();
        }

        executable_buffer.resize(executable_buffer.size() * 2, L'\0');
    }
}

// Converts UTF-8 text to UTF-16 for Win32 API calls.
[[nodiscard]] std::wstring WidenUtf8String(std::string_view utf8_string)
{
    if (utf8_string.empty())
    {
        return {};
    }

    const auto buffer_size =
        MultiByteToWideChar(CP_UTF8, 0, utf8_string.data(), static_cast<int>(utf8_string.size()), nullptr, 0);
    if (buffer_size <= 0)
    {
        return {};
    }

    std::wstring wide_string(static_cast<std::size_t>(buffer_size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_string.data(), static_cast<int>(utf8_string.size()), wide_string.data(),
                        buffer_size);
    return wide_string;
}

// Converts UTF-16 text from Win32 APIs into UTF-8 for EngineBase logs and errors.
[[nodiscard]] std::string NarrowWideString(const std::wstring_view wide_string)
{
    if (wide_string.empty())
    {
        return {};
    }

    const auto buffer_size =
        WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), nullptr, 0, nullptr,
                            nullptr);
    if (buffer_size <= 0)
    {
        return {};
    }

    std::string utf8_string(static_cast<std::size_t>(buffer_size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_string.data(), static_cast<int>(wide_string.size()), utf8_string.data(),
                        buffer_size, nullptr, nullptr);
    return utf8_string;
}

// Formats a Win32 error code into a stable diagnostic message.
[[nodiscard]] std::string FormatWindowsErrorMessage(DWORD error_code)
{
    if (error_code == 0)
    {
        return "code=0";
    }

    std::wstring message_buffer(512, L'\0');
    const auto message_length =
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error_code, 0,
                       message_buffer.data(), static_cast<DWORD>(message_buffer.size()), nullptr);

    std::ostringstream stream;
    stream << "code=" << error_code;
    if (message_length > 0)
    {
        std::wstring_view windows_message(message_buffer.data(), message_length);
        while (!windows_message.empty() &&
               (windows_message.back() == L'\r' || windows_message.back() == L'\n' || windows_message.back() == L' '))
        {
            windows_message.remove_suffix(1);
        }

        const auto utf8_message = NarrowWideString(windows_message);
        if (!utf8_message.empty())
        {
            stream << ", message=" << utf8_message;
        }
    }

    return stream.str();
}

// Builds a PlatformEvent for window-lifecycle notifications.
[[nodiscard]] PlatformEvent MakeWindowEvent(PlatformEventType type, WindowId window_id, std::uint32_t client_width = 0,
                                            std::uint32_t client_height = 0, bool focused = false) noexcept
{
    PlatformEvent event;
    event.type = type;
    event.window_id = window_id;
    event.client_width = client_width;
    event.client_height = client_height;
    event.focused = focused;
    return event;
}

// Builds a PlatformEvent for keyboard input notifications.
[[nodiscard]] PlatformEvent MakeKeyEvent(PlatformEventType type, WindowId window_id, std::uint32_t key_code,
                                         std::uint32_t scan_code, bool repeated) noexcept
{
    PlatformEvent event;
    event.type = type;
    event.window_id = window_id;
    event.key_code = key_code;
    event.scan_code = scan_code;
    event.repeated = repeated;
    return event;
}

// Builds a PlatformEvent for mouse-move notifications.
[[nodiscard]] PlatformEvent MakeMouseMoveEvent(WindowId window_id, std::int32_t x, std::int32_t y) noexcept
{
    PlatformEvent event;
    event.type = PlatformEventType::MouseMoved;
    event.window_id = window_id;
    event.mouse_x = x;
    event.mouse_y = y;
    return event;
}

// Builds a PlatformEvent for mouse-button notifications.
[[nodiscard]] PlatformEvent MakeMouseButtonEvent(PlatformEventType type, WindowId window_id, std::uint8_t button,
                                                 std::int32_t x, std::int32_t y) noexcept
{
    PlatformEvent event;
    event.type = type;
    event.window_id = window_id;
    event.mouse_button = button;
    event.mouse_x = x;
    event.mouse_y = y;
    return event;
}

// Builds a PlatformEvent for mouse-wheel notifications.
[[nodiscard]] PlatformEvent MakeMouseWheelEvent(WindowId window_id, std::int32_t x, std::int32_t y,
                                                std::int32_t wheel_delta) noexcept
{
    PlatformEvent event;
    event.type = PlatformEventType::MouseWheel;
    event.window_id = window_id;
    event.mouse_x = x;
    event.mouse_y = y;
    event.wheel_delta = wheel_delta;
    return event;
}

// Builds a PlatformEvent for mouse-capture state changes.
[[nodiscard]] PlatformEvent MakeCaptureChangedEvent(WindowId window_id, bool captured) noexcept
{
    PlatformEvent event;
    event.type = PlatformEventType::MouseCaptureChanged;
    event.window_id = window_id;
    event.captured = captured;
    return event;
}
// Captures immutable process information exposed through IPlatformRuntime.
[[nodiscard]] ProcessInfo BuildProcessInfo()
{
    ProcessInfo process_info;

    const auto executable_path = GetExecutablePath();
    if (!executable_path.empty())
    {
        process_info.executable_path = epidemic::foundation::Path(executable_path);
    }

    process_info.working_directory = epidemic::foundation::Path(std::filesystem::current_path().lexically_normal());

    int argc = 0;
    if (auto *argv = CommandLineToArgvW(GetCommandLineW(), &argc))
    {
        process_info.arguments.reserve(static_cast<std::size_t>(argc));
        for (int index = 0; index < argc; ++index)
        {
            process_info.arguments.push_back(NarrowWideString(argv[index]));
        }
        LocalFree(argv);
    }

    return process_info;
}

// Win32-backed implementation of the dynamic-library abstraction returned by the runtime.
class WindowsDynamicLibrary final : public IDynamicLibrary
{
  public:
    // Stores the resolved module handle and diagnostic name.
        WindowsDynamicLibrary(std::string name, HMODULE module_handle)
        : name_(std::move(name)), module_handle_(module_handle)
    {
    }

    // Releases the loaded module when the wrapper is destroyed.
        ~WindowsDynamicLibrary() override
    {
        if (module_handle_ != nullptr)
        {
            FreeLibrary(module_handle_);
        }
    }

    // Returns the diagnostic name associated with the loaded module.
        [[nodiscard]] std::string_view Name() const override
    {
        return name_;
    }

    // Resolves one exported symbol from the loaded module.
        [[nodiscard]] epidemic::foundation::Result<void *> FindSymbol(std::string_view symbol_name) const override
    {
        SetLastError(ERROR_SUCCESS);
        const auto *symbol = GetProcAddress(module_handle_, std::string(symbol_name).c_str());
        if (symbol == nullptr)
        {
            const auto error_code = GetLastError();
            return epidemic::foundation::Result<void *>::Failure(
                epidemic::foundation::Error::Create(
                    "platform.symbol_not_found",
                    "Failed to resolve symbol '" + std::string(symbol_name) + "' in library '" + name_ + "' (" +
                        FormatWindowsErrorMessage(error_code) + ")"));
        }

        return epidemic::foundation::Result<void *>::Success(reinterpret_cast<void *>(symbol));
    }

  private:
    std::string name_;
    HMODULE module_handle_{nullptr};
};
} // namespace

struct WindowsPlatformRuntime::Impl
{
    struct RuntimeToken
    {
        Impl *owner{nullptr};
    };

    Impl()
    {
        runtime_token->owner = this;
    }

    // Concrete Win32 window implementation tracked by the runtime.
    class WindowsWindow final : public IWindow
    {
      public:
                // Captures the owning runtime, logical id, and title before native creation is attached.
        WindowsWindow(std::weak_ptr<RuntimeToken> owner, WindowId id, std::string title)
            : owner_(std::move(owner)), id_(id), title_(std::move(title))
        {
        }

                // Destroys the native window if it still exists.
        ~WindowsWindow() override
        {
            if (hwnd_ != nullptr && IsWindow(hwnd_))
            {
                DestroyWindow(hwnd_);
            }
        }

        [[nodiscard]] WindowId Id() const noexcept override
        {
            return id_;
        }

        [[nodiscard]] std::string_view Title() const noexcept override
        {
            return title_;
        }

        [[nodiscard]] NativeWindowHandle GetNativeHandle() const noexcept override
        {
            return NativeWindowHandle(reinterpret_cast<void *>(hwnd_));
        }

        [[nodiscard]] std::uint32_t ClientWidth() const noexcept override
        {
            return client_width_;
        }

        [[nodiscard]] std::uint32_t ClientHeight() const noexcept override
        {
            return client_height_;
        }

        [[nodiscard]] std::uint32_t Dpi() const noexcept override
        {
            return dpi_;
        }

        [[nodiscard]] bool HasFocus() const noexcept override
        {
            return focused_;
        }

        [[nodiscard]] bool IsMinimized() const noexcept override
        {
            return minimized_;
        }

        [[nodiscard]] bool IsCloseRequested() const noexcept override
        {
            return close_requested_;
        }

                // Shows the native window on the owning main thread.
        void Show() override
        {
            auto &owner = OwnerOrThrow("IWindow::Show");
            owner.EnsureMainThread("IWindow::Show");
            if (hwnd_ != nullptr)
            {
                ShowWindow(hwnd_, SW_SHOWNORMAL);
                UpdateWindow(hwnd_);
            }
        }

                // Requests close, emits the close-requested event once, and destroys the native window.
        void Close() override
        {
            auto &owner = OwnerOrThrow("IWindow::Close");
            owner.EnsureMainThread("IWindow::Close");
            CloseNativeWindowNoThrow(&owner, true);
        }

                // Attaches the newly created HWND and synchronizes cached metrics and focus/capture state.
        void Attach(HWND hwnd)
        {
            auto &owner = OwnerOrThrow("WindowsWindow::Attach");
            owner.EnsureMainThread("WindowsWindow::Attach");
            hwnd_ = hwnd;
            UpdateClientMetrics();
            dpi_ = GetDpiForWindow(hwnd_);
            focused_ = (GetFocus() == hwnd_);
            mouse_captured_ = (GetCapture() == hwnd_);
        }

                // Adjusts the outer window size so the client area matches the requested dimensions.
        void EnsureClientSize(std::uint32_t target_width, std::uint32_t target_height)
        {
            auto &owner = OwnerOrThrow("WindowsWindow::EnsureClientSize");
            owner.EnsureMainThread("WindowsWindow::EnsureClientSize");
            if (hwnd_ == nullptr)
            {
                return;
            }

            UpdateClientMetrics();
            if (client_width_ == target_width && client_height_ == target_height)
            {
                return;
            }

            RECT window_rect{};
            if (!GetWindowRect(hwnd_, &window_rect))
            {
                return;
            }

            const auto current_window_width = window_rect.right - window_rect.left;
            const auto current_window_height = window_rect.bottom - window_rect.top;
            const auto width_delta = static_cast<int>(target_width) - static_cast<int>(client_width_);
            const auto height_delta = static_cast<int>(target_height) - static_cast<int>(client_height_);
            SetWindowPos(hwnd_, nullptr, 0, 0, current_window_width + width_delta, current_window_height + height_delta,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateClientMetrics();
        }

                // Translates one Win32 window message into state updates and EngineBase PlatformEvent records.
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
        {
            auto *owner = TryOwner();
            if (owner == nullptr)
            {
                return DefWindowProcW(hwnd_, message, wparam, lparam);
            }

            switch (message)
            {
            case WM_CLOSE:
                if (!close_requested_)
                {
                    close_requested_ = true;
                    owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowCloseRequested, id_));
                }
                return 0;
            case WM_DESTROY:
                hwnd_ = nullptr;
                owner->OnWindowDestroyed(id_);
                return 0;
            case WM_SIZE:
            {
                const auto was_minimized = minimized_;
                minimized_ = (wparam == SIZE_MINIMIZED);
                UpdateClientMetrics();
                if (minimized_)
                {
                    owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowMinimized, id_, client_width_, client_height_));
                }
                else
                {
                    if (was_minimized)
                    {
                        owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowRestored, id_, client_width_, client_height_));
                    }
                    owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowResized, id_, client_width_, client_height_));
                }
                return 0;
            }
            case WM_SETFOCUS:
                focused_ = true;
                owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowFocusChanged, id_, client_width_, client_height_, true));
                return 0;
            case WM_KILLFOCUS:
                focused_ = false;
                mouse_button_mask_ = 0;
                UpdateMouseCapture(false);
                owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowFocusChanged, id_, client_width_, client_height_, false));
                return 0;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                owner->EnqueueEvent(MakeKeyEvent(PlatformEventType::KeyPressed, id_, static_cast<std::uint32_t>(wparam),
                                                 (static_cast<std::uint32_t>(lparam) >> 16u) & 0xFFu,
                                                 (static_cast<std::uint32_t>(lparam) & (1u << 30u)) != 0));
                return 0;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                owner->EnqueueEvent(MakeKeyEvent(PlatformEventType::KeyReleased, id_, static_cast<std::uint32_t>(wparam),
                                                 (static_cast<std::uint32_t>(lparam) >> 16u) & 0xFFu, false));
                return 0;
            case WM_MOUSEMOVE:
                owner->EnqueueEvent(MakeMouseMoveEvent(id_, ExtractMouseX(lparam), ExtractMouseY(lparam)));
                return 0;
            case WM_LBUTTONDOWN:
                return HandleMouseButton(true, kMouseButtonLeft, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_LBUTTONUP:
                return HandleMouseButton(false, kMouseButtonLeft, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_RBUTTONDOWN:
                return HandleMouseButton(true, kMouseButtonRight, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_RBUTTONUP:
                return HandleMouseButton(false, kMouseButtonRight, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_MBUTTONDOWN:
                return HandleMouseButton(true, kMouseButtonMiddle, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_MBUTTONUP:
                return HandleMouseButton(false, kMouseButtonMiddle, ExtractMouseX(lparam), ExtractMouseY(lparam));
            case WM_XBUTTONDOWN:
                return HandleMouseButton(true, GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? kMouseButtonX1 : kMouseButtonX2,
                                         ExtractMouseX(lparam), ExtractMouseY(lparam), true);
            case WM_XBUTTONUP:
                return HandleMouseButton(false, GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? kMouseButtonX1 : kMouseButtonX2,
                                         ExtractMouseX(lparam), ExtractMouseY(lparam), true);
            case WM_MOUSEWHEEL:
            {
                POINT point{ExtractMouseX(lparam), ExtractMouseY(lparam)};
                ScreenToClient(hwnd_, &point);
                owner->EnqueueEvent(MakeMouseWheelEvent(id_, point.x, point.y,
                                                        static_cast<std::int16_t>(GET_WHEEL_DELTA_WPARAM(wparam))));
                return 0;
            }
            case WM_CAPTURECHANGED:
                mouse_button_mask_ = 0;
                UpdateMouseCapture(false);
                return 0;
            case WM_DPICHANGED:
                dpi_ = HIWORD(wparam);
                UpdateClientMetrics();
                return 0;
            default:
                return DefWindowProcW(hwnd_, message, wparam, lparam);
            }
        }

        void CloseFromRuntimeTeardownNoThrow() noexcept
        {
            CloseNativeWindowNoThrow(TryOwner(), false);
        }

      private:
        [[nodiscard]] Impl *TryOwner() const
        {
            const auto owner = owner_.lock();
            return owner ? owner->owner : nullptr;
        }

        [[nodiscard]] Impl &OwnerOrThrow(std::string_view operation) const
        {
            auto *owner = TryOwner();
            if (owner == nullptr)
            {
                throw std::runtime_error("WindowsPlatformRuntime has already been destroyed: " +
                                         std::string(operation));
            }
            return *owner;
        }

        void CloseNativeWindowNoThrow(Impl *owner, bool emit_close_event) noexcept
        {
            if (hwnd_ == nullptr)
            {
                return;
            }

            if (emit_close_event && owner != nullptr && !close_requested_)
            {
                close_requested_ = true;
                owner->EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowCloseRequested, id_));
            }
            else
            {
                close_requested_ = true;
            }

            if (IsWindow(hwnd_))
            {
                DestroyWindow(hwnd_);
            }
            hwnd_ = nullptr;
        }

        // Updates button-mask state, emits the matching mouse-button event, and refreshes capture ownership.
        [[nodiscard]] LRESULT HandleMouseButton(bool pressed, std::uint8_t button, std::int32_t x, std::int32_t y,
                                                bool return_true = false)
        {
            auto *owner = TryOwner();
            if (owner == nullptr)
            {
                return DefWindowProcW(hwnd_, pressed ? WM_LBUTTONDOWN : WM_LBUTTONUP, 0, 0);
            }
            const auto mask = static_cast<std::uint8_t>(1u << button);
            if (pressed)
            {
                mouse_button_mask_ |= mask;
            }
            else
            {
                mouse_button_mask_ &= static_cast<std::uint8_t>(~mask);
            }

            owner->EnqueueEvent(
                MakeMouseButtonEvent(pressed ? PlatformEventType::MouseButtonPressed : PlatformEventType::MouseButtonReleased,
                                     id_, button, x, y));
            UpdateMouseCapture(mouse_button_mask_ != 0);
            return return_true ? TRUE : 0;
        }

                // Synchronizes Win32 mouse capture with the current pressed-button set and emits capture-change events.
        void UpdateMouseCapture(bool captured)
        {
            auto *owner = TryOwner();
            if (hwnd_ == nullptr || mouse_captured_ == captured)
            {
                mouse_captured_ = captured;
                return;
            }

            if (captured)
            {
                SetCapture(hwnd_);
            }
            else if (GetCapture() == hwnd_)
            {
                ReleaseCapture();
            }

            mouse_captured_ = captured;
            if (owner != nullptr)
            {
                owner->EnqueueEvent(MakeCaptureChangedEvent(id_, captured));
            }
        }

                // Refreshes cached client-area dimensions from the current HWND.
        void UpdateClientMetrics()
        {
            if (hwnd_ == nullptr)
            {
                client_width_ = 0;
                client_height_ = 0;
                return;
            }

            RECT client_rect{};
            if (GetClientRect(hwnd_, &client_rect))
            {
                client_width_ = static_cast<std::uint32_t>(client_rect.right - client_rect.left);
                client_height_ = static_cast<std::uint32_t>(client_rect.bottom - client_rect.top);
            }
        }

        std::weak_ptr<RuntimeToken> owner_;
        WindowId id_{kInvalidWindowId};
        std::string title_;
        HWND hwnd_{nullptr};
        std::uint32_t client_width_{};
        std::uint32_t client_height_{};
        std::uint32_t dpi_{96};
        bool focused_{false};
        bool minimized_{false};
        bool close_requested_{false};
        bool mouse_captured_{false};
        std::uint8_t mouse_button_mask_{0};
    };

    static constexpr wchar_t kWindowClassName[] = L"EpidemicEnginePlatformWindow";

    std::shared_ptr<RuntimeToken> runtime_token{std::make_shared<RuntimeToken>()};
    ProcessInfo process_info{BuildProcessInfo()};
    HINSTANCE instance_handle{GetModuleHandleW(nullptr)};
    std::vector<PlatformEvent> queued_events;
    std::unordered_map<WindowId, std::shared_ptr<WindowsWindow>> windows_by_id;
    std::unordered_map<HWND, WindowsWindow *> windows_by_handle;
    std::uint64_t next_window_id{1};
    bool class_registered{false};
    bool exit_requested{false};
    std::thread::id main_thread_id{std::this_thread::get_id()};
    mutable std::mutex mutex;

        // Releases all tracked windows and unregisters the window class during runtime teardown.
    ~Impl() noexcept
    {
        std::unordered_map<WindowId, std::shared_ptr<WindowsWindow>> windows_to_close;
        {
            std::scoped_lock lock(mutex);
            windows_to_close.swap(windows_by_id);
            windows_by_handle.clear();
        }

        runtime_token->owner = nullptr;
        for (auto &[window_id, window] : windows_to_close)
        {
            static_cast<void>(window_id);
            if (window)
            {
                window->CloseFromRuntimeTeardownNoThrow();
            }
        }

        try
        {
            if (std::this_thread::get_id() == main_thread_id)
            {
                PumpMessages();
            }
        }
        catch (...)
        {
        }

        if (class_registered)
        {
            UnregisterClassW(kWindowClassName, instance_handle);
        }

        runtime_token->owner = nullptr;
    }

        // Throws when a runtime operation is invoked from a non-owner thread.
    void EnsureMainThread(std::string_view operation) const
    {
        if (std::this_thread::get_id() != main_thread_id)
        {
            throw std::runtime_error("WindowsPlatformRuntime operation must run on the owner thread: " +
                                     std::string(operation));
        }
    }
        // Creates a Win32 window, attaches it to a WindowsWindow wrapper, and starts tracking it.
    [[nodiscard]] epidemic::foundation::Result<WindowPtr> CreateWindow(const WindowCreateInfo &create_info)
    {
        if (create_info.client_width == 0 || create_info.client_height == 0)
        {
            return epidemic::foundation::Result<WindowPtr>::Failure(
                epidemic::foundation::Error::Create("platform.invalid_window_size",
                                                    "Window client size must be greater than zero"));
        }

        const auto registration_result = EnsureWindowClassRegistered();
        if (!registration_result.HasValue())
        {
            return epidemic::foundation::Result<WindowPtr>::Failure(registration_result.GetError());
        }

        const auto title = create_info.title.empty() ? std::string("Epidemic Engine v1.0") : create_info.title;
        const auto title_wide = WidenUtf8String(title);
        const auto window_id = next_window_id++;
        auto window = std::make_shared<WindowsWindow>(runtime_token, window_id, title);

        constexpr DWORD window_style = WS_OVERLAPPEDWINDOW;
        RECT desired_rect{0, 0, static_cast<LONG>(create_info.client_width), static_cast<LONG>(create_info.client_height)};
        AdjustWindowRectEx(&desired_rect, window_style, FALSE, 0);

        const auto hwnd = CreateWindowExW(0, kWindowClassName, title_wide.c_str(), window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                                          desired_rect.right - desired_rect.left, desired_rect.bottom - desired_rect.top,
                                          nullptr, nullptr, instance_handle, window.get());
        if (hwnd == nullptr)
        {
            const auto error_code = GetLastError();
            return epidemic::foundation::Result<WindowPtr>::Failure(
                epidemic::foundation::Error::Create(
                    "platform.create_window_failed",
                    "Failed to create Win32 window '" + title + "' (" + FormatWindowsErrorMessage(error_code) + ")"));
        }

        window->Attach(hwnd);
        {
            std::scoped_lock lock(mutex);
            windows_by_id.emplace(window_id, window);
            windows_by_handle.emplace(hwnd, window.get());
        }

        if (create_info.visible)
        {
            window->Show();
        }

        return epidemic::foundation::Result<WindowPtr>::Success(std::static_pointer_cast<IWindow>(window));
    }

        // Lazily registers the Win32 window class used for all EngineBase windows.
    [[nodiscard]] epidemic::foundation::Result<int> EnsureWindowClassRegistered()
    {
        if (class_registered)
        {
            return epidemic::foundation::Result<int>::Success(0);
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance_handle;
        window_class.lpfnWndProc = &StaticWindowProc;
        window_class.lpszClassName = kWindowClassName;
        window_class.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.style = CS_HREDRAW | CS_VREDRAW;

        if (RegisterClassExW(&window_class) == 0)
        {
            const auto error_code = GetLastError();
            return epidemic::foundation::Result<int>::Failure(
                epidemic::foundation::Error::Create(
                    "platform.register_window_class_failed",
                    "Failed to register Win32 window class (" + FormatWindowsErrorMessage(error_code) + ")"));
        }

        class_registered = true;
        return epidemic::foundation::Result<int>::Success(0);
    }

        // Pumps all currently pending Win32 messages.
    void PumpMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                exit_requested = true;
                continue;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

        // Queues one normalized PlatformEvent for later drainage by higher layers.
    void EnqueueEvent(PlatformEvent event)
    {
        std::scoped_lock lock(mutex);
        queued_events.push_back(event);

    }

        // Removes destroyed-window bookkeeping and requests process exit when the final window disappears.
    void OnWindowDestroyed(WindowId window_id)
    {
        std::scoped_lock lock(mutex);
        HWND handle_to_remove = nullptr;
        for (auto it = windows_by_handle.begin(); it != windows_by_handle.end(); ++it)
        {
            if (it->second != nullptr && it->second->Id() == window_id)
            {
                handle_to_remove = it->first;
                windows_by_handle.erase(it);
                break;
            }
        }

        static_cast<void>(handle_to_remove);
        windows_by_id.erase(window_id);
        if (windows_by_id.empty())
        {
            exit_requested = true;
            PostQuitMessage(0);
        }
    }

        // Drains all queued PlatformEvent values.
    [[nodiscard]] std::vector<PlatformEvent> DrainEvents()
    {
        std::scoped_lock lock(mutex);
        std::vector<PlatformEvent> drained_events;
        drained_events.swap(queued_events);
        return drained_events;
    }

        // Returns whether normalized PlatformEvent values are waiting to be drained.
    [[nodiscard]] bool HasPendingEvents() const noexcept
    {
        std::scoped_lock lock(mutex);
        return !queued_events.empty();
    }

        // Returns the number of currently tracked windows.
    [[nodiscard]] std::size_t WindowCount() const noexcept
    {
        std::scoped_lock lock(mutex);
        return windows_by_id.size();
    }

        // Win32 static window procedure that forwards messages to the associated WindowsWindow instance.
    [[nodiscard]] static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            const auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(lparam);
            auto *window = static_cast<WindowsWindow *>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            return TRUE;
        }

        if (auto *window = reinterpret_cast<WindowsWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        {
            return window->HandleMessage(message, wparam, lparam);
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
};

// Creates the runtime implementation and captures the constructing thread as the owner.
WindowsPlatformRuntime::WindowsPlatformRuntime() : impl_(std::make_unique<Impl>())
{
}

// Defaulted because the Impl object owns teardown behavior.
WindowsPlatformRuntime::~WindowsPlatformRuntime() = default;

// Returns the stable backend name exposed through IPlatformRuntime.
std::string_view WindowsPlatformRuntime::Name() const
{
    return "WindowsPlatformRuntime";
}

// Returns immutable process metadata captured during construction.
const ProcessInfo &WindowsPlatformRuntime::GetProcessInfo() const
{
    return impl_->process_info;
}

// Returns the current EngineBase clock value.
epidemic::foundation::TimePoint WindowsPlatformRuntime::Now() const
{
    return epidemic::foundation::Clock::now();
}

epidemic::foundation::Result<DynamicLibraryPtr>
// Loads a dynamic library through Win32 and wraps it in the EngineBase abstraction.
WindowsPlatformRuntime::LoadDynamicLibrary(const epidemic::foundation::Path &path)
{
    if (path.Empty())
    {
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("platform.empty_library_path", "Dynamic library path must not be empty"));
    }

    const auto &requested_path = path.Native();
    if (!requested_path.is_absolute())
    {
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("platform.relative_library_path",
                                                "Dynamic library path must be canonical and absolute"));
    }

    std::error_code canonical_error;
    auto canonical_path = std::filesystem::weakly_canonical(requested_path, canonical_error);
    if (canonical_error)
    {
        canonical_path = requested_path.lexically_normal();
    }
    if (!canonical_path.is_absolute())
    {
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("platform.relative_library_path",
                                                "Dynamic library path must resolve to an absolute path"));
    }

    constexpr DWORD load_flags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    const auto module_handle = LoadLibraryExW(canonical_path.c_str(), nullptr, load_flags);
    if (module_handle == nullptr)
    {
        const auto error_code = GetLastError();
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create(
                "platform.load_library_failed",
                "Failed to load dynamic library '" + epidemic::foundation::Path(canonical_path).GenericString() + "' (" +
                    FormatWindowsErrorMessage(error_code) + ")"));
    }

    const auto library_name = epidemic::foundation::Path(canonical_path).GenericString();
    auto dynamic_library = std::make_shared<WindowsDynamicLibrary>(library_name, module_handle);
    return epidemic::foundation::Result<DynamicLibraryPtr>::Success(std::move(dynamic_library));
}

// Pumps all pending Win32 messages through the internal implementation.
void WindowsPlatformRuntime::PumpEvents()
{
    impl_->PumpMessages();
}

// Returns whether the runtime has observed an exit condition.
bool WindowsPlatformRuntime::IsExitRequested() const
{
    return impl_->exit_requested;
}

// Creates a window through the internal Win32 implementation.
epidemic::foundation::Result<WindowPtr> WindowsPlatformRuntime::CreateWindow(const WindowCreateInfo &create_info)
{
    return impl_->CreateWindow(create_info);
}

// Drains normalized platform events from the internal queue.
std::vector<PlatformEvent> WindowsPlatformRuntime::DrainEvents()
{
    return impl_->DrainEvents();
}

// Returns whether normalized platform events are waiting to be drained.
bool WindowsPlatformRuntime::HasPendingEvents() const noexcept
{
    return impl_->HasPendingEvents();
}

// Returns the number of currently tracked windows.
std::size_t WindowsPlatformRuntime::WindowCount() const noexcept
{
    return impl_->WindowCount();
}
} // namespace epidemic::platform













