#pragma once

#include <cstdint>

namespace epidemic::runtime
{
enum class SceneAttachmentState
{
    Detached,
    Attached,
};

enum class SceneMobility
{
    Static,
    Dynamic,
};

enum class SceneVisibilityState
{
    Visible,
    Hidden,
};

enum class SceneDirtyFlags : std::uint32_t
{
    None = 0,
    Transform = 1u << 0,
    Bounds = 1u << 1,
    Hierarchy = 1u << 2,
    Visibility = 1u << 3,
};

using SceneDirtyMask = std::uint32_t;

[[nodiscard]] constexpr SceneDirtyMask ToSceneDirtyMask(SceneDirtyFlags flag) noexcept
{
    return static_cast<SceneDirtyMask>(flag);
}

[[nodiscard]] constexpr bool HasSceneDirtyFlag(SceneDirtyMask mask, SceneDirtyFlags flag) noexcept
{
    return (mask & ToSceneDirtyMask(flag)) != 0u;
}

[[nodiscard]] constexpr SceneDirtyMask AddSceneDirtyFlag(SceneDirtyMask mask, SceneDirtyFlags flag) noexcept
{
    return mask | ToSceneDirtyMask(flag);
}

[[nodiscard]] constexpr SceneDirtyMask ClearSceneDirtyFlag(SceneDirtyMask mask, SceneDirtyFlags flag) noexcept
{
    return mask & ~ToSceneDirtyMask(flag);
}
} // namespace epidemic::runtime
