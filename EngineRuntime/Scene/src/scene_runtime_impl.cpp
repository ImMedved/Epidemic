#include "scene_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
foundation::Result<SceneNodeId> SceneRuntime::CreateNode()
{
    const SceneNodeId node_id{next_node_value_++};
    SceneNodeRecord record{};
    record.node.id = node_id;
    record.node.state = SceneNodeState::Detached;
    nodes_.emplace(node_id, record);
    return foundation::Result<SceneNodeId>::Success(node_id);
}

foundation::Result<void> SceneRuntime::DestroyNode(SceneNodeId node)
{
    if (!node.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("scene.invalid_node", "scene node id must be valid before destruction"));
    }

    const auto erased = nodes_.erase(node);
    if (erased == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("scene.node_not_found", "scene node was not found for destruction"));
    }

    return foundation::Result<void>::Success();
}

bool SceneRuntime::Exists(SceneNodeId node) const
{
    return FindRecord(node) != nullptr;
}

foundation::Result<void> SceneRuntime::SetTransform(SceneNodeId node, const Transform& transform)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("scene.node_not_found", "scene node was not found for transform update"));
    }

    record->transform = transform;
    record->transform_dirty = true;
    record->node.state = SceneNodeState::TransformDirty;
    return foundation::Result<void>::Success();
}

std::optional<Transform> SceneRuntime::GetTransform(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return std::nullopt;
    }

    return record->transform;
}

void SceneRuntime::MarkClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return;
    }

    record->transform_dirty = false;
    if (record->node.state == SceneNodeState::TransformDirty)
    {
        record->node.state = SceneNodeState::Detached;
    }
}

bool SceneRuntime::IsTransformDirty(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && record->transform_dirty;
}

foundation::Result<void> SceneRuntime::SetBounds(SceneNodeId node, const Aabb& bounds)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("scene.node_not_found", "scene node was not found for bounds update"));
    }

    record->bounds = bounds;
    record->has_bounds = true;
    record->bounds_dirty = true;
    record->node.state = SceneNodeState::BoundsDirty;
    return foundation::Result<void>::Success();
}

std::optional<Aabb> SceneRuntime::GetBounds(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr || !record->has_bounds)
    {
        return std::nullopt;
    }

    return record->bounds;
}

void SceneRuntime::MarkBoundsClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return;
    }

    record->bounds_dirty = false;
    if (record->node.state == SceneNodeState::BoundsDirty)
    {
        record->node.state = SceneNodeState::Detached;
    }
}

bool SceneRuntime::IsBoundsDirty(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && record->bounds_dirty;
}

std::vector<SceneNodeId> SceneRuntime::QueryAabb(const Aabb& bounds) const
{
    std::vector<SceneNodeId> matches;
    for (const auto& [node_id, record] : nodes_)
    {
        if (record.has_bounds && IntersectsAabb(record.bounds, bounds))
        {
            matches.push_back(node_id);
        }
    }

    return matches;
}

std::vector<SceneNodeId> SceneRuntime::QuerySphere(const Vec3& center, float radius) const
{
    std::vector<SceneNodeId> matches;
    for (const auto& [node_id, record] : nodes_)
    {
        if (record.has_bounds && IntersectsSphere(record.bounds, center, radius))
        {
            matches.push_back(node_id);
        }
    }

    return matches;
}

bool SceneRuntime::IntersectsAabb(const Aabb& left, const Aabb& right) noexcept
{
    return left.min.x <= right.max.x && left.max.x >= right.min.x &&
           left.min.y <= right.max.y && left.max.y >= right.min.y &&
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
} // namespace epidemic::runtime
