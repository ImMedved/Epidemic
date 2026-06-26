#pragma once

namespace epidemic::layers::runtime
{
class IScriptHost
{
  public:
    virtual ~IScriptHost() = default;

    [[nodiscard]] virtual bool IsReady() const = 0;
};
} // namespace epidemic::layers::runtime
