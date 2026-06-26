#pragma once

#include <string_view>

namespace epidemic::layers::platform
{
class IPlatformRuntime
{
  public:
    virtual ~IPlatformRuntime() = default;

    [[nodiscard]] virtual std::string_view Name() const = 0;
    virtual void PumpEvents() = 0;
    [[nodiscard]] virtual bool IsExitRequested() const = 0;
};
} // namespace epidemic::layers::platform
