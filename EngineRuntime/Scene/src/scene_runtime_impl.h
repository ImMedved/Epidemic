#pragma once

#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <unordered_map>

namespace epidemic::runtime
{
class SceneRuntime final : public ISceneNodeRegistry, public ITransformRegistry, public ISpatialIndex, public ISceneQuery
{
  public:
    // Function note: Creates node.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<SceneNodeId> CreateNode() override;
    // Function note: Destroys node.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> DestroyNode(SceneNodeId node) override;
    // Function note: Handles exists.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool Exists(SceneNodeId node) const override;

    // Function note: Sets transform.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> SetTransform(SceneNodeId node, const Transform& transform) override;
    // Function note: Gets transform.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<Transform> GetTransform(SceneNodeId node) const override;
    // Function note: Marks clean.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void MarkClean(SceneNodeId node) override;
    // Function note: Checks transform dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsTransformDirty(SceneNodeId node) const override;

    // Function note: Sets bounds.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] foundation::Result<void> SetBounds(SceneNodeId node, const Aabb& bounds) override;
    // Function note: Gets bounds.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::optional<Aabb> GetBounds(SceneNodeId node) const override;
    // Function note: Marks bounds clean.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    void MarkBoundsClean(SceneNodeId node) override;
    // Function note: Checks bounds dirty.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] bool IsBoundsDirty(SceneNodeId node) const override;

    // Function note: Handles query aabb.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] std::vector<SceneNodeId> QueryAabb(const Aabb& bounds) const override;
    // Function note: Handles query sphere.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

    // Function note: Handles intersects aabb.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] static bool IntersectsAabb(const Aabb& left, const Aabb& right) noexcept;
    // Function note: Handles intersects sphere.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] static bool IntersectsSphere(const Aabb& bounds, const Vec3& center, float radius) noexcept;
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] SceneNodeRecord* FindRecord(SceneNodeId node);
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] const SceneNodeRecord* FindRecord(SceneNodeId node) const;

    std::unordered_map<SceneNodeId, SceneNodeRecord> nodes_;
    std::uint64_t next_node_value_ = 1;
};
} 
