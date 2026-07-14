#pragma once

#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

#include <unordered_map>
#include <vector>

namespace epidemic::runtime
{
class SceneRuntime final : public ISceneNodeRegistry,
                           public ITransformRegistry,
                           public ISpatialIndex,
                           public ISceneQuery,
                           public ISceneSnapshotProvider
{
  public:
    [[nodiscard]] foundation::Result<SceneNodeId> CreateNode() override;
    [[nodiscard]] foundation::Result<void> DestroyNode(SceneNodeId node) override;
    [[nodiscard]] bool Exists(SceneNodeId node) const override;
    [[nodiscard]] std::optional<SceneNode> GetNode(SceneNodeId node) const override;
    [[nodiscard]] foundation::Result<void> AttachNode(SceneNodeId child, SceneNodeId parent) override;
    [[nodiscard]] foundation::Result<void> DetachNode(SceneNodeId child) override;
    [[nodiscard]] std::optional<SceneNodeId> GetParent(SceneNodeId child) const override;
    [[nodiscard]] std::vector<SceneNodeId> GetChildren(SceneNodeId parent) const override;
    [[nodiscard]] foundation::Result<void> SetMobility(SceneNodeId node, SceneMobility mobility) override;
    [[nodiscard]] foundation::Result<void> SetVisibility(SceneNodeId node, SceneVisibilityState visibility) override;
    [[nodiscard]] std::uint64_t GetRevision() const override;

    [[nodiscard]] foundation::Result<void> SetLocalTransform(SceneNodeId node, const Transform& transform) override;
    [[nodiscard]] std::optional<Transform> GetLocalTransform(SceneNodeId node) const override;
    [[nodiscard]] std::optional<Transform> GetWorldTransform(SceneNodeId node) const override;
    void MarkTransformClean(SceneNodeId node) override;
    [[nodiscard]] bool IsTransformDirty(SceneNodeId node) const override;

    [[nodiscard]] foundation::Result<void> SetLocalBounds(SceneNodeId node, const Aabb& bounds) override;
    [[nodiscard]] std::optional<Aabb> GetLocalBounds(SceneNodeId node) const override;
    [[nodiscard]] std::optional<Aabb> GetWorldBounds(SceneNodeId node) const override;
    void MarkBoundsClean(SceneNodeId node) override;
    [[nodiscard]] bool IsBoundsDirty(SceneNodeId node) const override;

    [[nodiscard]] std::vector<SceneNodeId> QueryAabb(const Aabb& bounds) const override;
    [[nodiscard]] std::vector<SceneNodeId> QuerySphere(const Vec3& center, float radius) const override;
    [[nodiscard]] SceneSnapshot CaptureSnapshot() const override;

  private:
    struct SceneNodeRecord
    {
        SceneNode node{};
        std::vector<SceneNodeId> children;
        Transform local_transform{};
        Aabb local_bounds{};
        bool has_bounds = false;
    };

    [[nodiscard]] static bool IntersectsAabb(const Aabb& left, const Aabb& right) noexcept;
    [[nodiscard]] static bool IntersectsSphere(const Aabb& bounds, const Vec3& center, float radius) noexcept;
    [[nodiscard]] SceneNodeRecord* FindRecord(SceneNodeId node);
    [[nodiscard]] const SceneNodeRecord* FindRecord(SceneNodeId node) const;
    [[nodiscard]] foundation::Result<void> RequireNode(SceneNodeId node, const char* message) const;
    [[nodiscard]] bool WouldCreateCycle(SceneNodeId child, SceneNodeId parent) const;
    [[nodiscard]] Transform ComputeWorldTransform(SceneNodeId node) const;
    [[nodiscard]] std::optional<Aabb> ComputeWorldBounds(SceneNodeId node) const;
    void MarkSubtreeDirty(SceneNodeId node, SceneDirtyMask flags);
    void BumpRevision(SceneNodeRecord& record, SceneDirtyMask flags);
    void RemoveChild(SceneNodeId parent, SceneNodeId child);
    [[nodiscard]] std::vector<SceneNodeId> SortedNodeIds() const;

    std::unordered_map<SceneNodeId, SceneNodeRecord> nodes_;
    std::uint64_t next_node_value_ = 1;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime
