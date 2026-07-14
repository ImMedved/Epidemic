#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/transform.h"

#include <optional>

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class ITransformRegistry
{
  public:
    virtual ~ITransformRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> SetLocalTransform(SceneNodeId node, const Transform& transform) = 0;
    [[nodiscard]] virtual std::optional<Transform> GetLocalTransform(SceneNodeId node) const = 0;
    [[nodiscard]] virtual std::optional<Transform> GetWorldTransform(SceneNodeId node) const = 0;
    virtual void MarkTransformClean(SceneNodeId node) = 0;
    [[nodiscard]] virtual bool IsTransformDirty(SceneNodeId node) const = 0;
};
} // namespace epidemic::runtime
