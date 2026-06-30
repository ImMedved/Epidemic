#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <cstdint>

namespace epidemic::core::config
{
class IConfiguration
{
  public:
    virtual ~IConfiguration() = default;

    [[nodiscard]] virtual std::optional<std::string> GetString(std::string_view key) const = 0;
    virtual void SetString(std::string key, std::string value) = 0;

    [[nodiscard]] virtual std::optional<std::int64_t> GetInt(std::string_view key) const = 0;
    virtual void SetInt(std::string key, std::int64_t value) = 0;

    [[nodiscard]] virtual std::optional<bool> GetBool(std::string_view key) const = 0;
    virtual void SetBool(std::string key, bool value) = 0;

    [[nodiscard]] static constexpr std::string_view RuntimeNameKey() noexcept
    {
        return "runtime.name";
    }

    [[nodiscard]] static constexpr std::string_view WorkerCountKey() noexcept
    {
        return "core.worker_count";
    }

    [[nodiscard]] static constexpr std::string_view MemoryTrackingEnabledKey() noexcept
    {
        return "memory.tracking_enabled";
    }

    [[nodiscard]] static constexpr std::string_view RhiDebugEnabledKey() noexcept
    {
        return "rhi.debug_enabled";
    }

    [[nodiscard]] static constexpr std::string_view DefaultWindowWidthKey() noexcept
    {
        return "window.default_width";
    }

    [[nodiscard]] static constexpr std::string_view DefaultWindowHeightKey() noexcept
    {
        return "window.default_height";
    }

    [[nodiscard]] std::optional<std::string> GetRuntimeName() const
    {
        return GetString(RuntimeNameKey());
    }

    void SetRuntimeName(std::string runtime_name)
    {
        SetString(std::string(RuntimeNameKey()), std::move(runtime_name));
    }

    [[nodiscard]] std::optional<std::size_t> GetWorkerCount() const
    {
        const auto value = GetInt(WorkerCountKey());
        if (!value.has_value() || *value < 0)
        {
            return std::nullopt;
        }

        return static_cast<std::size_t>(*value);
    }

    void SetWorkerCount(std::size_t worker_count)
    {
        SetInt(std::string(WorkerCountKey()), static_cast<std::int64_t>(worker_count));
    }

    [[nodiscard]] std::optional<bool> GetMemoryTrackingEnabled() const
    {
        return GetBool(MemoryTrackingEnabledKey());
    }

    void SetMemoryTrackingEnabled(bool enabled)
    {
        SetBool(std::string(MemoryTrackingEnabledKey()), enabled);
    }

    [[nodiscard]] std::optional<bool> GetRhiDebugEnabled() const
    {
        return GetBool(RhiDebugEnabledKey());
    }

    void SetRhiDebugEnabled(bool enabled)
    {
        SetBool(std::string(RhiDebugEnabledKey()), enabled);
    }

    [[nodiscard]] std::optional<std::int32_t> GetDefaultWindowWidth() const
    {
        const auto value = GetInt(DefaultWindowWidthKey());
        if (!value.has_value())
        {
            return std::nullopt;
        }

        return static_cast<std::int32_t>(*value);
    }

    void SetDefaultWindowWidth(std::int32_t width)
    {
        SetInt(std::string(DefaultWindowWidthKey()), width);
    }

    [[nodiscard]] std::optional<std::int32_t> GetDefaultWindowHeight() const
    {
        const auto value = GetInt(DefaultWindowHeightKey());
        if (!value.has_value())
        {
            return std::nullopt;
        }

        return static_cast<std::int32_t>(*value);
    }

    void SetDefaultWindowHeight(std::int32_t height)
    {
        SetInt(std::string(DefaultWindowHeightKey()), height);
    }
};
} // namespace epidemic::core::config
