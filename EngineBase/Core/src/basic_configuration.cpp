#include <Epidemic/Core/basic_configuration.h>

namespace epidemic::core::config
{
// This file implements the default in-memory configuration store.
// The methods are thin locked wrappers around typed maps declared in basic_configuration.h.

// Returns the stored string value for the requested key.
std::optional<std::string> BasicConfiguration::GetString(std::string_view key) const
{
    std::scoped_lock lock(mutex_);

    const auto it = string_values_.find(std::string(key));
    if (it == string_values_.end())
    {
        return std::nullopt;
    }

    return it->second;
}

// Stores or replaces a string value.
void BasicConfiguration::SetString(std::string key, std::string value)
{
    std::scoped_lock lock(mutex_);
    string_values_.insert_or_assign(std::move(key), std::move(value));
}

// Returns the stored integer value for the requested key.
std::optional<std::int64_t> BasicConfiguration::GetInt(std::string_view key) const
{
    std::scoped_lock lock(mutex_);

    const auto it = int_values_.find(std::string(key));
    if (it == int_values_.end())
    {
        return std::nullopt;
    }

    return it->second;
}

// Stores or replaces an integer value.
void BasicConfiguration::SetInt(std::string key, std::int64_t value)
{
    std::scoped_lock lock(mutex_);
    int_values_.insert_or_assign(std::move(key), value);
}

// Returns the stored boolean value for the requested key.
std::optional<bool> BasicConfiguration::GetBool(std::string_view key) const
{
    std::scoped_lock lock(mutex_);

    const auto it = bool_values_.find(std::string(key));
    if (it == bool_values_.end())
    {
        return std::nullopt;
    }

    return it->second;
}

// Stores or replaces a boolean value.
void BasicConfiguration::SetBool(std::string key, bool value)
{
    std::scoped_lock lock(mutex_);
    bool_values_.insert_or_assign(std::move(key), value);
}
} // namespace epidemic::core::config
