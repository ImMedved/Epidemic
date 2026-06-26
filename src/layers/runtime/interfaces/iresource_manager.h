#pragma once

#include <string_view>

namespace epidemic::layers::runtime
{
class IResourceManager
{
  public:
    virtual ~IResourceManager() = default;

    [[nodiscard]] virtual bool HasResource(std::string_view resource_id) const = 0;
};
} // namespace epidemic::layers::runtime
