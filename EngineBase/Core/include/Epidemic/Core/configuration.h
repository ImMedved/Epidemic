#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <cstdint>

namespace epidemic::core::config
{
// This file defines the abstract configuration surface consumed by EngineBase services.
// The interface exposes primitive typed storage plus a small set of well-known keys used
// during base composition, bootstrap, and window/RHI setup.

class IConfiguration
{
  public:
    virtual ~IConfiguration() = default;

    // Returns the stored string value for a key, or nullopt when the key does not exist.
    [[nodiscard]] virtual std::optional<std::string> GetString(std::string_view key) const = 0;

    // Stores or replaces a string value for a key.
    virtual void SetString(std::string key, std::string value) = 0;

    // Returns the stored integer value for a key, or nullopt when the key does not exist.
    [[nodiscard]] virtual std::optional<std::int64_t> GetInt(std::string_view key) const = 0;

    // Stores or replaces an integer value for a key.
    virtual void SetInt(std::string key, std::int64_t value) = 0;

    // Returns the stored boolean value for a key, or nullopt when the key does not exist.
    [[nodiscard]] virtual std::optional<bool> GetBool(std::string_view key) const = 0;

    // Stores or replaces a boolean value for a key.
    virtual void SetBool(std::string key, bool value) = 0;

    // Returns the canonical key for the runtime name.
    [[nodiscard]] static constexpr std::string_view RuntimeNameKey() noexcept
    {
        return "runtime.name";
    }

    // Returns the canonical key for worker-thread count.
    [[nodiscard]] static constexpr std::string_view WorkerCountKey() noexcept
    {
        return "core.worker_count";
    }

    // Returns the canonical key for memory tracking enablement.
    [[nodiscard]] static constexpr std::string_view MemoryTrackingEnabledKey() noexcept
    {
        return "memory.tracking_enabled";
    }

    // Returns the canonical key for RHI debug validation enablement.
    [[nodiscard]] static constexpr std::string_view RhiDebugEnabledKey() noexcept
    {
        return "rhi.debug_enabled";
    }

    // Returns the canonical key for the default window width.
    [[nodiscard]] static constexpr std::string_view DefaultWindowWidthKey() noexcept
    {
        return "window.default_width";
    }

    // Returns the canonical key for the default window height.
    [[nodiscard]] static constexpr std::string_view DefaultWindowHeightKey() noexcept
    {
        return "window.default_height";
    }

    // Convenience accessor for the configured runtime name.
    [[nodiscard]] std::optional<std::string> GetRuntimeName() const
    {
        return GetString(RuntimeNameKey());
    }

    // Convenience mutator for the runtime name.
    void SetRuntimeName(std::string runtime_name)
    {
        SetString(std::string(RuntimeNameKey()), std::move(runtime_name));
    }

    // Returns the configured worker count when present and non-negative.
    [[nodiscard]] std::optional<std::size_t> GetWorkerCount() const
    {
        const auto value = GetInt(WorkerCountKey());
        if (!value.has_value() || *value < 0)
        {
            return std::nullopt;
        }

        return static_cast<std::size_t>(*value);
    }

    // Convenience mutator for worker count.
    void SetWorkerCount(std::size_t worker_count)
    {
        SetInt(std::string(WorkerCountKey()), static_cast<std::int64_t>(worker_count));
    }

    // Returns whether memory tracking is enabled when the key is present.
    [[nodiscard]] std::optional<bool> GetMemoryTrackingEnabled() const
    {
        return GetBool(MemoryTrackingEnabledKey());
    }

    // Convenience mutator for memory tracking enablement.
    void SetMemoryTrackingEnabled(bool enabled)
    {
        SetBool(std::string(MemoryTrackingEnabledKey()), enabled);
    }

    // Returns whether RHI debug validation is enabled when the key is present.
    [[nodiscard]] std::optional<bool> GetRhiDebugEnabled() const
    {
        return GetBool(RhiDebugEnabledKey());
    }

    // Convenience mutator for RHI debug validation enablement.
    void SetRhiDebugEnabled(bool enabled)
    {
        SetBool(std::string(RhiDebugEnabledKey()), enabled);
    }

    // Returns the configured default window width when present.
    [[nodiscard]] std::optional<std::int32_t> GetDefaultWindowWidth() const
    {
        const auto value = GetInt(DefaultWindowWidthKey());
        if (!value.has_value())
        {
            return std::nullopt;
        }

        return static_cast<std::int32_t>(*value);
    }

    // Convenience mutator for default window width.
    void SetDefaultWindowWidth(std::int32_t width)
    {
        SetInt(std::string(DefaultWindowWidthKey()), width);
    }

    // Returns the configured default window height when present.
    [[nodiscard]] std::optional<std::int32_t> GetDefaultWindowHeight() const
    {
        const auto value = GetInt(DefaultWindowHeightKey());
        if (!value.has_value())
        {
            return std::nullopt;
        }

        return static_cast<std::int32_t>(*value);
    }

    // Convenience mutator for default window height.
    void SetDefaultWindowHeight(std::int32_t height)
    {
        SetInt(std::string(DefaultWindowHeightKey()), height);
    }
};
} // namespace epidemic::core::config