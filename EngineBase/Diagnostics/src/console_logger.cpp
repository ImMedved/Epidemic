#include <Epidemic/Diagnostics/console_logger.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

namespace epidemic::diagnostics
{
// This file implements the default console-and-file logger used by EngineBase.
// The logger detects the repository root from stable markers and writes logs to logs/epidemic.log.

namespace
{
// Returns true when the supplied directory looks like the repository root expected by EngineBase.
[[nodiscard]] bool IsProjectRootCandidate(const std::filesystem::path &path)
{
    const bool has_engine_base_directory = std::filesystem::exists(path / "EngineBase");
    const bool has_root_cmake = std::filesystem::exists(path / "CMakeLists.txt");
    const bool has_api_stability = std::filesystem::exists(path / "EngineBase" / "API_STABILITY.md");
    return has_engine_base_directory && (has_root_cmake || has_api_stability);
}

// Walks upward from the current working directory until a repository root marker is found.
// Output: resolved project root, or current_path() as a safe fallback when detection fails.
std::filesystem::path FindProjectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (IsProjectRootCandidate(current))
        {
            return current;
        }

        const auto parent = current.parent_path();
        if (parent == current)
        {
            break;
        }

        current = parent;
    }

    return std::filesystem::current_path();
}
} // namespace

// Opens the log file lazily so simple test runs do not pay setup cost until the first log write.
void ConsoleLogger::EnsureLogFileInitialized()
{
    if (file_initialized_)
    {
        return;
    }

    file_initialized_ = true;

    const auto logs_directory = FindProjectRoot() / "logs";
    std::error_code error;
    std::filesystem::create_directories(logs_directory, error);
    if (error)
    {
        return;
    }

    file_.open(logs_directory / "epidemic.log", std::ios::out | std::ios::app);
}

// Formats one log line, writes it to stdout, and mirrors it to the persistent log file when available.
void ConsoleLogger::Log(const LogMessage &message)
{
    const auto time = std::chrono::system_clock::to_time_t(message.timestamp);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(message.timestamp.time_since_epoch()) % 1000;

    std::tm local_time{};
    localtime_s(&local_time, &time);

    std::ostringstream thread_stream;
    thread_stream << message.thread_id;

    std::ostringstream line;
    line << '[' << std::put_time(&local_time, "%H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
         << milliseconds.count() << "]"
         << " [" << ToString(message.level) << ']'
         << " [" << message.module_name << ']';

    if (!message.category.empty())
    {
        line << " [" << message.category << ']';
    }

    line << " [thread=";
    if (!message.thread_name.empty())
    {
        line << message.thread_name;
    }
    else
    {
        line << thread_stream.str();
    }
    line << "] " << message.message;

    std::scoped_lock lock(mutex_);
    EnsureLogFileInitialized();

    const auto formatted_line = line.str();
    std::cout << formatted_line << '\n';

    if (file_.is_open())
    {
        file_ << formatted_line << '\n';
        file_.flush();
    }
}
} // namespace epidemic::diagnostics