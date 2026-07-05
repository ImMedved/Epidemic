#pragma once

#include <Epidemic/Platform/native_window_handle.h>
#include <Epidemic/Platform/platform_event.h>

#include <string_view>

namespace epidemic::platform
{
class IWindow
{
  public:
    virtual ~IWindow() = default;

    [[nodiscard]] virtual WindowId Id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view Title() const noexcept = 0;
    [[nodiscard]] virtual NativeWindowHandle GetNativeHandle() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t ClientWidth() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t ClientHeight() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t Dpi() const noexcept = 0;
    [[nodiscard]] virtual bool HasFocus() const noexcept = 0;
    [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;
    [[nodiscard]] virtual bool IsCloseRequested() const noexcept = 0;
    virtual void Show() = 0;
    virtual void Close() = 0;
};
} // namespace epidemic::platform
