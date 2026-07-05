#pragma once

#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow_system.h>

#include <memory>

namespace epidemic::platform
{
// WindowsPlatformRuntime is currently a main-thread runtime.
// Window creation, event pumping, and destruction are expected to happen on the
// same thread that constructed the runtime instance.
class WindowsPlatformRuntime final : public IPlatformRuntime, public IWindowSystem
{
  public:
    WindowsPlatformRuntime();
    ~WindowsPlatformRuntime() override;

    [[nodiscard]] std::string_view Name() const override;
    [[nodiscard]] const ProcessInfo &GetProcessInfo() const override;
    [[nodiscard]] epidemic::foundation::TimePoint Now() const override;
    [[nodiscard]] epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) override;
    void PumpEvents() override;
    [[nodiscard]] bool IsExitRequested() const override;

    [[nodiscard]] epidemic::foundation::Result<WindowPtr> CreateWindow(const WindowCreateInfo &create_info) override;
    [[nodiscard]] std::vector<PlatformEvent> DrainEvents() override;
    [[nodiscard]] bool HasPendingEvents() const noexcept override;
    [[nodiscard]] std::size_t WindowCount() const noexcept override;

  private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};
} // namespace epidemic::platform
