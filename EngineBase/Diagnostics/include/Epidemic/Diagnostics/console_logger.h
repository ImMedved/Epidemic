#pragma once

#include <Epidemic/Diagnostics/logger.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace epidemic::diagnostics
{
// This file declares the default logger implementation used by EngineBase smoke apps and tests.
// ConsoleLogger mirrors each formatted line to stdout and to logs/epidemic.log near the project root.

class ConsoleLogger final : public ILogger
{
  public:
    // Formats and writes one structured log message to the console and to the log file.
    void Log(const LogMessage &message) override;

  private:
    // Lazily opens the log file on first write.
    // Relationship: called under mutex from Log to keep initialization thread-safe.
    void EnsureLogFileInitialized();

    std::mutex mutex_;
    std::ofstream file_;
    bool file_initialized_{false};
};
} // namespace epidemic::diagnostics