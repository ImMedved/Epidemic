#include "scene_runtime_impl.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

#include <algorithm>

namespace epidemic::runtime
{
// Function note: Creates node.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<SceneNodeId> SceneRuntime::CreateNode()
{
    const SceneNodeId node_id{next_node_value_++};
    SceneNodeRecord record{};
    record.node.id = node_id;
    record.node.state = SceneNodeState::Detached;
    nodes_.emplace(node_id, record);
    return foundation::Result<SceneNodeId>::Success(node_id);
}

// Function note: Destroys node.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> SceneRuntime::DestroyNode(SceneNodeId node)
{
    if (!node.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("scene.invalid_node", "scene node id must be valid before destruction"));
    }

    const auto erased = nodes_.erase(node);
    if (erased == 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("scene.node_not_found", "scene node was not found for destruction"));
    }

    return foundation::Result<void>::Success();
}

// Function note: Handles exists.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SceneRuntime::Exists(SceneNodeId node) const
{
    return FindRecord(node) != nullptr;
}

// Function note: Sets transform.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> SceneRuntime::SetTransform(SceneNodeId node, const Transform& transform)
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("scene.node_not_found", "scene node was not found for transform update"));
    }

    record->transform = transform;
    record->transform_dirty = true;
    record->node.state = SceneNodeState::TransformDirty;
    return foundation::Result<void>::Success();
}

// Function note: Gets transform.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<Transform> SceneRuntime::GetTransform(SceneNodeId node) const
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return std::nullopt;
    }

    return record->transform;
}

// Function note: Marks clean.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void SceneRuntime::MarkClean(SceneNodeId node)
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

// Function note: Checks transform dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SceneRuntime::IsTransformDirty(SceneNodeId node) const
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && record->transform_dirty;
}

// Function note: Sets bounds.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> SceneRuntime::SetBounds(SceneNodeId node, const Aabb& bounds)
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("scene.node_not_found", "scene node was not found for bounds update"));
    }

    record->bounds = bounds;
    record->has_bounds = true;
    record->bounds_dirty = true;
    record->node.state = SceneNodeState::BoundsDirty;
    return foundation::Result<void>::Success();
}

// Function note: Gets bounds.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<Aabb> SceneRuntime::GetBounds(SceneNodeId node) const
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr || !record->has_bounds)
    {
        return std::nullopt;
    }

    return record->bounds;
}

// Function note: Marks bounds clean.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
void SceneRuntime::MarkBoundsClean(SceneNodeId node)
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
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

// Function note: Checks bounds dirty.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SceneRuntime::IsBoundsDirty(SceneNodeId node) const
{
    // Function note: Finds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const SceneNodeRecord* record = FindRecord(node);
    return record != nullptr && record->bounds_dirty;
}

// Function note: Handles query aabb.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles query sphere.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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

// Function note: Handles intersects aabb.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SceneRuntime::IntersectsAabb(const Aabb& left, const Aabb& right) noexcept
{
    return left.min.x <= right.max.x && left.max.x >= right.min.x &&
           left.min.y <= right.max.y && left.max.y >= right.min.y &&
           left.min.z <= right.max.z && left.max.z >= right.min.z;
}

// Function note: Handles intersects sphere.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
bool SceneRuntime::IntersectsSphere(const Aabb& bounds, const Vec3& center, float radius) noexcept
{
    const auto clamp = [](float value, float min_value, float max_value) noexcept {
        return std::max(min_value, std::min(value, max_value));
    };

    // Function note: Handles clamp.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const float closest_x = clamp(center.x, bounds.min.x, bounds.max.x);
    // Function note: Handles clamp.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const float closest_y = clamp(center.y, bounds.min.y, bounds.max.y);
    // Function note: Handles clamp.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const float closest_z = clamp(center.z, bounds.min.z, bounds.max.z);

    const float dx = center.x - closest_x;
    const float dy = center.y - closest_y;
    const float dz = center.z - closest_z;
    return (dx * dx) + (dy * dy) + (dz * dz) <= radius * radius;
}

// Function note: Finds record.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
SceneRuntime::SceneNodeRecord* SceneRuntime::FindRecord(SceneNodeId node)
{
    const auto iterator = nodes_.find(node);
    if (iterator == nodes_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

// Function note: Finds record.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
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
