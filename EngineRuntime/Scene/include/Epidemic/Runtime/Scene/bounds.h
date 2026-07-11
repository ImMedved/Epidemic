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
} // namespace epidemic::runtime
