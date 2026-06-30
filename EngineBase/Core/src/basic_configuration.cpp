#include <Epidemic/Core/basic_configuration.h>

namespace epidemic::core::config
{
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

void BasicConfiguration::SetString(std::string key, std::string value)
{
    std::scoped_lock lock(mutex_);
    string_values_.insert_or_assign(std::move(key), std::move(value));
}

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

void BasicConfiguration::SetInt(std::string key, std::int64_t value)
{
    std::scoped_lock lock(mutex_);
    int_values_.insert_or_assign(std::move(key), value);
}

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

void BasicConfiguration::SetBool(std::string key, bool value)
{
    std::scoped_lock lock(mutex_);
    bool_values_.insert_or_assign(std::move(key), value);
}
} // namespace epidemic::core::config
