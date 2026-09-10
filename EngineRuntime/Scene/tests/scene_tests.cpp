#include "scene_runtime_impl.h"

#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/scene_services.h"
#include "Epidemic/Runtime/Scene/scene_state.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

#include <iostream>
#include <limits>
#include <cmath>
#include <type_traits>
#include <vector>

namespace
{
using epidemic::runtime::Aabb;
using epidemic::runtime::ClearSceneDirtyFlag;
using epidemic::runtime::CreateSceneServices;
using epidemic::runtime::HasSceneDirtyFlag;
using epidemic::runtime::ISceneNodeRegistry;
using epidemic::runtime::ISceneQuery;
using epidemic::runtime::ISceneSnapshotProvider;
using epidemic::runtime::ISpatialIndex;
using epidemic::runtime::ITransformRegistry;
using epidemic::runtime::Quat;
using epidemic::runtime::ReparentMode;
using epidemic::runtime::SceneAttachmentState;
using epidemic::runtime::SceneDirtyFlags;
using epidemic::runtime::SceneMobility;
using epidemic::runtime::SceneNode;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::SceneRuntime;
using epidemic::runtime::SceneVisibilityState;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;
using epidemic::runtime::WorldTransformWriteMode;

[[nodiscard]] bool Near(float left, float right)
{
    return std::abs(left - right) <= 0.001f;
}

[[nodiscard]] bool Near(Vec3 left, Vec3 right)
{
    return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
}

[[nodiscard]] bool ContainsNode(const std::vector<SceneNodeId>& nodes, SceneNodeId target)
{
    for (SceneNodeId node : nodes)
    {
        if (node == target)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool TestSplitStateDefaults()
{
    const SceneNode node{};
    return !node.id.IsValid() && node.attachment_state == SceneAttachmentState::Detached && node.mobility == SceneMobility::Dynamic &&
           node.visibility == SceneVisibilityState::Visible && node.dirty_flags == 0u && !HasSceneDirtyFlag(node.dirty_flags, SceneDirtyFlags::Transform) &&
           ClearSceneDirtyFlag(0u, SceneDirtyFlags::Bounds) == 0u;
}

[[nodiscard]] bool TestCreateDestroyAndRevision()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created)
    {
        return false;
    }

    const std::uint64_t created_revision = runtime.GetRevision();
    const auto destroyed = runtime.DestroyNode(created.Value());
    return created.Value().IsValid() && created_revision > 0u && destroyed && !runtime.Exists(created.Value()) &&
           runtime.GetRevision() > created_revision;
}

[[nodiscard]] bool TestHierarchyAttachDetachAndCycleReject()
{
    SceneRuntime runtime;
    const auto root = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    const auto grandchild = runtime.CreateNode();
    if (!root || !child || !grandchild)
    {
        return false;
    }

    const auto attach_child = runtime.AttachNode(child.Value(), root.Value());
    const auto attach_grandchild = runtime.AttachNode(grandchild.Value(), child.Value());
    const auto cycle = runtime.AttachNode(root.Value(), grandchild.Value());
    const auto children = runtime.GetChildren(root.Value());
    const auto parent = runtime.GetParent(child.Value());
    const auto detach = runtime.DetachNode(child.Value());

    return attach_child && attach_grandchild && !cycle && cycle.GetError().HasCode("scene.hierarchy_cycle") && parent &&
           *parent == root.Value() && children.size() == 1u && children.front() == child.Value() && detach &&
           !runtime.GetParent(child.Value()) && runtime.GetNode(child.Value())->attachment_state == SceneAttachmentState::Detached;
}

[[nodiscard]] bool TestLocalAndWorldTransforms()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child || !runtime.AttachNode(child.Value(), parent.Value()))
    {
        return false;
    }

    const Transform parent_transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}};
    const Transform child_transform{Vec3{1.0f, 2.0f, 3.0f}, Quat{}, Vec3{0.5f, 1.0f, 1.0f}};
    if (!runtime.SetLocalTransform(parent.Value(), parent_transform) || !runtime.SetLocalTransform(child.Value(), child_transform))
    {
        return false;
    }

    const auto local = runtime.GetLocalTransform(child.Value());
    const auto world = runtime.GetWorldTransform(child.Value());
    return local && *local == child_transform && world && world->position == Vec3{12.0f, 4.0f, 6.0f} &&
           world->scale == Vec3{1.0f, 2.0f, 2.0f};
}

[[nodiscard]] bool TestSetWorldTransformRoot()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node)
    {
        return false;
    }

    const std::uint64_t before_revision = runtime.GetRevision();
    const Transform requested{Vec3{4.0f, 5.0f, 6.0f}, Quat{}, Vec3{2.0f, 3.0f, 4.0f}};
    const auto set = runtime.SetWorldTransform(node.Value(), requested);
    const auto local = runtime.GetLocalTransform(node.Value());
    const auto world = runtime.GetWorldTransform(node.Value());

    return set && local && world && Near(local->position, requested.position) && Near(world->position, requested.position) &&
           Near(world->scale, requested.scale) && runtime.GetRevision() == before_revision + 1u;
}

[[nodiscard]] bool TestSetWorldTransformChildUnderParent()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(), Transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}) ||
        !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const std::uint64_t before_revision = runtime.GetRevision();
    const Transform requested{Vec3{14.0f, 6.0f, 8.0f}, Quat{}, Vec3{4.0f, 2.0f, 2.0f}};
    const auto set = runtime.SetWorldTransform(child.Value(), requested);
    const auto local = runtime.GetLocalTransform(child.Value());
    const auto world = runtime.GetWorldTransform(child.Value());

    return set && local && world && Near(local->position, Vec3{2.0f, 3.0f, 4.0f}) &&
           Near(world->position, requested.position) && Near(world->scale, requested.scale) &&
           runtime.GetRevision() == before_revision + 1u;
}

[[nodiscard]] bool TestSetWorldTransformChildUnderRotatedScaledParent()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    const float quarter_turn = 0.70710677f;
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(),
                                   Transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{0.0f, 0.0f, quarter_turn, quarter_turn}, Vec3{2.0f, 3.0f, 1.0f}}) ||
        !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const Transform requested{Vec3{10.0f, 2.0f, 0.0f}, Quat{}, Vec3{4.0f, 6.0f, 1.0f}};
    const auto set = runtime.SetWorldTransform(child.Value(), requested, WorldTransformWriteMode::RejectNonInvertibleParent);
    const auto local = runtime.GetLocalTransform(child.Value());
    const auto world = runtime.GetWorldTransform(child.Value());

    return set && local && world && Near(local->position, Vec3{1.0f, 0.0f, 0.0f}) &&
           Near(world->position, requested.position) && Near(world->scale, requested.scale);
}

[[nodiscard]] bool TestSetWorldTransformRejectsInvalidWithoutMutation()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node || !runtime.SetWorldTransform(node.Value(), Transform{Vec3{2.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto before = runtime.GetWorldTransform(node.Value());
    const auto rejected = runtime.SetWorldTransform(node.Value(), Transform{Vec3{9.0f, 0.0f, 0.0f}, Quat{}, Vec3{0.0f, 1.0f, 1.0f}});
    const auto after = runtime.GetWorldTransform(node.Value());

    return before && !rejected && rejected.GetError().HasCode("scene.invalid_transform") && after &&
           Near(after->position, before->position) && Near(after->scale, before->scale);
}

[[nodiscard]] bool TestAttachDetachKeepWorldByDefault()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(), Transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}) ||
        !runtime.SetLocalTransform(child.Value(), Transform{Vec3{3.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto before_attach = runtime.GetWorldTransform(child.Value());
    const auto attach = runtime.AttachNode(child.Value(), parent.Value());
    const auto after_attach = runtime.GetWorldTransform(child.Value());
    const auto local_after_attach = runtime.GetLocalTransform(child.Value());
    const auto detach = runtime.DetachNode(child.Value());
    const auto after_detach = runtime.GetWorldTransform(child.Value());
    const auto local_after_detach = runtime.GetLocalTransform(child.Value());

    return before_attach && attach && after_attach && local_after_attach && detach && after_detach && local_after_detach &&
           Near(after_attach->position, before_attach->position) &&
           Near(local_after_attach->position, Vec3{-3.5f, 0.0f, 0.0f}) &&
           Near(after_detach->position, before_attach->position) &&
           Near(local_after_detach->position, before_attach->position);
}

[[nodiscard]] bool TestAttachDetachKeepLocal()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(), Transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}) ||
        !runtime.SetLocalTransform(child.Value(), Transform{Vec3{3.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto attach = runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal);
    const auto attached_world = runtime.GetWorldTransform(child.Value());
    const auto detach = runtime.DetachNode(child.Value(), ReparentMode::KeepLocal);
    const auto detached_world = runtime.GetWorldTransform(child.Value());

    return attach && attached_world && Near(attached_world->position, Vec3{16.0f, 0.0f, 0.0f}) &&
           detach && detached_world && Near(detached_world->position, Vec3{3.0f, 0.0f, 0.0f});
}

[[nodiscard]] bool TestDestroyParentKeepsChildWorldTransform()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(), Transform{Vec3{5.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}) ||
        !runtime.SetLocalTransform(child.Value(), Transform{Vec3{2.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}) ||
        !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const auto before_destroy = runtime.GetWorldTransform(child.Value());
    const auto destroyed = runtime.DestroyNode(parent.Value());
    const auto after_destroy = runtime.GetWorldTransform(child.Value());

    return before_destroy && destroyed && after_destroy && !runtime.GetParent(child.Value()) &&
           Near(after_destroy->position, before_destroy->position);
}

[[nodiscard]] bool TestWorldBoundsAndValidation()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node)
    {
        return false;
    }

    const auto invalid_bounds = runtime.SetLocalBounds(node.Value(), Aabb{Vec3{2.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}});
    const auto invalid_transform = runtime.SetLocalTransform(node.Value(), Transform{Vec3{}, Quat{}, Vec3{0.0f, 1.0f, 1.0f}});
    const auto set_transform = runtime.SetLocalTransform(node.Value(), Transform{Vec3{5.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}});
    const auto set_bounds = runtime.SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}});
    const auto world_bounds = runtime.GetWorldBounds(node.Value());

    return !invalid_bounds && invalid_bounds.GetError().HasCode("scene.invalid_bounds") && !invalid_transform &&
           invalid_transform.GetError().HasCode("scene.invalid_transform") && set_transform && set_bounds && world_bounds &&
           world_bounds->min == Vec3{4.0f, -1.0f, -1.0f} && world_bounds->max == Vec3{6.0f, 1.0f, 1.0f};
}

[[nodiscard]] bool TestRotatedScaledWorldBounds()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node)
    {
        return false;
    }

    const float half_turn = 0.70710677f;
    const Transform transform{Vec3{1.0f, 2.0f, 0.0f}, Quat{0.0f, 0.0f, half_turn, half_turn}, Vec3{2.0f, 1.0f, 1.0f}};
    if (!runtime.SetLocalTransform(node.Value(), transform) ||
        !runtime.SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f}}))
    {
        return false;
    }

    const auto world_bounds = runtime.GetWorldBounds(node.Value());
    return world_bounds && Near(world_bounds->min, Vec3{0.0f, 0.0f, 0.0f}) &&
           Near(world_bounds->max, Vec3{2.0f, 4.0f, 0.0f});
}

[[nodiscard]] bool TestDirtyFlagsSetAndClear()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node || !runtime.SetLocalTransform(node.Value(), Transform{}) || !runtime.SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    if (!runtime.IsTransformDirty(node.Value()) || !runtime.IsBoundsDirty(node.Value()))
    {
        return false;
    }

    const auto transform_clean = runtime.MarkTransformClean(node.Value());
    const auto bounds_clean = runtime.MarkBoundsClean(node.Value());
    return transform_clean && bounds_clean && !runtime.IsTransformDirty(node.Value()) && !runtime.IsBoundsDirty(node.Value());
}

[[nodiscard]] bool TestDirtyFlagsAreTransientForRevisionAndSnapshots()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node || !runtime.SetLocalTransform(node.Value(), Transform{}))
    {
        return false;
    }

    const auto revision_after_change = runtime.GetRevision();
    const auto dirty_snapshot = runtime.CaptureSnapshot();
    if (!runtime.MarkTransformClean(node.Value()))
    {
        return false;
    }
    const auto clean_revision = runtime.GetRevision();
    const auto clean_snapshot = runtime.CaptureSnapshot();

    return dirty_snapshot.nodes.size() == 1u && clean_revision == revision_after_change &&
           clean_snapshot.nodes.size() == 1u &&
           clean_snapshot.revision == dirty_snapshot.revision &&
           clean_snapshot.nodes.front().revision == dirty_snapshot.nodes.front().revision;
}

[[nodiscard]] bool TestQueriesAreVisibleAndDeterministic()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    const auto third = runtime.CreateNode();
    if (!first || !second || !third)
    {
        return false;
    }

    const Aabb bounds{Vec3{0.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f}};
    if (!runtime.SetLocalBounds(third.Value(), bounds) || !runtime.SetLocalBounds(first.Value(), bounds) ||
        !runtime.SetLocalBounds(second.Value(), bounds) || !runtime.SetVisibility(second.Value(), SceneVisibilityState::Hidden))
    {
        return false;
    }

    const auto result = runtime.QueryAabb(Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{3.0f, 3.0f, 3.0f}});
    const auto sphere = runtime.QuerySphere(Vec3{1.0f, 1.0f, 1.0f}, 2.0f);
    return result.size() == 2u && result[0] == first.Value() && result[1] == third.Value() && !ContainsNode(result, second.Value()) &&
           sphere == result;
}

[[nodiscard]] bool TestSnapshotCapturesRevisionAndSortedNodes()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    if (!first || !second || !runtime.SetLocalTransform(second.Value(), Transform{Vec3{2.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto snapshot = runtime.CaptureSnapshot();
    return snapshot.revision == runtime.GetRevision() && snapshot.nodes.size() == 2u && snapshot.nodes[0].id == first.Value() &&
           snapshot.nodes[1].id == second.Value() && snapshot.nodes[1].world_transform.position == Vec3{2.0f, 0.0f, 0.0f} &&
           snapshot.nodes[1].revision > 0u && snapshot.nodes[1].visibility == SceneVisibilityState::Visible;
}

[[nodiscard]] bool TestFactoryCreatesSharedRuntimeServices()
{
    const auto services = CreateSceneServices();
    if (!services || !services.Value().nodes || !services.Value().transforms || !services.Value().spatial || !services.Value().queries || !services.Value().snapshots)
    {
        return false;
    }

    const auto node = services.Value().nodes->CreateNode();
    if (!node || !services.Value().transforms->SetLocalTransform(node.Value(), Transform{Vec3{3.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}) ||
        !services.Value().spatial->SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto query = services.Value().queries->QueryAabb(Aabb{Vec3{2.0f, -1.0f, -1.0f}, Vec3{4.0f, 1.0f, 1.0f}});
    const auto snapshot = services.Value().snapshots->CaptureSnapshot();
    return query.size() == 1u && query.front() == node.Value() && snapshot.nodes.size() == 1u;
}

[[nodiscard]] bool SameSnapshot(const epidemic::runtime::SceneSnapshot& left, const epidemic::runtime::SceneSnapshot& right)
{
    if (left.revision != right.revision || left.nodes.size() != right.nodes.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.nodes.size(); ++index)
    {
        const auto& a = left.nodes[index];
        const auto& b = right.nodes[index];
        if (a.id != b.id || a.parent != b.parent || a.local_transform != b.local_transform ||
            a.world_transform != b.world_transform || a.local_bounds != b.local_bounds || a.world_bounds != b.world_bounds ||
            a.attachment != b.attachment || a.mobility != b.mobility || a.visibility != b.visibility || a.revision != b.revision)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool TestFailedReparentIsTransactional()
{
    SceneRuntime runtime;
    const auto old_parent = runtime.CreateNode();
    const auto new_parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!old_parent || !new_parent || !child ||
        !runtime.SetLocalTransform(new_parent.Value(), Transform{Vec3{}, Quat{}, Vec3{0.0000001f, 1.0f, 1.0f}}) ||
        !runtime.AttachNode(child.Value(), old_parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const auto before = runtime.CaptureSnapshot();
    const auto rejected = runtime.AttachNode(child.Value(), new_parent.Value(), ReparentMode::KeepWorld);
    const auto after = runtime.CaptureSnapshot();
    if (rejected || !rejected.GetError().HasCode("scene.non_invertible_parent_transform") || !SameSnapshot(before, after))
    {
        return false;
    }

    runtime.FailNextAllocationForTesting();
    const auto allocation_before = runtime.CaptureSnapshot();
    const auto allocation_failed = runtime.AttachNode(child.Value(), new_parent.Value(), ReparentMode::KeepLocal);
    return !allocation_failed && allocation_failed.GetError().HasCode("scene.allocation_failed") &&
           SameSnapshot(allocation_before, runtime.CaptureSnapshot());
}

[[nodiscard]] bool TestSceneBoundaryContracts()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    if (!first || !second)
    {
        return false;
    }
    const auto before = runtime.CaptureSnapshot();
    const auto invalid_mode = runtime.AttachNode(second.Value(), first.Value(), static_cast<ReparentMode>(255));
    const auto invalid_mobility = runtime.SetMobility(first.Value(), static_cast<SceneMobility>(255));
    const auto invalid_visibility = runtime.SetVisibility(first.Value(), static_cast<SceneVisibilityState>(255));
    const auto invalid_write = runtime.SetWorldTransform(first.Value(), Transform{}, static_cast<WorldTransformWriteMode>(255));
    const auto stale_clean = runtime.MarkTransformClean(SceneNodeId{999999});
    const auto stale_bounds = runtime.MarkBoundsClean(SceneNodeId{999999});
    if (invalid_mode || invalid_mobility || invalid_visibility || invalid_write || stale_clean || stale_bounds ||
        !SameSnapshot(before, runtime.CaptureSnapshot()))
    {
        return false;
    }

    runtime.SetRevisionForTesting(std::numeric_limits<std::uint64_t>::max() - 1u);
    const auto final_revision = runtime.SetVisibility(first.Value(), SceneVisibilityState::Hidden);
    if (!final_revision || runtime.GetRevision() != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    const auto max_snapshot = runtime.CaptureSnapshot();
    const auto exhausted = runtime.SetVisibility(first.Value(), SceneVisibilityState::Visible);
    if (exhausted || !exhausted.GetError().HasCode("scene.revision_overflow") || !SameSnapshot(max_snapshot, runtime.CaptureSnapshot()))
    {
        return false;
    }

    SceneRuntime ids;
    ids.SetAllocatorStateForTesting(std::numeric_limits<std::uint64_t>::max());
    const auto last = ids.CreateNode();
    const auto overflow = ids.CreateNode();
    if (!last || last.Value().Raw() != std::numeric_limits<std::uint64_t>::max() || overflow ||
        !overflow.GetError().HasCode("scene.node_id_exhausted"))
    {
        return false;
    }
    SceneRuntime allocation;
    const auto revision_before = allocation.GetRevision();
    allocation.FailNextAllocationForTesting();
    const auto failed_create = allocation.CreateNode();
    return !failed_create && allocation.GetRevision() == revision_before && allocation.CaptureSnapshot().nodes.empty();
}

[[nodiscard]] bool TestDeepHierarchyUsesIterativeTraversal()
{
    SceneRuntime runtime;
    constexpr std::size_t depth = 4096;
    std::vector<SceneNodeId> nodes;
    nodes.reserve(depth);
    for (std::size_t index = 0; index < depth; ++index)
    {
        const auto node = runtime.CreateNode();
        if (!node)
        {
            return false;
        }
        nodes.push_back(node.Value());
        if (index > 0 && !runtime.AttachNode(nodes[index], nodes[index - 1], ReparentMode::KeepLocal))
        {
            return false;
        }
    }
    const auto world = runtime.GetWorldTransform(nodes.back());
    return world.has_value() && runtime.SetLocalTransform(nodes.front(), Transform{Vec3{1.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}).HasValue();
}

} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<Vec3>);
    static_assert(std::is_trivially_copyable_v<Quat>);
    static_assert(std::is_trivially_copyable_v<Transform>);
    static_assert(std::is_trivially_copyable_v<Aabb>);
    static_assert(std::is_trivially_copyable_v<SceneNodeId>);
    static_assert(std::is_trivially_copyable_v<SceneNode>);
    static_assert(std::has_virtual_destructor_v<ITransformRegistry>);
    static_assert(std::has_virtual_destructor_v<ISceneNodeRegistry>);
    static_assert(std::has_virtual_destructor_v<ISpatialIndex>);
    static_assert(std::has_virtual_destructor_v<ISceneQuery>);
    static_assert(std::has_virtual_destructor_v<ISceneSnapshotProvider>);

    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"SplitStateDefaults", TestSplitStateDefaults},
        {"CreateDestroyAndRevision", TestCreateDestroyAndRevision},
        {"HierarchyAttachDetachAndCycleReject", TestHierarchyAttachDetachAndCycleReject},
        {"LocalAndWorldTransforms", TestLocalAndWorldTransforms},
        {"SetWorldTransformRoot", TestSetWorldTransformRoot},
        {"SetWorldTransformChildUnderParent", TestSetWorldTransformChildUnderParent},
        {"SetWorldTransformChildUnderRotatedScaledParent", TestSetWorldTransformChildUnderRotatedScaledParent},
        {"SetWorldTransformRejectsInvalidWithoutMutation", TestSetWorldTransformRejectsInvalidWithoutMutation},
        {"AttachDetachKeepWorldByDefault", TestAttachDetachKeepWorldByDefault},
        {"AttachDetachKeepLocal", TestAttachDetachKeepLocal},
        {"DestroyParentKeepsChildWorldTransform", TestDestroyParentKeepsChildWorldTransform},
        {"WorldBoundsAndValidation", TestWorldBoundsAndValidation},
        {"RotatedScaledWorldBounds", TestRotatedScaledWorldBounds},
        {"DirtyFlagsSetAndClear", TestDirtyFlagsSetAndClear},
        {"DirtyFlagsAreTransientForRevisionAndSnapshots", TestDirtyFlagsAreTransientForRevisionAndSnapshots},
        {"QueriesAreVisibleAndDeterministic", TestQueriesAreVisibleAndDeterministic},
        {"SnapshotCapturesRevisionAndSortedNodes", TestSnapshotCapturesRevisionAndSortedNodes},
        {"FactoryCreatesSharedRuntimeServices", TestFactoryCreatesSharedRuntimeServices},
        {"FailedReparentIsTransactional", TestFailedReparentIsTransactional},
        {"SceneBoundaryContracts", TestSceneBoundaryContracts},
        {"DeepHierarchyUsesIterativeTraversal", TestDeepHierarchyUsesIterativeTraversal},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Scene test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}
