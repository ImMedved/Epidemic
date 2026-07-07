#pragma once

#include <Epidemic/Foundation/result.h>
#include <Epidemic/Platform/iwindow.h>
#include <Epidemic/Platform/platform_event.h>

#include <memory>
#include <string>
#include <vector>

namespace epidemic::platform
{
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

    [[nodiscard]] virtual epidemic::foundation::Result<WindowPtr> CreateWindow(const WindowCreateInfo &create_info) = 0;
    [[nodiscard]] virtual std::vector<PlatformEvent> DrainEvents() = 0;
    [[nodiscard]] virtual bool HasPendingEvents() const noexcept = 0;
    [[nodiscard]] virtual std::size_t WindowCount() const noexcept = 0;
};
} // namespace epidemic::platform
