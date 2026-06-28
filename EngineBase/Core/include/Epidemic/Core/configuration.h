#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace epidemic::core::config
{
class IConfiguration
{
  public:
    virtual ~IConfiguration() = default;

    [[nodiscard]] virtual std::optional<std::string> GetString(std::string_view key) const = 0;
    virtual void SetString(std::string key, std::string value) = 0;
};
} // namespace epidemic::core::config