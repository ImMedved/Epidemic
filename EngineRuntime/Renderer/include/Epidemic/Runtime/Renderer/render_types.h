#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

#include <cstdint>
#include <functional>

namespace epidemic::runtime::renderer
{
// File note:
// Shared value types for the Renderer major. They describe render proxies, view identity,
// frame state and public descriptor data without exposing renderer implementation details.

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

enum class RenderProxyState
{
    Unregistered,
    Registered,
    ResourceMissing,
    Ready,
    Visible,
    Culled,
    DirtyTransform,
    DirtyMaterial,
    Destroyed
};

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
    Presented,
    Failed
};

struct RenderProxyDesc
{
    RuntimeObjectId owner{};
    ResourceId mesh{};
    ResourceId material{};
    SceneNodeId transform_node{};
    RenderLayer layer = RenderLayer::Opaque;
};

struct ViewDesc
{
    SceneNodeId transform_node{};
    float vertical_fov = 60.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
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
