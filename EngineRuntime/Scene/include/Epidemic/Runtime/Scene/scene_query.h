#pragma once

#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/transform.h"

#include <vector>

namespace epidemic::runtime
{
class ISceneQuery
{
  public:
    virtual ~ISceneQuery() = default;

    [[nodiscard]] virtual std::vector<SceneNodeId> QueryAabb(const Aabb& bounds) const = 0;
    [[nodiscard]] virtual std::vector<SceneNodeId> QuerySphere(const Vec3& center, float radius) const = 0;
};
} // namespace epidemic::runtime
