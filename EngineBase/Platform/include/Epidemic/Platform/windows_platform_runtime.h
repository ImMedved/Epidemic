#pragma once

#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/iwindow_system.h>

#include <memory>

namespace epidemic::platform
{
// This file declares the Win32 implementation of both IPlatformRuntime and IWindowSystem.
// WindowsPlatformRuntime is currently a main-thread runtime: window creation, event pumping,
// and destruction are expected to happen on the constructing thread.

class WindowsPlatformRuntime final : public IPlatformRuntime, public IWindowSystem
{
  public:
    // Captures process information and initializes the internal Win32 runtime state.
    WindowsPlatformRuntime();

    // Releases the internal implementation and any tracked windows.
    ~WindowsPlatformRuntime() override;

    // Returns the backend name for diagnostics and logs.
    [[nodiscard]] std::string_view Name() const override;

    // Returns captured process metadata.
    [[nodiscard]] const ProcessInfo &GetProcessInfo() const override;

    // Returns the current system time.
    [[nodiscard]] epidemic::foundation::TimePoint Now() const override;

    // Loads a dynamic library through Win32 LoadLibraryW.
    [[nodiscard]] epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) override;

    // Pumps pending Win32 messages and updates internal event queues.
    void PumpEvents() override;

    // Returns whether any tracked window or process state has requested exit.
    [[nodiscard]] bool IsExitRequested() const override;

    // Creates a new Win32 window and returns it as an IWindow implementation.
    [[nodiscard]] epidemic::foundation::Result<WindowPtr> CreateWindow(const WindowCreateInfo &create_info) override;

    // Drains and returns accumulated PlatformEvent values.
    [[nodiscard]] std::vector<PlatformEvent> DrainEvents() override;

    // Returns whether the runtime has queued PlatformEvent values waiting to be drained.
    [[nodiscard]] bool HasPendingEvents() const noexcept override;

    // Returns the number of tracked windows.
    [[nodiscard]] std::size_t WindowCount() const noexcept override;

  private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
};
} // namespace epidemic::platform