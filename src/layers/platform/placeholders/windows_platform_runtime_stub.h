#pragma once

#include "layers/platform/interfaces/iplatform_runtime.h"

namespace epidemic::layers::platform
{
class WindowsPlatformRuntimeStub final : public IPlatformRuntime
{
  public:
    [[nodiscard]] std::string_view Name() const override;
    void PumpEvents() override;
    [[nodiscard]] bool IsExitRequested() const override;
};
} // namespace epidemic::layers::platform
