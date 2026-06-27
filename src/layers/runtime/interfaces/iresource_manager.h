#pragma once

#include <string_view>

namespace epidemic::layers::runtime
{
class IResourceManager
{
  public:
    virtual ~IResourceManager() = default;

    // Reports whether a resource id can currently be resolved by the manager.
    [[nodiscard]] virtual bool HasResource(std::string_view resource_id) const = 0;
};
} // namespace epidemic::layers::runtime
