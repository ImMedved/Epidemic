#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

#include <cstdint>
#include <functional>

namespace epidemic::runtime::renderer
{
struct RenderProxyId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RenderProxyId&) const noexcept = default;
};

struct ViewId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const ViewId&) const noexcept = default;
};

enum class RenderProxyLifecycle
{
    Unregistered,
    Registered,
    DestroyPending,
    Destroyed,
};

enum class RenderProxyReadiness
{
    MissingResources,
    Ready,
};

enum class RenderProxyVisibility
{
    Hidden,
    Visible,
    Culled,
};

enum class RenderProxyDirtyFlags : std::uint32_t
{
    None = 0,
    Transform = 1u << 0,
    Material = 1u << 1,
    Visibility = 1u << 2,
};

using RenderProxyDirtyMask = std::uint32_t;

[[nodiscard]] constexpr RenderProxyDirtyMask ToRenderDirtyMask(RenderProxyDirtyFlags flag) noexcept
{
    return static_cast<RenderProxyDirtyMask>(flag);
}

[[nodiscard]] constexpr bool HasRenderDirtyFlag(RenderProxyDirtyMask mask, RenderProxyDirtyFlags flag) noexcept
{
    return (mask & ToRenderDirtyMask(flag)) != 0u;
}

[[nodiscard]] constexpr RenderProxyDirtyMask AddRenderDirtyFlag(RenderProxyDirtyMask mask, RenderProxyDirtyFlags flag) noexcept
{
    return mask | ToRenderDirtyMask(flag);
}

[[nodiscard]] constexpr RenderProxyDirtyMask ClearRenderDirtyFlag(RenderProxyDirtyMask mask, RenderProxyDirtyFlags flag) noexcept
{
    return mask & ~ToRenderDirtyMask(flag);
}

enum class RenderLayer
{
    Opaque,
    Transparent,
    Sky,
    Water,
    Debug
};

enum class RenderFrameState
{
    NotPrepared,
    Preparing,
    ReadyToRender,
    Rendering,
    Submitted,
    Failed
};

enum class ViewLifecycle
{
    Active,
    DestroyPending,
    Destroyed,
};

struct RenderProxyDesc
{
    RuntimeObjectId owner{};
    ResourceId mesh{};
    ResourceId material{};
    SceneNodeId transform_node{};
    RenderLayer layer = RenderLayer::Opaque;
    RenderProxyVisibility visibility = RenderProxyVisibility::Visible;
};

struct ViewDesc
{
    SceneNodeId transform_node{};
    float vertical_fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
};

struct RenderResourcePayloads
{
    ResourcePayloadPtr mesh{};
    ResourcePayloadPtr material{};
};
} // namespace epidemic::runtime::renderer

namespace std
{
template <> struct hash<epidemic::runtime::renderer::RenderProxyId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::renderer::RenderProxyId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::renderer::ViewId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::renderer::ViewId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
