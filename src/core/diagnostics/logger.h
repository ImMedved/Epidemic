#pragma once

#include <string_view>

namespace epidemic::core::diagnostics
{
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
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
    }

    return "UNKNOWN";
}

class ILogger
{
  public:
    virtual ~ILogger() = default;

    virtual void Log(LogLevel level, std::string_view category, std::string_view message) = 0;

    void Trace(std::string_view category, std::string_view message)
    {
        Log(LogLevel::Trace, category, message);
    }

    void Debug(std::string_view category, std::string_view message)
    {
        Log(LogLevel::Debug, category, message);
    }

    void Info(std::string_view category, std::string_view message)
    {
        Log(LogLevel::Info, category, message);
    }

    void Warn(std::string_view category, std::string_view message)
    {
        Log(LogLevel::Warning, category, message);
    }

    void Error(std::string_view category, std::string_view message)
    {
        Log(LogLevel::Error, category, message);
    }
};
} // namespace epidemic::core::diagnostics
