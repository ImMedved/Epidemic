#include "scene_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> SceneFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool NodeLess(SceneNodeId left, SceneNodeId right) noexcept
{
    return left.Raw() < right.Raw();
}

[[nodiscard]] Quat Conjugate(Quat value) noexcept
{
    return Quat{-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] Vec3 Divide(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x / right.x, left.y / right.y, left.z / right.z};
}

[[nodiscard]] bool IsInvertibleScale(Vec3 scale) noexcept
{
    return std::abs(scale.x) > kSpatialEpsilon && std::abs(scale.y) > kSpatialEpsilon && std::abs(scale.z) > kSpatialEpsilon;
}
} // namespace

foundation::Result<SceneNodeId> SceneRuntime::CreateNode()
{
    const auto node_value = AllocateMonotonicId(next_node_value_, "scene.node_id_exhausted", "scene node id allocator is exhausted");
    if (!node_value)
    {
        return foundation::Result<SceneNodeId>::Failure(node_value.GetError());
    }
    const SceneNodeId node_id{node_value.Value()};
    SceneNodeRecord record{};
    record.node.id = node_id;
    BumpRevision(record, 0);
    const auto [_, inserted] = nodes_.emplace(node_id, record);
    if (!inserted)
    {
        return foundation::Result<SceneNodeId>::Failure(
            foundation::Error::Create("scene.duplicate_node_id", "allocated scene node id already exists"));
    }
    return foundation::Result<SceneNodeId>::Success(node_id);
}

foundation::Result<void> SceneRuntime::DestroyNode(SceneNodeId node)
{
    const auto required = RequireNode(node, "scene node was not found for destruction");
    if (!required)
    {
        return required;
    }

    SceneNodeRecord& record = *FindRecord(node);
    if (record.node.parent_id.IsValid())
    {
        RemoveChild(record.node.parent_id, node);
    }
    for (SceneNodeId child : record.children)
    {
        SceneNodeRecord* child_record = FindRecord(child);
        if (child_record != nullptr)
        {
            child_record->local_transform = ComputeWorldTransform(child);
            child_record->node.parent_id = {};
            child_record->node.attachment_state = SceneAttachmentState::Detached;
            MarkSubtreeDirty(child, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy) | ToSceneDirtyMask(SceneDirtyFlags::Transform) |
                                      ToSceneDirtyMask(SceneDirtyFlags::Bounds));
        }
    }

    nodes_.erase(node);
    ++revision_;
    return foundation::Result<void>::Success();
}

bool SceneRuntime::Exists(SceneNodeId node) const
{
    return FindRecord(node) != nullptr;
}

std::optional<SceneNode> SceneRuntime::GetNode(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return std::nullopt;
    }
    return record->node;
}

foundation::Result<void> SceneRuntime::AttachNode(SceneNodeId child, SceneNodeId parent, ReparentMode mode)
{
    if (child == parent)
    {
        return SceneFailure("scene.hierarchy_cycle", "scene node cannot be attached to itself");
    }
    const auto child_required = RequireNode(child, "child scene node was not found for attachment");
    if (!child_required)
    {
        return child_required;
    }
    const auto parent_required = RequireNode(parent, "parent scene node was not found for attachment");
    if (!parent_required)
    {
        return parent_required;
    }
    if (WouldCreateCycle(child, parent))
    {
        return SceneFailure("scene.hierarchy_cycle", "scene node attachment would create a cycle");
    }

    SceneNodeRecord& child_record = *FindRecord(child);
    const Transform child_world = ComputeWorldTransform(child);
    if (child_record.node.parent_id.IsValid())
    {
        RemoveChild(child_record.node.parent_id, child);
    }

    if (mode == ReparentMode::KeepWorld)
    {
        auto local_transform = ComputeLocalTransform(parent, child_world);
        if (!local_transform)
        {
            return foundation::Result<void>::Failure(local_transform.GetError());
        }
        child_record.local_transform = local_transform.Value();
    }
    child_record.node.parent_id = parent;
    child_record.node.attachment_state = SceneAttachmentState::Attached;
    FindRecord(parent)->children.push_back(child);
    MarkSubtreeDirty(child, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy) | ToSceneDirtyMask(SceneDirtyFlags::Transform) |
                              ToSceneDirtyMask(SceneDirtyFlags::Bounds));
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::DetachNode(SceneNodeId child, ReparentMode mode)
{
    const auto required = RequireNode(child, "scene node was not found for detach");
    if (!required)
    {
        return required;
    }

    SceneNodeRecord& child_record = *FindRecord(child);
    const Transform child_world = ComputeWorldTransform(child);
    if (child_record.node.parent_id.IsValid())
    {
        RemoveChild(child_record.node.parent_id, child);
    }
    if (mode == ReparentMode::KeepWorld)
    {
        child_record.local_transform = child_world;
    }
    child_record.node.parent_id = {};
    child_record.node.attachment_state = SceneAttachmentState::Detached;
    MarkSubtreeDirty(child, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy) | ToSceneDirtyMask(SceneDirtyFlags::Transform) |
                              ToSceneDirtyMask(SceneDirtyFlags::Bounds));
    return foundation::Result<void>::Success();
}

std::optional<SceneNodeId> SceneRuntime::GetParent(SceneNodeId child) const
{
    const SceneNodeRecord* record = FindRecord(child);
    if (record == nullptr || !record->node.parent_id.IsValid())
    {
        return std::nullopt;
    }
    return record->node.parent_id;
}

std::vector<SceneNodeId> SceneRuntime::GetChildren(SceneNodeId parent) const
{
    const SceneNodeRecord* record = FindRecord(parent);
    if (record == nullptr)
    {
        return {};
    }
    std::vector<SceneNodeId> children = record->children;
    std::sort(children.begin(), children.end(), NodeLess);
    return children;
}

foundation::Result<void> SceneRuntime::SetMobility(SceneNodeId node, SceneMobility mobility)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for mobility update");
    }
    record->node.mobility = mobility;
    BumpRevision(*record, 0);
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::SetVisibility(SceneNodeId node, SceneVisibilityState visibility)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for visibility update");
    }
    record->node.visibility = visibility;
    BumpRevision(*record, ToSceneDirtyMask(SceneDirtyFlags::Visibility));
    return foundation::Result<void>::Success();
}

std::uint64_t SceneRuntime::GetRevision() const
{
    return revision_;
}

foundation::Result<void> SceneRuntime::SetLocalTransform(SceneNodeId node, const Transform& transform)
{
    if (!IsValidTransform(transform))
    {
        return SceneFailure("scene.invalid_transform", "scene transform must contain finite values and non-zero scale");
    }
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for transform update");
    }

    record->local_transform = transform;
    MarkSubtreeDirty(node, ToSceneDirtyMask(SceneDirtyFlags::Transform) | ToSceneDirtyMask(SceneDirtyFlags::Bounds));
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::SetWorldTransform(SceneNodeId node, const Transform& transform, WorldTransformWriteMode mode)
{
    if (mode != WorldTransformWriteMode::RejectNonInvertibleParent)
    {
        return SceneFailure("scene.unsupported_world_transform_write_mode", "scene world transform write mode is not supported");
    }
    if (!IsValidTransform(transform))
    {
        return SceneFailure("scene.invalid_transform", "scene transform must contain finite values and non-zero scale");
    }
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for transform update");
    }

    Transform local_transform = transform;
    if (record->node.parent_id.IsValid())
    {
        auto computed = ComputeLocalTransform(record->node.parent_id, transform);
        if (!computed)
        {
            return foundation::Result<void>::Failure(computed.GetError());
        }
        local_transform = computed.Value();
        if (!IsValidTransform(local_transform))
        {
            return SceneFailure("scene.invalid_transform", "scene computed local transform is not representable as a valid TRS transform");
        }
    }

    record->local_transform = local_transform;
    MarkSubtreeDirty(node, ToSceneDirtyMask(SceneDirtyFlags::Transform) | ToSceneDirtyMask(SceneDirtyFlags::Bounds));
    return foundation::Result<void>::Success();
}

std::optional<Transform> SceneRuntime::GetLocalTransform(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return std::nullopt;
    }
    return record->local_transform;
}

std::optional<Transform> SceneRuntime::GetWorldTransform(SceneNodeId node) const
{
    if (!Exists(node))
    {
        return std::nullopt;
    }
    return ComputeWorldTransform(node);
}

void SceneRuntime::MarkTransformClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record != nullptr)
    {
        record->node.dirty_flags = ClearSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Transform);
    }
}

bool SceneRuntime::IsTransformDirty(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && HasSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Transform);
}

foundation::Result<void> SceneRuntime::SetLocalBounds(SceneNodeId node, const Aabb& bounds)
{
    if (!IsValidAabb(bounds))
    {
        return SceneFailure("scene.invalid_bounds", "scene bounds must contain finite min/max values with min <= max");
    }
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for bounds update");
    }

    record->local_bounds = bounds;
    record->has_bounds = true;
    MarkSubtreeDirty(node, ToSceneDirtyMask(SceneDirtyFlags::Bounds));
    return foundation::Result<void>::Success();
}

std::optional<Aabb> SceneRuntime::GetLocalBounds(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr || !record->has_bounds)
    {
        return std::nullopt;
    }
    return record->local_bounds;
}

std::optional<Aabb> SceneRuntime::GetWorldBounds(SceneNodeId node) const
{
    return ComputeWorldBounds(node);
}

void SceneRuntime::MarkBoundsClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record != nullptr)
    {
        record->node.dirty_flags = ClearSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Bounds);
    }
}

bool SceneRuntime::IsBoundsDirty(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && HasSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Bounds);
}

std::vector<SceneNodeId> SceneRuntime::QueryAabb(const Aabb& bounds) const
{
    if (!IsValidAabb(bounds))
    {
        return {};
    }

    std::vector<SceneNodeId> matches;
    for (SceneNodeId node_id : SortedNodeIds())
    {
        const SceneNodeRecord& record = nodes_.at(node_id);
        const auto world_bounds = ComputeWorldBounds(node_id);
        if (record.node.visibility == SceneVisibilityState::Visible && world_bounds && IntersectsAabb(*world_bounds, bounds))
        {
            matches.push_back(node_id);
        }
    }
    return matches;
}

std::vector<SceneNodeId> SceneRuntime::QuerySphere(const Vec3& center, float radius) const
{
    if (!IsFinite(center) || !std::isfinite(radius) || radius < 0.0f)
    {
        return {};
    }

    std::vector<SceneNodeId> matches;
    for (SceneNodeId node_id : SortedNodeIds())
    {
        const SceneNodeRecord& record = nodes_.at(node_id);
        const auto world_bounds = ComputeWorldBounds(node_id);
        if (record.node.visibility == SceneVisibilityState::Visible && world_bounds && IntersectsSphere(*world_bounds, center, radius))
        {
            matches.push_back(node_id);
        }
    }
    return matches;
}

SceneSnapshot SceneRuntime::CaptureSnapshot() const
{
    SceneSnapshot snapshot{};
    snapshot.revision = revision_;
    for (SceneNodeId node_id : SortedNodeIds())
    {
        const SceneNodeRecord& record = nodes_.at(node_id);
        snapshot.nodes.push_back(SceneNodeSnapshot{
            record.node.id,
            record.node.parent_id,
            record.local_transform,
            ComputeWorldTransform(node_id),
            record.has_bounds ? std::optional<Aabb>{record.local_bounds} : std::nullopt,
            ComputeWorldBounds(node_id),
            record.node.attachment_state,
            record.node.mobility,
            record.node.visibility,
            record.node.revision});
    }
    return snapshot;
}

bool SceneRuntime::IntersectsAabb(const Aabb& left, const Aabb& right) noexcept
{
    return left.min.x <= right.max.x && left.max.x >= right.min.x && left.min.y <= right.max.y && left.max.y >= right.min.y &&
           left.min.z <= right.max.z && left.max.z >= right.min.z;
}

bool SceneRuntime::IntersectsSphere(const Aabb& bounds, const Vec3& center, float radius) noexcept
{
    const auto clamp = [](float value, float min_value, float max_value) noexcept {
        return std::max(min_value, std::min(value, max_value));
    };

    const float closest_x = clamp(center.x, bounds.min.x, bounds.max.x);
    const float closest_y = clamp(center.y, bounds.min.y, bounds.max.y);
    const float closest_z = clamp(center.z, bounds.min.z, bounds.max.z);
    const float dx = center.x - closest_x;
    const float dy = center.y - closest_y;
    const float dz = center.z - closest_z;
    return (dx * dx) + (dy * dy) + (dz * dz) <= radius * radius;
}

SceneRuntime::SceneNodeRecord* SceneRuntime::FindRecord(SceneNodeId node)
{
    const auto iterator = nodes_.find(node);
    if (iterator == nodes_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

const SceneRuntime::SceneNodeRecord* SceneRuntime::FindRecord(SceneNodeId node) const
{
    const auto iterator = nodes_.find(node);
    if (iterator == nodes_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

foundation::Result<void> SceneRuntime::RequireNode(SceneNodeId node, const char* message) const
{
    if (!node.IsValid())
    {
        return SceneFailure("scene.invalid_node", "scene node id must be valid");
    }
    if (!Exists(node))
    {
        return SceneFailure("scene.node_not_found", message);
    }
    return foundation::Result<void>::Success();
}

bool SceneRuntime::WouldCreateCycle(SceneNodeId child, SceneNodeId parent) const
{
    SceneNodeId current = parent;
    while (current.IsValid())
    {
        if (current == child)
        {
            return true;
        }
        const SceneNodeRecord* record = FindRecord(current);
        if (record == nullptr)
        {
            return false;
        }
        current = record->node.parent_id;
    }
    return false;
}

Transform SceneRuntime::ComputeWorldTransform(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return {};
    }
    if (!record->node.parent_id.IsValid())
    {
        return record->local_transform;
    }
    return ComposeTransform(ComputeWorldTransform(record->node.parent_id), record->local_transform);
}

foundation::Result<Transform> SceneRuntime::ComputeLocalTransform(SceneNodeId parent, const Transform& world_transform) const
{
    const Transform parent_world = ComputeWorldTransform(parent);
    if (!IsInvertibleScale(parent_world.scale))
    {
        return foundation::Result<Transform>::Failure(
            foundation::Error::Create("scene.non_invertible_parent_transform", "parent scene transform cannot be inverted"));
    }
    const Quat inverse_parent_rotation = Conjugate(Normalize(parent_world.rotation));
    const Vec3 unrotated_position = RotateVector(inverse_parent_rotation, world_transform.position - parent_world.position);
    return foundation::Result<Transform>::Success(Transform{
        Divide(unrotated_position, parent_world.scale),
        Multiply(inverse_parent_rotation, world_transform.rotation),
        Divide(world_transform.scale, parent_world.scale),
    });
}

std::optional<Aabb> SceneRuntime::ComputeWorldBounds(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr || !record->has_bounds)
    {
        return std::nullopt;
    }
    return TransformAabb(ComputeWorldTransform(node), record->local_bounds);
}

void SceneRuntime::MarkSubtreeDirty(SceneNodeId node, SceneDirtyMask flags)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return;
    }
    BumpRevision(*record, flags);
    for (SceneNodeId child : record->children)
    {
        MarkSubtreeDirty(child, flags);
    }
}

void SceneRuntime::BumpRevision(SceneNodeRecord& record, SceneDirtyMask flags)
{
    ++revision_;
    record.node.revision = revision_;
    record.node.dirty_flags |= flags;
}

void SceneRuntime::RemoveChild(SceneNodeId parent, SceneNodeId child)
{
    SceneNodeRecord* parent_record = FindRecord(parent);
    if (parent_record == nullptr)
    {
        return;
    }
    auto& children = parent_record->children;
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    BumpRevision(*parent_record, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy));
}

std::vector<SceneNodeId> SceneRuntime::SortedNodeIds() const
{
    std::vector<SceneNodeId> ids;
    ids.reserve(nodes_.size());
    for (const auto& [node_id, record] : nodes_)
    {
        (void)record;
        ids.push_back(node_id);
    }
    std::sort(ids.begin(), ids.end(), NodeLess);
    return ids;
}
} // namespace epidemic::runtime
