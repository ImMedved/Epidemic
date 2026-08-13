#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/spatial.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

#include <memory>
#include <vector>

namespace epidemic::runtime::renderer
{
struct RenderTransformSnapshot
{
    RenderTransformId node{};
    Transform world_transform{};
    std::uint64_t revision = 0;
};

struct RenderPoseBuffer
{
    RuntimeObjectId owner{};
    std::vector<Transform> bone_transforms{};
    std::uint64_t revision = 0;
};

class IRenderPoseSource
{
  public:
    virtual ~IRenderPoseSource() = default;

    [[nodiscard]] virtual foundation::Result<std::shared_ptr<const RenderPoseBuffer>>
        GetPose(RuntimeObjectId owner) const = 0;
};

class IRenderResourceBridge
{
  public:
    virtual ~IRenderResourceBridge() = default;

    [[nodiscard]] virtual foundation::Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) = 0;
    [[nodiscard]] virtual foundation::Result<void> ReleasePayloads(ResourceId mesh, ResourceId material) = 0;
    [[nodiscard]] virtual foundation::Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const = 0;
};

class IRenderSceneSource
{
  public:
    virtual ~IRenderSceneSource() = default;

    [[nodiscard]] virtual foundation::Result<RenderTransformSnapshot> GetTransformSnapshot(RenderTransformId node) const = 0;
};

struct RenderProxySubmission
{
    RenderProxyId proxy{};
    RenderTransformSnapshot transform{};
    RenderResourcePayloads payloads{};
    std::shared_ptr<const RenderPoseBuffer> pose{};
    RenderLayer layer = RenderLayer::Opaque;
    RenderProxyVisibility visibility = RenderProxyVisibility::Visible;
};

struct RenderFrameContext
{
    ViewId view{};
    ViewDesc desc{};
    RenderTransformSnapshot transform{};
};

class IRenderCommandSink
{
  public:
    virtual ~IRenderCommandSink() = default;

    [[nodiscard]] virtual foundation::Result<void> BeginFrame(const RenderFrameContext& context) = 0;
    [[nodiscard]] virtual foundation::Result<void> SubmitProxy(const RenderProxySubmission& submission) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndFrame() = 0;
    [[nodiscard]] virtual foundation::Result<void> AbortFrame() = 0;
};
} // namespace epidemic::runtime::renderer
