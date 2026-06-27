#pragma once

#include "layers/platform/interfaces/iplatform_runtime.h"

namespace epidemic::layers::platform
{
class WindowsPlatformRuntime final : public IPlatformRuntime
{
  public:
    WindowsPlatformRuntime();

    [[nodiscard]] std::string_view Name() const override;
    [[nodiscard]] const ProcessInfo &GetProcessInfo() const override;
    [[nodiscard]] std::chrono::steady_clock::time_point Now() const override;
    [[nodiscard]] epidemic::foundation::Result<DynamicLibraryPtr>
    LoadDynamicLibrary(const epidemic::foundation::Path &path) override;
    void PumpEvents() override;
    [[nodiscard]] bool IsExitRequested() const override;

  private:
    ProcessInfo process_info_;
    bool exit_requested_{false};
};
} // namespace epidemic::layers::platform
