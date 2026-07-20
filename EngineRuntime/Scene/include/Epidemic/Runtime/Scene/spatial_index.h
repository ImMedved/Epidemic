#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"

#include <optional>

namespace epidemic::runtime
{
class ISpatialIndex
{
  public:
    virtual ~ISpatialIndex() = default;

    [[nodiscard]] virtual foundation::Result<void> SetLocalBounds(SceneNodeId node, const Aabb& bounds) = 0;
    [[nodiscard]] virtual std::optional<Aabb> GetLocalBounds(SceneNodeId node) const = 0;
    [[nodiscard]] virtual std::optional<Aabb> GetWorldBounds(SceneNodeId node) const = 0;
    virtual void MarkBoundsClean(SceneNodeId node) = 0;
    [[nodiscard]] virtual bool IsBoundsDirty(SceneNodeId node) const = 0;
};
} // namespace epidemic::runtime
