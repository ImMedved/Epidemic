#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include <optional>

namespace epidemic::runtime
{
struct RegionDescriptor
{
    RegionId id{};
    foundation::StringId name{};

    [[nodiscard]] constexpr bool operator==(const RegionDescriptor&) const noexcept = default;
};

class IRegionRegistry
{
  public:
    virtual ~IRegionRegistry() = default;
    [[nodiscard]] virtual foundation::Result<void> RegisterRegion(RegionDescriptor region) = 0;
    [[nodiscard]] virtual std::optional<RegionDescriptor> FindRegion(RegionId id) const = 0;
    virtual void Freeze() noexcept = 0;
    [[nodiscard]] virtual bool IsFrozen() const noexcept = 0;
};
}
