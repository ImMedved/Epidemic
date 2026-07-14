#pragma once

#include "Epidemic/Runtime/Scene/transform.h"

namespace epidemic::runtime
{
struct Aabb
{
    Vec3 min{};
    Vec3 max{};

    [[nodiscard]] constexpr bool operator==(const Aabb&) const noexcept = default;
};

[[nodiscard]] inline bool IsValidAabb(const Aabb& bounds) noexcept
{
    return IsFinite(bounds.min) && IsFinite(bounds.max) && bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
           bounds.min.z <= bounds.max.z;
}

[[nodiscard]] constexpr Aabb TranslateBounds(const Aabb& bounds, Vec3 offset) noexcept
{
    return Aabb{bounds.min + offset, bounds.max + offset};
}
} // namespace epidemic::runtime
