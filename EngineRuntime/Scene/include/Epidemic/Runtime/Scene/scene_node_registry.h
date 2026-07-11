#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

namespace epidemic::runtime
{
class ISceneNodeRegistry
{
  public:
    virtual ~ISceneNodeRegistry() = default;

    [[nodiscard]] virtual foundation::Result<SceneNodeId> CreateNode() = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyNode(SceneNodeId node) = 0;
    [[nodiscard]] virtual bool Exists(SceneNodeId node) const = 0;
};
} // namespace epidemic::runtime
