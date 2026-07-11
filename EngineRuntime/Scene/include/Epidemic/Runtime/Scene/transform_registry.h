#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/transform.h"

#include <optional>

namespace epidemic::runtime
{
class ITransformRegistry
{
  public:
    virtual ~ITransformRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> SetTransform(SceneNodeId node, const Transform& transform) = 0;
    [[nodiscard]] virtual std::optional<Transform> GetTransform(SceneNodeId node) const = 0;
    virtual void MarkClean(SceneNodeId node) = 0;
    [[nodiscard]] virtual bool IsTransformDirty(SceneNodeId node) const = 0;
};
} // namespace epidemic::runtime
