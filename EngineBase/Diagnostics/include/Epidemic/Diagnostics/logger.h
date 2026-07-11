#pragma once

#include <Epidemic/Diagnostics/thread_context.h>

#include <chrono>
#include <string_view>
#include <thread>

namespace epidemic::diagnostics
{
// This file defines the minimal logging contract shared by EngineBase systems.
// The API is intentionally lightweight: callers provide structured log fields,
// while concrete loggers decide where the final formatted output is written.

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

// Converts a log level into a stable uppercase label suitable for console or file output.
// Input: log severity enum.
// Output: borrowed text label with process-lifetime storage.
[[nodiscard]] inline std::string_view ToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }

    return "UNKNOWN";
}

// Captures one structured logging request before a concrete logger formats it.
// Relationship: ILogger::Log(const LogMessage&) is the canonical sink for this payload.
struct LogMessage
{
    LogLevel level{LogLevel::Info};
    std::string_view module_name;
    std::string_view category;
    std::string_view message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::thread::id thread_id{std::this_thread::get_id()};
    std::string_view thread_name;
};

// Abstract logging sink used by application services, runtime wiring, and tests.
class ILogger
{
  public:
    virtual ~ILogger() = default;

    // Writes a fully prepared log message.
    // Input: structured message payload including timestamp and thread metadata.
    // Output: none.
    // Relationship: all convenience overloads funnel into this virtual method.
    virtual void Log(const LogMessage &message) = 0;

    // Builds a structured message from individual fields and forwards it to Log(LogMessage).
    // Input: severity, module name, optional category, and final message text.
    // Output: none.
    void Log(LogLevel level, std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogMessage{level, module_name, category, message, std::chrono::system_clock::now(), std::this_thread::get_id(),
                       GetCurrentThreadName()});
    }

    // Convenience overload for uncategorized log entries.
    void Log(LogLevel level, std::string_view module_name, std::string_view message)
    {
        Log(level, module_name, {}, message);
    }

    // Emits a trace-level message with an explicit category.
    void Trace(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Trace, module_name, category, message);
    }

    // Emits a trace-level message without a category.
    void Trace(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Trace, module_name, message);
    }

    // Emits a debug-level message with an explicit category.
    void Debug(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Debug, module_name, category, message);
    }

    // Emits a debug-level message without a category.
    void Debug(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Debug, module_name, message);
    }

    // Emits an info-level message with an explicit category.
    void Info(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Info, module_name, category, message);
    }

    // Emits an info-level message without a category.
    void Info(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Info, module_name, message);
    }

    // Emits a warning-level message with an explicit category.
    void Warn(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Warning, module_name, category, message);
    }

    // Emits a warning-level message without a category.
    void Warn(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Warning, module_name, message);
    }

    // Emits an error-level message with an explicit category.
    void Error(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Error, module_name, category, message);
    }

    // Emits an error-level message without a category.
    void Error(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Error, module_name, message);
    }

    // Emits a fatal-level message with an explicit category.
    void Fatal(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Fatal, module_name, category, message);
    }

    // Emits a fatal-level message without a category.
    void Fatal(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Fatal, module_name, message);
    }
};
} // namespace epidemic::diagnostics