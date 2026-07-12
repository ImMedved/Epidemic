#pragma once

#include "Epidemic/Runtime/Renderer/render_types.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

namespace epidemic::runtime::renderer
{
// File note:
// Adapter contracts used by Renderer to query resource readiness and scene-node presence
// without owning resource loading or transform storage.
class IRenderResourceBridge
{
  public:
    virtual ~IRenderResourceBridge() = default;

    [[nodiscard]] virtual ResourceState GetResourceState(ResourceId id) const = 0;
};

class IRenderSceneSource
{
  public:
    virtual ~IRenderSceneSource() = default;

    [[nodiscard]] virtual bool HasNode(SceneNodeId node) const = 0;
};
} // namespace epidemic::runtime::renderer
