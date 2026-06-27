#pragma once

namespace epidemic::layers::runtime
{
class IScriptHost
{
  public:
    virtual ~IScriptHost() = default;

    // Reports whether the script host is initialized and ready to execute scripts.
    [[nodiscard]] virtual bool IsReady() const = 0;
};
} // namespace epidemic::layers::runtime
