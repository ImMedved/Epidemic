#include <Epidemic/Platform/windows_platform_runtime.h>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#ifdef CreateWindow
#undef CreateWindow
#endif

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
namespace
{
constexpr std::uint8_t kMouseButtonLeft = 0;
constexpr std::uint8_t kMouseButtonRight = 1;
constexpr std::uint8_t kMouseButtonMiddle = 2;
constexpr std::uint8_t kMouseButtonX1 = 3;
constexpr std::uint8_t kMouseButtonX2 = 4;

[[nodiscard]] constexpr std::int32_t ExtractMouseX(LPARAM lparam) noexcept
{
    return static_cast<std::int32_t>(static_cast<short>(LOWORD(static_cast<DWORD_PTR>(lparam))));
}

[[nodiscard]] constexpr std::int32_t ExtractMouseY(LPARAM lparam) noexcept
{
    return static_cast<std::int32_t>(static_cast<short>(HIWORD(static_cast<DWORD_PTR>(lparam))));
}

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

[[nodiscard]] PlatformEvent MakeMouseMoveEvent(WindowId window_id, std::int32_t x, std::int32_t y) noexcept
{
    PlatformEvent event;
    event.type = PlatformEventType::MouseMoved;
    event.window_id = window_id;
    event.mouse_x = x;
    event.mouse_y = y;
    return event;
}

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

[[nodiscard]] PlatformEvent MakeCaptureChangedEvent(WindowId window_id, bool captured) noexcept
{
    PlatformEvent event;
    event.type = PlatformEventType::MouseCaptureChanged;
    event.window_id = window_id;
    event.captured = captured;
    return event;
}
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

class WindowsDynamicLibrary final : public IDynamicLibrary
{
  public:
    WindowsDynamicLibrary(std::string name, HMODULE module_handle)
        : name_(std::move(name)), module_handle_(module_handle)
    {
    }

    ~WindowsDynamicLibrary() override
    {
        if (module_handle_ != nullptr)
        {
            FreeLibrary(module_handle_);
        }
    }

    [[nodiscard]] std::string_view Name() const override
    {
        return name_;
    }

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
    class WindowsWindow final : public IWindow
    {
      public:
        WindowsWindow(Impl &owner, WindowId id, std::string title)
            : owner_(owner), id_(id), title_(std::move(title))
        {
        }

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

        void Show() override
        {
            owner_.EnsureMainThread("IWindow::Show");
            if (hwnd_ != nullptr)
            {
                ShowWindow(hwnd_, SW_SHOWNORMAL);
                UpdateWindow(hwnd_);
            }
        }

        void Close() override
        {
            owner_.EnsureMainThread("IWindow::Close");
            if (hwnd_ != nullptr)
            {
                if (!close_requested_)
                {
                    close_requested_ = true;
                    owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowCloseRequested, id_));
                }
                DestroyWindow(hwnd_);
            }
        }

        void Attach(HWND hwnd)
        {
            owner_.EnsureMainThread("WindowsWindow::Attach");
            hwnd_ = hwnd;
            UpdateClientMetrics();
            dpi_ = GetDpiForWindow(hwnd_);
            focused_ = (GetFocus() == hwnd_);
            mouse_captured_ = (GetCapture() == hwnd_);
        }

        void EnsureClientSize(std::uint32_t target_width, std::uint32_t target_height)
        {
            owner_.EnsureMainThread("WindowsWindow::EnsureClientSize");
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

        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message)
            {
            case WM_CLOSE:
                if (!close_requested_)
                {
                    close_requested_ = true;
                    owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowCloseRequested, id_));
                }
                return 0;
            case WM_DESTROY:
                hwnd_ = nullptr;
                owner_.OnWindowDestroyed(id_);
                return 0;
            case WM_SIZE:
            {
                const auto was_minimized = minimized_;
                minimized_ = (wparam == SIZE_MINIMIZED);
                UpdateClientMetrics();
                if (minimized_)
                {
                    owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowMinimized, id_, client_width_, client_height_));
                }
                else
                {
                    if (was_minimized)
                    {
                        owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowRestored, id_, client_width_, client_height_));
                    }
                    owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowResized, id_, client_width_, client_height_));
                }
                return 0;
            }
            case WM_SETFOCUS:
                focused_ = true;
                owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowFocusChanged, id_, client_width_, client_height_, true));
                return 0;
            case WM_KILLFOCUS:
                focused_ = false;
                mouse_button_mask_ = 0;
                UpdateMouseCapture(false);
                owner_.EnqueueEvent(MakeWindowEvent(PlatformEventType::WindowFocusChanged, id_, client_width_, client_height_, false));
                return 0;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                owner_.EnqueueEvent(MakeKeyEvent(PlatformEventType::KeyPressed, id_, static_cast<std::uint32_t>(wparam),
                                                 (static_cast<std::uint32_t>(lparam) >> 16u) & 0xFFu,
                                                 (static_cast<std::uint32_t>(lparam) & (1u << 30u)) != 0));
                return 0;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                owner_.EnqueueEvent(MakeKeyEvent(PlatformEventType::KeyReleased, id_, static_cast<std::uint32_t>(wparam),
                                                 (static_cast<std::uint32_t>(lparam) >> 16u) & 0xFFu, false));
                return 0;
            case WM_MOUSEMOVE:
                owner_.EnqueueEvent(MakeMouseMoveEvent(id_, ExtractMouseX(lparam), ExtractMouseY(lparam)));
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
                owner_.EnqueueEvent(MakeMouseWheelEvent(id_, point.x, point.y,
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

      private:
        [[nodiscard]] LRESULT HandleMouseButton(bool pressed, std::uint8_t button, std::int32_t x, std::int32_t y,
                                                bool return_true = false)
        {
            const auto mask = static_cast<std::uint8_t>(1u << button);
            if (pressed)
            {
                mouse_button_mask_ |= mask;
            }
            else
            {
                mouse_button_mask_ &= static_cast<std::uint8_t>(~mask);
            }

            owner_.EnqueueEvent(
                MakeMouseButtonEvent(pressed ? PlatformEventType::MouseButtonPressed : PlatformEventType::MouseButtonReleased,
                                     id_, button, x, y));
            UpdateMouseCapture(mouse_button_mask_ != 0);
            return return_true ? TRUE : 0;
        }

        void UpdateMouseCapture(bool captured)
        {
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
            owner_.EnqueueEvent(MakeCaptureChangedEvent(id_, captured));
        }

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

        Impl &owner_;
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

    ~Impl()
    {
        std::vector<std::shared_ptr<WindowsWindow>> windows;
        {
            std::scoped_lock lock(mutex);
            for (auto &[window_id, window] : windows_by_id)
            {
                static_cast<void>(window_id);
                windows.push_back(window);
            }
            windows_by_id.clear();
            windows_by_handle.clear();
        }

        for (auto &window : windows)
        {
            if (window)
            {
                window->Close();
            }
        }

        PumpMessages();

        if (class_registered)
        {
            UnregisterClassW(kWindowClassName, instance_handle);
        }
    }

    void EnsureMainThread(std::string_view operation) const
    {
        if (std::this_thread::get_id() != main_thread_id)
        {
            throw std::runtime_error("WindowsPlatformRuntime operation must run on the owner thread: " +
                                     std::string(operation));
        }
    }
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
        auto window = std::make_shared<WindowsWindow>(*this, window_id, title);

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

    void EnqueueEvent(PlatformEvent event)
    {
        std::scoped_lock lock(mutex);
        queued_events.push_back(event);

    }

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

    [[nodiscard]] std::vector<PlatformEvent> DrainEvents()
    {
        std::scoped_lock lock(mutex);
        std::vector<PlatformEvent> drained_events;
        drained_events.swap(queued_events);
        return drained_events;
    }

    [[nodiscard]] bool HasPendingEvents() const noexcept
    {
        std::scoped_lock lock(mutex);
        return !queued_events.empty();
    }

    [[nodiscard]] std::size_t WindowCount() const noexcept
    {
        std::scoped_lock lock(mutex);
        return windows_by_id.size();
    }

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

WindowsPlatformRuntime::WindowsPlatformRuntime() : impl_(std::make_unique<Impl>())
{
}

WindowsPlatformRuntime::~WindowsPlatformRuntime() = default;

std::string_view WindowsPlatformRuntime::Name() const
{
    return "WindowsPlatformRuntime";
}

const ProcessInfo &WindowsPlatformRuntime::GetProcessInfo() const
{
    return impl_->process_info;
}

epidemic::foundation::TimePoint WindowsPlatformRuntime::Now() const
{
    return epidemic::foundation::Clock::now();
}

epidemic::foundation::Result<DynamicLibraryPtr>
WindowsPlatformRuntime::LoadDynamicLibrary(const epidemic::foundation::Path &path)
{
    if (path.Empty())
    {
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create("platform.empty_library_path", "Dynamic library path must not be empty"));
    }

    const auto module_handle = LoadLibraryW(path.Native().c_str());
    if (module_handle == nullptr)
    {
        const auto error_code = GetLastError();
        return epidemic::foundation::Result<DynamicLibraryPtr>::Failure(
            epidemic::foundation::Error::Create(
                "platform.load_library_failed",
                "Failed to load dynamic library '" + path.GenericString() + "' (" + FormatWindowsErrorMessage(error_code) +
                    ")"));
    }

    const auto library_name = path.GenericString().empty() ? path.Native().string() : path.GenericString();
    auto dynamic_library = std::make_shared<WindowsDynamicLibrary>(library_name, module_handle);
    return epidemic::foundation::Result<DynamicLibraryPtr>::Success(std::move(dynamic_library));
}

void WindowsPlatformRuntime::PumpEvents()
{
    impl_->PumpMessages();
}

bool WindowsPlatformRuntime::IsExitRequested() const
{
    return impl_->exit_requested;
}

epidemic::foundation::Result<WindowPtr> WindowsPlatformRuntime::CreateWindow(const WindowCreateInfo &create_info)
{
    return impl_->CreateWindow(create_info);
}

std::vector<PlatformEvent> WindowsPlatformRuntime::DrainEvents()
{
    return impl_->DrainEvents();
}

bool WindowsPlatformRuntime::HasPendingEvents() const noexcept
{
    return impl_->HasPendingEvents();
}

std::size_t WindowsPlatformRuntime::WindowCount() const noexcept
{
    return impl_->WindowCount();
}
} // namespace epidemic::platform













