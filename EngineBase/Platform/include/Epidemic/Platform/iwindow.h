#pragma once

#include <Epidemic/Platform/native_window_handle.h>
#include <Epidemic/Platform/platform_event.h>

#include <string_view>

namespace epidemic::platform
{
// This file defines the platform window abstraction consumed by EngineBase and smoke apps.
// A window exposes identity, size/focus state, and a minimal set of imperative lifecycle actions.

class IWindow
{
  public:
    virtual ~IWindow() = default;

    // Returns the EngineBase-assigned logical window identifier.
    [[nodiscard]] virtual WindowId Id() const noexcept = 0;

    // Returns the current UTF-8 title.
    [[nodiscard]] virtual std::string_view Title() const noexcept = 0;

    // Returns the native OS window handle wrapped in a portable carrier type.
    [[nodiscard]] virtual NativeWindowHandle GetNativeHandle() const noexcept = 0;

    // Returns the current client-area width in pixels.
    [[nodiscard]] virtual std::uint32_t ClientWidth() const noexcept = 0;

    // Returns the current client-area height in pixels.
    [[nodiscard]] virtual std::uint32_t ClientHeight() const noexcept = 0;

    // Returns the current DPI associated with the native window.
    [[nodiscard]] virtual std::uint32_t Dpi() const noexcept = 0;

    // Returns whether the window currently has input focus.
    [[nodiscard]] virtual bool HasFocus() const noexcept = 0;

    // Returns whether the window is currently minimized.
    [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;

    // Returns whether a close request has been observed for this window.
    [[nodiscard]] virtual bool IsCloseRequested() const noexcept = 0;

    // Makes the window visible to the user.
    virtual void Show() = 0;

    // Requests final destruction of the native window.
    virtual void Close() = 0;
};
} // namespace epidemic::platform