#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/spatial.h"
#include "Epidemic/Runtime/Renderer/render_types.h"

namespace epidemic::runtime::renderer
{
struct RenderTransformSnapshot
{
    RenderTransformId node{};
    Transform world_transform{};
    std::uint64_t revision = 0;
};

class IRenderResourceBridge
{
  public:
    virtual ~IRenderResourceBridge() = default;

    [[nodiscard]] virtual foundation::Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) = 0;
    virtual void ReleasePayloads(ResourceId mesh, ResourceId material) = 0;
    [[nodiscard]] virtual foundation::Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const = 0;
};

class IRenderSceneSource
{
  public:
    virtual ~IRenderSceneSource() = default;

    [[nodiscard]] virtual foundation::Result<RenderTransformSnapshot> GetTransformSnapshot(RenderTransformId node) const = 0;
};
} // namespace epidemic::runtime::renderer
