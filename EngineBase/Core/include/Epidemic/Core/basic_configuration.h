#pragma once

#include <Epidemic/Core/configuration.h>

#include <mutex>
#include <string>
#include <unordered_map>

namespace epidemic::core::config
{
class BasicConfiguration final : public IConfiguration
{
  public:
    [[nodiscard]] std::optional<std::string> GetString(std::string_view key) const override;
    void SetString(std::string key, std::string value) override;

    [[nodiscard]] std::optional<std::int64_t> GetInt(std::string_view key) const override;
    void SetInt(std::string key, std::int64_t value) override;

    [[nodiscard]] std::optional<bool> GetBool(std::string_view key) const override;
    void SetBool(std::string key, bool value) override;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> string_values_;
    std::unordered_map<std::string, std::int64_t> int_values_;
    std::unordered_map<std::string, bool> bool_values_;
};
} // namespace epidemic::core::config
