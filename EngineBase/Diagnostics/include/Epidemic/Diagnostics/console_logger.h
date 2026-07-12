#pragma once

#include <Epidemic/Diagnostics/logger.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace epidemic::diagnostics
{
class ConsoleLogger final : public ILogger
{
  public:
    void Log(const LogMessage &message) override;

  private:
    void EnsureLogFileInitialized();

    std::mutex mutex_;
    std::ofstream file_;
    bool file_initialized_{false};
};
} 
