#pragma once

#include "core/config/configuration.h"

#include <mutex>
#include <unordered_map>

namespace epidemic::core::config
{
class MemoryConfiguration final : public IConfiguration
{
  public:
    [[nodiscard]] std::optional<std::string> GetString(std::string_view key) const override;
    void SetString(std::string key, std::string value) override;

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> values_;
};
} // namespace epidemic::core::config
