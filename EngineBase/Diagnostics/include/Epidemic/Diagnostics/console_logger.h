#pragma once

#include <Epidemic/Diagnostics/logger.h>

#include <fstream>
#include <mutex>

namespace epidemic::diagnostics
{
// Default best-effort console/file sink. ILogger contains all sink exceptions at the public boundary.
class ConsoleLogger final : public ILogger
{
  private:
    // Formats and writes one structured log message to the console and, when available, the log file.
    void Write(const LogMessage &message) override;

    // Lazily opens the log file on first write. Called under mutex from Write.
    void EnsureLogFileInitialized();

    std::mutex mutex_;
    std::ofstream file_;
    bool file_initialized_{false};
};
} // namespace epidemic::diagnostics
