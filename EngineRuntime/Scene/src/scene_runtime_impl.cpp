#include "scene_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> SceneFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> SceneFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool NodeLess(SceneNodeId left, SceneNodeId right) noexcept
{
    return left.Raw() < right.Raw();
}

[[nodiscard]] bool IsValid(ReparentMode value) noexcept
{
    return value == ReparentMode::KeepWorld || value == ReparentMode::KeepLocal;
}

[[nodiscard]] bool IsValid(SceneMobility value) noexcept
{
    return value == SceneMobility::Static || value == SceneMobility::Dynamic;
}

[[nodiscard]] bool IsValid(SceneVisibilityState value) noexcept
{
    return value == SceneVisibilityState::Visible || value == SceneVisibilityState::Hidden;
}

[[nodiscard]] bool IsValid(WorldTransformWriteMode value) noexcept
{
    return value == WorldTransformWriteMode::RejectNonInvertibleParent;
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

[[nodiscard]] std::uint64_t NextMonotonicValue(std::uint64_t current) noexcept
{
    return current == std::numeric_limits<std::uint64_t>::max() ? 0u : current + 1u;
}

[[nodiscard]] SceneDirtyMask HierarchyTransformBoundsMask() noexcept
{
    return ToSceneDirtyMask(SceneDirtyFlags::Hierarchy) | ToSceneDirtyMask(SceneDirtyFlags::Transform) |
           ToSceneDirtyMask(SceneDirtyFlags::Bounds);
}
} // namespace

foundation::Result<SceneNodeId> SceneRuntime::CreateNode()
{
    if (next_node_value_ == 0)
    {
        return SceneFailureValue<SceneNodeId>("scene.node_id_exhausted", "scene node id allocator is exhausted");
    }
    const auto revisions = ReserveRevisionRange(1);
    if (!revisions)
    {
        return foundation::Result<SceneNodeId>::Failure(revisions.GetError());
    }

    const SceneNodeId node_id{next_node_value_};
    SceneNodeRecord record{};
    record.node.id = node_id;
    record.node.revision = revisions.Value();

    if (fail_next_allocation_for_testing_)
    {
        fail_next_allocation_for_testing_ = false;
        return SceneFailureValue<SceneNodeId>("scene.allocation_failed", "scene node storage allocation failed");
    }
    try
    {
        const auto [_, inserted] = nodes_.emplace(node_id, record);
        if (!inserted)
        {
            return SceneFailureValue<SceneNodeId>("scene.duplicate_node_id", "allocated scene node id already exists");
        }
    }
    catch (const std::exception&)
    {
        return SceneFailureValue<SceneNodeId>("scene.allocation_failed", "scene node storage allocation failed");
    }

    next_node_value_ = NextMonotonicValue(next_node_value_);
    revision_ = revisions.Value();
    return foundation::Result<SceneNodeId>::Success(node_id);
}

foundation::Result<void> SceneRuntime::DestroyNode(SceneNodeId node)
{
    const auto required = RequireNode(node, "scene node was not found for destruction");
    if (!required)
    {
        return required;
    }

    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for destruction");
    }

    std::vector<std::pair<SceneNodeId, Transform>> child_world;
    std::vector<SceneNodeId> dirty_nodes;
    std::vector<SceneNodeId> staged_parent_children;
    try
    {
        child_world.reserve(record->children.size());
        for (SceneNodeId child : record->children)
        {
            child_world.emplace_back(child, ComputeWorldTransform(child));
            auto subtree = CollectSubtree(child);
            dirty_nodes.insert(dirty_nodes.end(), subtree.begin(), subtree.end());
        }
        if (record->node.parent_id.IsValid())
        {
            const SceneNodeRecord* parent = FindRecord(record->node.parent_id);
            if (parent == nullptr)
            {
                return SceneFailure("scene.invalid_hierarchy", "scene node references a missing parent");
            }
            staged_parent_children = parent->children;
            staged_parent_children.erase(std::remove(staged_parent_children.begin(), staged_parent_children.end(), node), staged_parent_children.end());
        }
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene hierarchy staging allocation failed");
    }

    const std::size_t required_revisions = dirty_nodes.size() + (record->node.parent_id.IsValid() ? 1u : 0u) + 1u;
    const auto revisions = ReserveRevisionRange(required_revisions);
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }

    std::uint64_t next_revision = revisions.Value();
    const SceneNodeId old_parent = record->node.parent_id;
    if (old_parent.IsValid())
    {
        SceneNodeRecord* parent = FindRecord(old_parent);
        parent->children.swap(staged_parent_children);
        BumpRevisionCommitted(*parent, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy), next_revision++);
    }

    for (const auto& [child, world] : child_world)
    {
        SceneNodeRecord* child_record = FindRecord(child);
        if (child_record != nullptr)
        {
            child_record->local_transform = world;
            child_record->node.parent_id = {};
            child_record->node.attachment_state = SceneAttachmentState::Detached;
        }
    }
    MarkNodesDirty(dirty_nodes, HierarchyTransformBoundsMask(), next_revision);
    nodes_.erase(node);
    revision_ = next_revision; // destruction itself consumes the final reserved revision.
    return foundation::Result<void>::Success();
}

bool SceneRuntime::Exists(SceneNodeId node) const
{
    return FindRecord(node) != nullptr;
}

std::optional<SceneNode> SceneRuntime::GetNode(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record == nullptr ? std::nullopt : std::optional<SceneNode>{record->node};
}

foundation::Result<void> SceneRuntime::AttachNode(SceneNodeId child, SceneNodeId parent, ReparentMode mode)
{
    if (!IsValid(mode))
    {
        return SceneFailure("scene.invalid_reparent_mode", "scene reparent mode is outside the declared domain");
    }
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

    SceneNodeRecord* child_record = FindRecord(child);
    if (child_record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "child scene node was not found for attachment");
    }
    if (child_record->node.parent_id == parent)
    {
        return foundation::Result<void>::Success();
    }

    const Transform child_world = ComputeWorldTransform(child);
    Transform staged_local = child_record->local_transform;
    if (mode == ReparentMode::KeepWorld)
    {
        const auto computed = ComputeLocalTransform(parent, child_world);
        if (!computed)
        {
            return foundation::Result<void>::Failure(computed.GetError());
        }
        staged_local = computed.Value();
        if (!IsValidTransform(staged_local))
        {
            return SceneFailure("scene.invalid_transform", "scene computed local transform is not representable as a valid TRS transform");
        }
    }

    std::vector<SceneNodeId> staged_old_children;
    std::vector<SceneNodeId> staged_new_children;
    std::vector<SceneNodeId> subtree;
    if (fail_next_allocation_for_testing_)
    {
        fail_next_allocation_for_testing_ = false;
        return SceneFailure("scene.allocation_failed", "scene hierarchy staging allocation failed");
    }
    try
    {
        subtree = CollectSubtree(child);
        if (child_record->node.parent_id.IsValid())
        {
            const SceneNodeRecord* old_parent = FindRecord(child_record->node.parent_id);
            if (old_parent == nullptr)
            {
                return SceneFailure("scene.invalid_hierarchy", "scene node references a missing parent");
            }
            staged_old_children = old_parent->children;
            staged_old_children.erase(std::remove(staged_old_children.begin(), staged_old_children.end(), child), staged_old_children.end());
        }
        const SceneNodeRecord* new_parent = FindRecord(parent);
        staged_new_children = new_parent->children;
        staged_new_children.reserve(staged_new_children.size() + 1u);
        staged_new_children.push_back(child);
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene hierarchy staging allocation failed");
    }

    const std::size_t required_revisions = subtree.size() + (child_record->node.parent_id.IsValid() ? 1u : 0u) + 1u;
    const auto revisions = ReserveRevisionRange(required_revisions);
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }

    const SceneNodeId old_parent_id = child_record->node.parent_id;
    std::uint64_t next_revision = revisions.Value();
    if (old_parent_id.IsValid())
    {
        SceneNodeRecord* old_parent = FindRecord(old_parent_id);
        old_parent->children.swap(staged_old_children);
        BumpRevisionCommitted(*old_parent, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy), next_revision++);
    }
    SceneNodeRecord* new_parent = FindRecord(parent);
    new_parent->children.swap(staged_new_children);
    BumpRevisionCommitted(*new_parent, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy), next_revision++);
    child_record->local_transform = staged_local;
    child_record->node.parent_id = parent;
    child_record->node.attachment_state = SceneAttachmentState::Attached;
    MarkNodesDirty(subtree, HierarchyTransformBoundsMask(), next_revision);
    revision_ = required_revisions == 0 ? revision_ : next_revision - 1u;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::DetachNode(SceneNodeId child, ReparentMode mode)
{
    if (!IsValid(mode))
    {
        return SceneFailure("scene.invalid_reparent_mode", "scene reparent mode is outside the declared domain");
    }
    const auto required = RequireNode(child, "scene node was not found for detach");
    if (!required)
    {
        return required;
    }

    SceneNodeRecord* child_record = FindRecord(child);
    if (!child_record->node.parent_id.IsValid())
    {
        return foundation::Result<void>::Success();
    }

    const Transform staged_local = mode == ReparentMode::KeepWorld ? ComputeWorldTransform(child) : child_record->local_transform;
    if (!IsValidTransform(staged_local))
    {
        return SceneFailure("scene.invalid_transform", "scene detached local transform is not representable as a valid TRS transform");
    }

    std::vector<SceneNodeId> staged_parent_children;
    std::vector<SceneNodeId> subtree;
    try
    {
        const SceneNodeRecord* parent = FindRecord(child_record->node.parent_id);
        if (parent == nullptr)
        {
            return SceneFailure("scene.invalid_hierarchy", "scene node references a missing parent");
        }
        staged_parent_children = parent->children;
        staged_parent_children.erase(std::remove(staged_parent_children.begin(), staged_parent_children.end(), child), staged_parent_children.end());
        subtree = CollectSubtree(child);
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene hierarchy staging allocation failed");
    }

    const auto revisions = ReserveRevisionRange(subtree.size() + 1u);
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }

    const SceneNodeId old_parent_id = child_record->node.parent_id;
    std::uint64_t next_revision = revisions.Value();
    SceneNodeRecord* old_parent = FindRecord(old_parent_id);
    old_parent->children.swap(staged_parent_children);
    BumpRevisionCommitted(*old_parent, ToSceneDirtyMask(SceneDirtyFlags::Hierarchy), next_revision++);
    child_record->local_transform = staged_local;
    child_record->node.parent_id = {};
    child_record->node.attachment_state = SceneAttachmentState::Detached;
    MarkNodesDirty(subtree, HierarchyTransformBoundsMask(), next_revision);
    revision_ = next_revision - 1u;
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
    if (!IsValid(mobility))
    {
        return SceneFailure("scene.invalid_mobility", "scene mobility is outside the declared domain");
    }
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for mobility update");
    }
    if (record->node.mobility == mobility)
    {
        return foundation::Result<void>::Success();
    }
    const auto revisions = ReserveRevisionRange(1);
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }
    record->node.mobility = mobility;
    BumpRevisionCommitted(*record, 0, revisions.Value());
    revision_ = revisions.Value();
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::SetVisibility(SceneNodeId node, SceneVisibilityState visibility)
{
    if (!IsValid(visibility))
    {
        return SceneFailure("scene.invalid_visibility", "scene visibility is outside the declared domain");
    }
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for visibility update");
    }
    if (record->node.visibility == visibility)
    {
        return foundation::Result<void>::Success();
    }
    const auto revisions = ReserveRevisionRange(1);
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }
    record->node.visibility = visibility;
    BumpRevisionCommitted(*record, ToSceneDirtyMask(SceneDirtyFlags::Visibility), revisions.Value());
    revision_ = revisions.Value();
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
    std::vector<SceneNodeId> subtree;
    try
    {
        subtree = CollectSubtree(node);
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene subtree staging allocation failed");
    }
    const auto revisions = ReserveRevisionRange(subtree.size());
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }
    record->local_transform = transform;
    std::uint64_t next_revision = revisions.Value();
    MarkNodesDirty(subtree, ToSceneDirtyMask(SceneDirtyFlags::Transform) | ToSceneDirtyMask(SceneDirtyFlags::Bounds), next_revision);
    revision_ = next_revision - 1u;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SceneRuntime::SetWorldTransform(SceneNodeId node, const Transform& transform, WorldTransformWriteMode mode)
{
    if (!IsValid(mode))
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
        const auto computed = ComputeLocalTransform(record->node.parent_id, transform);
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

    std::vector<SceneNodeId> subtree;
    try
    {
        subtree = CollectSubtree(node);
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene subtree staging allocation failed");
    }
    const auto revisions = ReserveRevisionRange(subtree.size());
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }
    record->local_transform = local_transform;
    std::uint64_t next_revision = revisions.Value();
    MarkNodesDirty(subtree, ToSceneDirtyMask(SceneDirtyFlags::Transform) | ToSceneDirtyMask(SceneDirtyFlags::Bounds), next_revision);
    revision_ = next_revision - 1u;
    return foundation::Result<void>::Success();
}

std::optional<Transform> SceneRuntime::GetLocalTransform(SceneNodeId node) const
{
    const SceneNodeRecord* record = FindRecord(node);
    return record == nullptr ? std::nullopt : std::optional<Transform>{record->local_transform};
}

std::optional<Transform> SceneRuntime::GetWorldTransform(SceneNodeId node) const
{
    return Exists(node) ? std::optional<Transform>{ComputeWorldTransform(node)} : std::nullopt;
}

foundation::Result<void> SceneRuntime::MarkTransformClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for transform clean marking");
    }
    record->node.dirty_flags = ClearSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Transform);
    return foundation::Result<void>::Success();
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
    std::vector<SceneNodeId> subtree;
    try
    {
        subtree = CollectSubtree(node);
    }
    catch (const std::exception&)
    {
        return SceneFailure("scene.allocation_failed", "scene subtree staging allocation failed");
    }
    const auto revisions = ReserveRevisionRange(subtree.size());
    if (!revisions)
    {
        return foundation::Result<void>::Failure(revisions.GetError());
    }
    record->local_bounds = bounds;
    record->has_bounds = true;
    std::uint64_t next_revision = revisions.Value();
    MarkNodesDirty(subtree, ToSceneDirtyMask(SceneDirtyFlags::Bounds), next_revision);
    revision_ = next_revision - 1u;
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

foundation::Result<void> SceneRuntime::MarkBoundsClean(SceneNodeId node)
{
    SceneNodeRecord* record = FindRecord(node);
    if (record == nullptr)
    {
        return SceneFailure("scene.node_not_found", "scene node was not found for bounds clean marking");
    }
    record->node.dirty_flags = ClearSceneDirtyFlag(record->node.dirty_flags, SceneDirtyFlags::Bounds);
    return foundation::Result<void>::Success();
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

void SceneRuntime::SetAllocatorStateForTesting(std::uint64_t next_node_value) noexcept
{
    next_node_value_ = next_node_value;
}

void SceneRuntime::SetRevisionForTesting(std::uint64_t revision) noexcept
{
    revision_ = revision;
}

void SceneRuntime::FailNextAllocationForTesting() noexcept
{
    fail_next_allocation_for_testing_ = true;
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
    return iterator == nodes_.end() ? nullptr : &iterator->second;
}

const SceneRuntime::SceneNodeRecord* SceneRuntime::FindRecord(SceneNodeId node) const
{
    const auto iterator = nodes_.find(node);
    return iterator == nodes_.end() ? nullptr : &iterator->second;
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
    std::vector<const SceneNodeRecord*> chain;
    SceneNodeId current = node;
    while (current.IsValid())
    {
        const SceneNodeRecord* record = FindRecord(current);
        if (record == nullptr)
        {
            return {};
        }
        chain.push_back(record);
        current = record->node.parent_id;
    }
    Transform world{};
    bool initialized = false;
    for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
    {
        if (!initialized)
        {
            world = (*iterator)->local_transform;
            initialized = true;
        }
        else
        {
            world = ComposeTransform(world, (*iterator)->local_transform);
        }
    }
    return world;
}

foundation::Result<Transform> SceneRuntime::ComputeLocalTransform(SceneNodeId parent, const Transform& world_transform) const
{
    const Transform parent_world = ComputeWorldTransform(parent);
    if (!IsInvertibleScale(parent_world.scale))
    {
        return SceneFailureValue<Transform>("scene.non_invertible_parent_transform", "parent scene transform cannot be inverted");
    }
    const Quat inverse_parent_rotation = Conjugate(Normalize(parent_world.rotation));
    const Vec3 unrotated_position = RotateVector(inverse_parent_rotation, world_transform.position - parent_world.position);
    const Transform local{
        Divide(unrotated_position, parent_world.scale),
        Multiply(inverse_parent_rotation, world_transform.rotation),
        Divide(world_transform.scale, parent_world.scale),
    };
    if (!IsValidTransform(local))
    {
        return SceneFailureValue<Transform>("scene.invalid_transform", "scene computed local transform is not finite or invertible");
    }
    return foundation::Result<Transform>::Success(local);
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

std::vector<SceneNodeId> SceneRuntime::CollectSubtree(SceneNodeId node) const
{
    std::vector<SceneNodeId> result;
    std::vector<SceneNodeId> stack;
    stack.push_back(node);
    while (!stack.empty())
    {
        const SceneNodeId current = stack.back();
        stack.pop_back();
        const SceneNodeRecord* record = FindRecord(current);
        if (record == nullptr)
        {
            continue;
        }
        result.push_back(current);
        for (auto iterator = record->children.rbegin(); iterator != record->children.rend(); ++iterator)
        {
            stack.push_back(*iterator);
        }
    }
    return result;
}

foundation::Result<std::uint64_t> SceneRuntime::ReserveRevisionRange(std::size_t count) const
{
    if (count == 0)
    {
        return foundation::Result<std::uint64_t>::Success(revision_);
    }
    const std::uint64_t count64 = static_cast<std::uint64_t>(count);
    if (count64 > std::numeric_limits<std::uint64_t>::max() - revision_)
    {
        return SceneFailureValue<std::uint64_t>("scene.revision_overflow", "scene revision range is exhausted");
    }
    return foundation::Result<std::uint64_t>::Success(revision_ + 1u);
}

void SceneRuntime::MarkNodesDirty(std::span<const SceneNodeId> nodes, SceneDirtyMask flags, std::uint64_t& next_revision) noexcept
{
    for (SceneNodeId node : nodes)
    {
        SceneNodeRecord* record = FindRecord(node);
        if (record != nullptr)
        {
            BumpRevisionCommitted(*record, flags, next_revision++);
        }
    }
}

void SceneRuntime::BumpRevisionCommitted(SceneNodeRecord& record, SceneDirtyMask flags, std::uint64_t revision) noexcept
{
    record.node.revision = revision;
    record.node.dirty_flags |= flags;
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
