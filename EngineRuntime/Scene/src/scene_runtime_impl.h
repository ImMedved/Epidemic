#pragma once

#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class SceneRuntime final : public ISceneNodeRegistry, public ITransformRegistry, public ISpatialIndex, public ISceneQuery
{
  public:
    [[nodiscard]] foundation::Result<SceneNodeId> CreateNode() override;
    [[nodiscard]] foundation::Result<void> DestroyNode(SceneNodeId node) override;
    [[nodiscard]] bool Exists(SceneNodeId node) const override;

    [[nodiscard]] foundation::Result<void> SetTransform(SceneNodeId node, const Transform& transform) override;
    [[nodiscard]] std::optional<Transform> GetTransform(SceneNodeId node) const override;
    void MarkClean(SceneNodeId node) override;
    [[nodiscard]] bool IsTransformDirty(SceneNodeId node) const override;

    [[nodiscard]] foundation::Result<void> SetBounds(SceneNodeId node, const Aabb& bounds) override;
    [[nodiscard]] std::optional<Aabb> GetBounds(SceneNodeId node) const override;
    void MarkBoundsClean(SceneNodeId node) override;
    [[nodiscard]] bool IsBoundsDirty(SceneNodeId node) const override;

    [[nodiscard]] std::vector<SceneNodeId> QueryAabb(const Aabb& bounds) const override;
    [[nodiscard]] std::vector<SceneNodeId> QuerySphere(const Vec3& center, float radius) const override;

  protected:
    struct SceneNodeRecord
    {
        SceneNode node{};
        Transform transform{};
        Aabb bounds{};
        bool has_bounds = false;
        bool transform_dirty = false;
        bool bounds_dirty = false;
    };

    [[nodiscard]] static bool IntersectsAabb(const Aabb& left, const Aabb& right) noexcept;
    [[nodiscard]] static bool IntersectsSphere(const Aabb& bounds, const Vec3& center, float radius) noexcept;
    [[nodiscard]] SceneNodeRecord* FindRecord(SceneNodeId node);
    [[nodiscard]] const SceneNodeRecord* FindRecord(SceneNodeId node) const;

    std::unordered_map<SceneNodeId, SceneNodeRecord> nodes_;
    std::uint64_t next_node_value_ = 1;
};
} // namespace epidemic::runtime
