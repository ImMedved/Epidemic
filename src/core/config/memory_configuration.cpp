#include "core/config/memory_configuration.h"

namespace epidemic::core::config
{
std::optional<std::string> MemoryConfiguration::GetString(std::string_view key) const
{
    std::scoped_lock lock(mutex_);

    const auto it = values_.find(std::string(key));
    if (it == values_.end())
    {
        return std::nullopt;
    }

    return it->second;
}

void MemoryConfiguration::SetString(std::string key, std::string value)
{
    std::scoped_lock lock(mutex_);
    values_.insert_or_assign(std::move(key), std::move(value));
}
} // namespace epidemic::core::config
