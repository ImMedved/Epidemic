#pragma once

#include <Epidemic/Diagnostics/thread_context.h>

#include <chrono>
#include <string_view>
#include <thread>

namespace epidemic::diagnostics
{
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

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

class ILogger
{
  public:
    virtual ~ILogger() = default;

    virtual void Log(const LogMessage &message) = 0;

    void Log(LogLevel level, std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogMessage{level, module_name, category, message, std::chrono::system_clock::now(), std::this_thread::get_id(),
                       GetCurrentThreadName()});
    }

    void Log(LogLevel level, std::string_view module_name, std::string_view message)
    {
        Log(level, module_name, {}, message);
    }

    void Trace(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Trace, module_name, category, message);
    }

    void Trace(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Trace, module_name, message);
    }

    void Debug(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Debug, module_name, category, message);
    }

    void Debug(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Debug, module_name, message);
    }

    void Info(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Info, module_name, category, message);
    }

    void Info(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Info, module_name, message);
    }

    void Warn(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Warning, module_name, category, message);
    }

    void Warn(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Warning, module_name, message);
    }

    void Error(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Error, module_name, category, message);
    }

    void Error(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Error, module_name, message);
    }

    void Fatal(std::string_view module_name, std::string_view category, std::string_view message)
    {
        Log(LogLevel::Fatal, module_name, category, message);
    }

    void Fatal(std::string_view module_name, std::string_view message)
    {
        Log(LogLevel::Fatal, module_name, message);
    }
};
} // namespace epidemic::diagnostics
