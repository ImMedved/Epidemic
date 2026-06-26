#pragma once

#include "core/diagnostics/logger.h"

#include <mutex>

namespace epidemic::core::diagnostics
{
class ConsoleLogger final : public ILogger
{
  public:
    void Log(LogLevel level, std::string_view category, std::string_view message) override;

  private:
    std::mutex mutex_;
};
} // namespace epidemic::core::diagnostics
