#pragma once

#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/transform.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct SceneNodeSnapshot
{
    SceneNode node{};
    Transform local_transform{};
    Transform world_transform{};
    std::optional<Aabb> local_bounds{};
    std::optional<Aabb> world_bounds{};
};

struct SceneSnapshot
{
    std::uint64_t revision = 0;
    std::vector<SceneNodeSnapshot> nodes;
};

class ISceneQuery
{
  public:
    virtual ~ISceneQuery() = default;

    [[nodiscard]] virtual std::vector<SceneNodeId> QueryAabb(const Aabb& bounds) const = 0;
    [[nodiscard]] virtual std::vector<SceneNodeId> QuerySphere(const Vec3& center, float radius) const = 0;
};

class ISceneSnapshotProvider
{
  public:
    virtual ~ISceneSnapshotProvider() = default;

    [[nodiscard]] virtual SceneSnapshot CaptureSnapshot() const = 0;
};
} // namespace epidemic::runtime
