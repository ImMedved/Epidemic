#include "core/diagnostics/console_logger.h"

#include <iostream>

namespace epidemic::core::diagnostics
{
void ConsoleLogger::Log(LogLevel level, std::string_view category, std::string_view message)
{
    std::scoped_lock lock(mutex_);
    std::cout << "[" << ToString(level) << "]"
              << " [" << category << "] " << message << '\n';
}
} // namespace epidemic::core::diagnostics
