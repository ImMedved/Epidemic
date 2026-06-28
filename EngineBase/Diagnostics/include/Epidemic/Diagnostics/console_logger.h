#pragma once

#include <Epidemic/Diagnostics/logger.h>

#include <mutex>

namespace epidemic::diagnostics
{
class ConsoleLogger final : public ILogger
{
  public:
    void Log(LogLevel level, std::string_view category, std::string_view message) override;

  private:
    std::mutex mutex_;
};
} // namespace epidemic::diagnostics