#pragma once

#include "Epidemic/Runtime/Scene/transform.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
struct Aabb
{
    Vec3 min{};
    Vec3 max{};

    [[nodiscard]] constexpr bool operator==(const Aabb&) const noexcept = default;
};
} 
