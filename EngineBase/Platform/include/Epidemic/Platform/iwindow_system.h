#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/Platform/iwindow.h>
#include <Epidemic/Platform/platform_event.h>

#include <memory>
#include <string>
#include <vector>

namespace epidemic::platform
{
// This file defines the window-system abstraction layered alongside IPlatformRuntime.
// The window system owns window creation and exposes drained platform events to higher layers.

struct WindowCreateInfo
{
    std::string title = "Epidemic Engine v1.0";
    std::uint32_t client_width = 1280;
    std::uint32_t client_height = 720;
    bool visible = true;
};

using WindowPtr = std::shared_ptr<IWindow>;

class IWindowSystem
{
  public:
    virtual ~IWindowSystem() = default;

    // Creates a new platform window and returns either the window object or an Error.
    [[nodiscard]] virtual epidemic::foundation::Result<WindowPtr> CreateWindow(const WindowCreateInfo &create_info) = 0;

    // Drains and returns accumulated platform events since the previous drain.
    [[nodiscard]] virtual std::vector<PlatformEvent> DrainEvents() = 0;

    // Returns whether events are waiting to be drained.
    [[nodiscard]] virtual bool HasPendingEvents() const noexcept = 0;

    // Returns the number of currently tracked windows.
    [[nodiscard]] virtual std::size_t WindowCount() const noexcept = 0;
};
} // namespace epidemic::platform