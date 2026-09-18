#pragma once

#include <Epidemic/Diagnostics/thread_context.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

namespace epidemic::diagnostics
{
// This file defines the minimal logging contract shared by EngineBase systems.
// Logging is observational: disabling logging or a failing sink must not change engine behavior.

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
// Unknown enum values map to "UNKNOWN" and never index external storage.
[[nodiscard]] inline std::string_view ToString(LogLevel level) noexcept
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

// Captures one synchronous logging request. The text fields are borrowed for the duration of Log/Write;
// thread_name is owned so thread-context lookup never leaves a dangling reference in the payload.
struct LogMessage
{
    LogLevel level{LogLevel::Info};
    std::string_view module_name;
    std::string_view category;
    std::string_view message;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::thread::id thread_id{std::this_thread::get_id()};
    std::string thread_name;
};

// Globally enables or disables logging dispatch. The flag controls observation only and is thread-safe.
void SetLoggingEnabled(bool enabled) noexcept;

// Returns whether logging dispatch is enabled.
[[nodiscard]] bool IsLoggingEnabled() noexcept;

// Abstract logging sink used by application services, runtime wiring, and tests.
// Public logging entry points are noexcept containment boundaries. A concrete sink may throw from Write,
// but the failure is swallowed before it can affect authoritative engine state or replace an original error.
class ILogger
{
  public:
    virtual ~ILogger() = default;

    // Writes a fully prepared log message if logging is enabled. Sink failures are contained.
    void Log(const LogMessage &message) noexcept
    {
        if (!IsLoggingEnabled())
        {
            return;
        }

        try
        {
            Write(message);
        }
        catch (...)
        {
        }
    }

    // Builds a structured message from individual fields and forwards it to the contained sink boundary.
    void Log(LogLevel level, std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        if (!IsLoggingEnabled())
        {
            return;
        }

        try
        {
            Log(LogMessage{level, module_name, category, message, std::chrono::system_clock::now(),
                           std::this_thread::get_id(), GetCurrentThreadName()});
        }
        catch (...)
        {
        }
    }

    // Convenience overload for uncategorized log entries.
    void Log(LogLevel level, std::string_view module_name, std::string_view message) noexcept
    {
        Log(level, module_name, {}, message);
    }

    void Trace(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Trace, module_name, category, message);
    }

    void Trace(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Trace, module_name, message);
    }

    void Debug(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Debug, module_name, category, message);
    }

    void Debug(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Debug, module_name, message);
    }

    void Info(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Info, module_name, category, message);
    }

    void Info(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Info, module_name, message);
    }

    void Warn(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Warning, module_name, category, message);
    }

    void Warn(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Warning, module_name, message);
    }

    void Error(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Error, module_name, category, message);
    }

    void Error(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Error, module_name, message);
    }

    void Fatal(std::string_view module_name, std::string_view category, std::string_view message) noexcept
    {
        Log(LogLevel::Fatal, module_name, category, message);
    }

    void Fatal(std::string_view module_name, std::string_view message) noexcept
    {
        Log(LogLevel::Fatal, module_name, message);
    }

  private:
    // Concrete sinks implement only the side effect. Exceptions are contained by ILogger::Log.
    virtual void Write(const LogMessage &message) = 0;
};
} // namespace epidemic::diagnostics
