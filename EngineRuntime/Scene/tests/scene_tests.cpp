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

[[nodiscard]] bool Near(Quat left, Quat right)
{
    const bool direct = Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z) && Near(left.w, right.w);
    const bool negated = Near(left.x, -right.x) && Near(left.y, -right.y) && Near(left.z, -right.z) && Near(left.w, -right.w);
    return direct || negated;
}

[[nodiscard]] bool Near(const Transform& left, const Transform& right)
{
    return Near(left.position, right.position) && Near(left.rotation, right.rotation) && Near(left.scale, right.scale);
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

[[nodiscard]] bool TestLargeFiniteSphereQueryDoesNotOverflowToIntersection()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node ||
        !runtime.SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto miss = runtime.QuerySphere(Vec3{1.0e30f, 0.0f, 0.0f}, 1.0e20f);
    const auto hit = runtime.QuerySphere(Vec3{1.0e30f, 0.0f, 0.0f}, 2.0e30f);
    return miss.empty() && hit.size() == 1u && hit.front() == node.Value();
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
    const auto old_children_before = runtime.GetChildren(old_parent.Value());
    const auto new_children_before = runtime.GetChildren(new_parent.Value());
    const auto old_parent_before = runtime.GetNode(old_parent.Value());
    const auto new_parent_before = runtime.GetNode(new_parent.Value());
    const auto child_before = runtime.GetNode(child.Value());
    const auto rejected = runtime.AttachNode(child.Value(), new_parent.Value(), ReparentMode::KeepWorld);
    const auto after = runtime.CaptureSnapshot();
    if (rejected || !rejected.GetError().HasCode("scene.non_invertible_parent_transform") || !SameSnapshot(before, after) ||
        runtime.GetChildren(old_parent.Value()) != old_children_before || runtime.GetChildren(new_parent.Value()) != new_children_before ||
        runtime.GetNode(old_parent.Value()) != old_parent_before || runtime.GetNode(new_parent.Value()) != new_parent_before ||
        runtime.GetNode(child.Value()) != child_before)
    {
        return false;
    }

    runtime.FailNextAllocationForTesting();
    const auto allocation_before = runtime.CaptureSnapshot();
    const auto allocation_old_children = runtime.GetChildren(old_parent.Value());
    const auto allocation_new_children = runtime.GetChildren(new_parent.Value());
    const auto allocation_old_parent = runtime.GetNode(old_parent.Value());
    const auto allocation_new_parent = runtime.GetNode(new_parent.Value());
    const auto allocation_child = runtime.GetNode(child.Value());
    const auto allocation_failed = runtime.AttachNode(child.Value(), new_parent.Value(), ReparentMode::KeepLocal);
    return !allocation_failed && allocation_failed.GetError().HasCode("scene.allocation_failed") &&
           SameSnapshot(allocation_before, runtime.CaptureSnapshot()) &&
           runtime.GetChildren(old_parent.Value()) == allocation_old_children &&
           runtime.GetChildren(new_parent.Value()) == allocation_new_children &&
           runtime.GetNode(old_parent.Value()) == allocation_old_parent &&
           runtime.GetNode(new_parent.Value()) == allocation_new_parent && runtime.GetNode(child.Value()) == allocation_child;
}

[[nodiscard]] bool TestIdentityLifecycleStaleAndDuplicateContracts()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    const auto third = runtime.CreateNode();
    if (!first || !second || !third || first.Value() == second.Value() || second.Value() == third.Value() ||
        !(first.Value().Raw() < second.Value().Raw() && second.Value().Raw() < third.Value().Raw()))
    {
        return false;
    }

    const auto invalid_destroy = runtime.DestroyNode(SceneNodeId{});
    if (invalid_destroy || !invalid_destroy.GetError().HasCode("scene.invalid_node"))
    {
        return false;
    }

    const SceneNodeId removed = second.Value();
    if (!runtime.DestroyNode(removed) || runtime.Exists(removed) || runtime.GetNode(removed).has_value())
    {
        return false;
    }
    const auto stale_destroy = runtime.DestroyNode(removed);
    const auto replacement = runtime.CreateNode();
    if (stale_destroy || !stale_destroy.GetError().HasCode("scene.node_not_found") || !replacement ||
        replacement.Value() == removed || replacement.Value().Raw() <= third.Value().Raw())
    {
        return false;
    }

    SceneRuntime duplicate_runtime;
    const auto duplicate_first = duplicate_runtime.CreateNode();
    if (!duplicate_first)
    {
        return false;
    }
    const auto duplicate_before = duplicate_runtime.CaptureSnapshot();
    duplicate_runtime.SetAllocatorStateForTesting(duplicate_first.Value().Raw());
    const auto duplicate = duplicate_runtime.CreateNode();
    return !duplicate && duplicate.GetError().HasCode("scene.duplicate_node_id") &&
           SameSnapshot(duplicate_before, duplicate_runtime.CaptureSnapshot());
}

[[nodiscard]] bool TestCreateRevisionAndAllocationFailureDoNotPublishOrConsumeIdentity()
{
    SceneRuntime revision_runtime;
    const auto first = revision_runtime.CreateNode();
    if (!first)
    {
        return false;
    }
    const auto before = revision_runtime.CaptureSnapshot();
    const std::uint64_t saved_revision = revision_runtime.GetRevision();
    revision_runtime.SetRevisionForTesting(std::numeric_limits<std::uint64_t>::max());
    const auto exhausted = revision_runtime.CreateNode();
    if (exhausted || !exhausted.GetError().HasCode("scene.revision_overflow"))
    {
        return false;
    }
    revision_runtime.SetRevisionForTesting(saved_revision);
    if (!SameSnapshot(before, revision_runtime.CaptureSnapshot()))
    {
        return false;
    }
    const auto retry = revision_runtime.CreateNode();
    if (!retry || retry.Value().Raw() != first.Value().Raw() + 1u)
    {
        return false;
    }

    SceneRuntime allocation_runtime;
    allocation_runtime.FailNextAllocationForTesting();
    const auto allocation_failed = allocation_runtime.CreateNode();
    if (allocation_failed || !allocation_failed.GetError().HasCode("scene.allocation_failed") ||
        allocation_runtime.GetRevision() != 0u || !allocation_runtime.CaptureSnapshot().nodes.empty())
    {
        return false;
    }
    const auto allocation_retry = allocation_runtime.CreateNode();
    return allocation_retry && allocation_retry.Value().Raw() == 1u;
}

[[nodiscard]] bool TestExplicitNoOpAndRepeatedWriteSemantics()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child || !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const auto attached = runtime.CaptureSnapshot();
    const auto attached_revision = runtime.GetRevision();
    if (!runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepWorld) ||
        runtime.GetRevision() != attached_revision || !SameSnapshot(attached, runtime.CaptureSnapshot()))
    {
        return false;
    }

    if (!runtime.DetachNode(child.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }
    const auto detached = runtime.CaptureSnapshot();
    const auto detached_revision = runtime.GetRevision();
    if (!runtime.DetachNode(child.Value(), ReparentMode::KeepWorld) || runtime.GetRevision() != detached_revision ||
        !SameSnapshot(detached, runtime.CaptureSnapshot()))
    {
        return false;
    }

    const auto metadata_before = runtime.CaptureSnapshot();
    const auto metadata_revision = runtime.GetRevision();
    if (!runtime.SetMobility(parent.Value(), SceneMobility::Dynamic) ||
        !runtime.SetVisibility(parent.Value(), SceneVisibilityState::Visible) ||
        !runtime.MarkTransformClean(parent.Value()) || !runtime.MarkBoundsClean(parent.Value()) ||
        runtime.GetRevision() != metadata_revision || !SameSnapshot(metadata_before, runtime.CaptureSnapshot()))
    {
        return false;
    }

    const Transform authored{Vec3{1.0f, 2.0f, 3.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}};
    if (!runtime.SetLocalTransform(parent.Value(), authored))
    {
        return false;
    }
    const auto first_write_revision = runtime.GetRevision();
    if (!runtime.SetLocalTransform(parent.Value(), authored) || runtime.GetRevision() != first_write_revision + 1u)
    {
        return false;
    }

    const Aabb bounds{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    if (!runtime.SetLocalBounds(parent.Value(), bounds))
    {
        return false;
    }
    const auto first_bounds_revision = runtime.GetRevision();
    return runtime.SetLocalBounds(parent.Value(), bounds) && runtime.GetRevision() == first_bounds_revision + 1u;
}

[[nodiscard]] bool TestNonFiniteInputsAndQueryValidation()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node)
    {
        return false;
    }
    const auto before = runtime.CaptureSnapshot();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();

    const auto nan_transform = runtime.SetLocalTransform(node.Value(), Transform{Vec3{nan, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}});
    const auto inf_transform = runtime.SetWorldTransform(node.Value(), Transform{Vec3{}, Quat{}, Vec3{1.0f, infinity, 1.0f}});
    const auto nan_bounds = runtime.SetLocalBounds(node.Value(), Aabb{Vec3{nan, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}});
    if (nan_transform || inf_transform || nan_bounds || !nan_transform.GetError().HasCode("scene.invalid_transform") ||
        !inf_transform.GetError().HasCode("scene.invalid_transform") || !nan_bounds.GetError().HasCode("scene.invalid_bounds") ||
        !SameSnapshot(before, runtime.CaptureSnapshot()))
    {
        return false;
    }

    const auto invalid_aabb = runtime.QueryAabb(Aabb{Vec3{2.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}});
    const auto nan_aabb = runtime.QueryAabb(Aabb{Vec3{nan, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}});
    const auto negative_sphere = runtime.QuerySphere(Vec3{}, -1.0f);
    const auto nan_center = runtime.QuerySphere(Vec3{nan, 0.0f, 0.0f}, 1.0f);
    const auto infinite_radius = runtime.QuerySphere(Vec3{}, infinity);
    return invalid_aabb.empty() && nan_aabb.empty() && negative_sphere.empty() && nan_center.empty() && infinite_radius.empty();
}

[[nodiscard]] bool TestNonInvertibleParentWorldWriteIsTransactional()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child ||
        !runtime.SetLocalTransform(parent.Value(), Transform{Vec3{1.0f, 2.0f, 3.0f}, Quat{}, Vec3{0.0000001f, 1.0f, 1.0f}}) ||
        !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }

    const auto before = runtime.CaptureSnapshot();
    const auto rejected = runtime.SetWorldTransform(child.Value(), Transform{Vec3{5.0f, 6.0f, 7.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}});
    return !rejected && rejected.GetError().HasCode("scene.non_invertible_parent_transform") &&
           SameSnapshot(before, runtime.CaptureSnapshot());
}

[[nodiscard]] bool TestSubtreeTransformPropagation()
{
    SceneRuntime runtime;
    const auto root = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    const auto grandchild = runtime.CreateNode();
    if (!root || !child || !grandchild || !runtime.AttachNode(child.Value(), root.Value(), ReparentMode::KeepLocal) ||
        !runtime.AttachNode(grandchild.Value(), child.Value(), ReparentMode::KeepLocal) ||
        !runtime.SetLocalTransform(child.Value(), Transform{Vec3{1.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}) ||
        !runtime.SetLocalTransform(grandchild.Value(), Transform{Vec3{2.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    if (!runtime.MarkTransformClean(root.Value()) || !runtime.MarkTransformClean(child.Value()) || !runtime.MarkTransformClean(grandchild.Value()) ||
        !runtime.SetLocalTransform(root.Value(), Transform{Vec3{10.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}))
    {
        return false;
    }
    const auto child_world = runtime.GetWorldTransform(child.Value());
    const auto grandchild_world = runtime.GetWorldTransform(grandchild.Value());
    if (!child_world || !grandchild_world || !Near(child_world->position, Vec3{12.0f, 0.0f, 0.0f}) ||
        !Near(grandchild_world->position, Vec3{16.0f, 0.0f, 0.0f}) || !runtime.IsTransformDirty(root.Value()) ||
        !runtime.IsTransformDirty(child.Value()) || !runtime.IsTransformDirty(grandchild.Value()))
    {
        return false;
    }

    const Transform requested{Vec3{20.0f, 0.0f, 0.0f}, Quat{}, Vec3{4.0f, 4.0f, 4.0f}};
    if (!runtime.SetWorldTransform(child.Value(), requested))
    {
        return false;
    }
    const auto updated_child_world = runtime.GetWorldTransform(child.Value());
    const auto updated_grandchild_world = runtime.GetWorldTransform(grandchild.Value());
    return updated_child_world && updated_grandchild_world && Near(*updated_child_world, requested) &&
           Near(updated_grandchild_world->position, Vec3{28.0f, 0.0f, 0.0f});
}

[[nodiscard]] bool TestNegativeNonUniformRotatedBounds()
{
    SceneRuntime runtime;
    const auto node = runtime.CreateNode();
    if (!node)
    {
        return false;
    }

    const float quarter_turn = 0.70710677f;
    const Transform transform{Vec3{1.0f, 2.0f, 3.0f}, Quat{0.0f, 0.0f, quarter_turn, quarter_turn}, Vec3{-2.0f, 3.0f, -0.5f}};
    if (!runtime.SetLocalBounds(node.Value(), Aabb{Vec3{-1.0f, -2.0f, -3.0f}, Vec3{4.0f, 5.0f, 6.0f}}) ||
        !runtime.SetLocalTransform(node.Value(), transform))
    {
        return false;
    }

    const auto world = runtime.GetWorldBounds(node.Value());
    return world && Near(world->min, Vec3{-14.0f, -6.0f, 0.0f}) && Near(world->max, Vec3{7.0f, 4.0f, 4.5f});
}

[[nodiscard]] bool TestKeepWorldPreservesFullApproximateTrs()
{
    SceneRuntime runtime;
    const auto parent = runtime.CreateNode();
    const auto child = runtime.CreateNode();
    if (!parent || !child)
    {
        return false;
    }

    const float quarter_turn = 0.70710677f;
    if (!runtime.SetLocalTransform(parent.Value(), Transform{Vec3{10.0f, 2.0f, 3.0f}, Quat{0.0f, 0.0f, quarter_turn, quarter_turn}, Vec3{-2.0f, 3.0f, 0.5f}}) ||
        !runtime.SetLocalTransform(child.Value(), Transform{Vec3{4.0f, -1.0f, 2.0f}, Quat{quarter_turn, 0.0f, 0.0f, quarter_turn}, Vec3{1.5f, -2.0f, 4.0f}}))
    {
        return false;
    }

    const auto before = runtime.GetWorldTransform(child.Value());
    if (!before || !runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepWorld))
    {
        return false;
    }
    const auto attached = runtime.GetWorldTransform(child.Value());
    if (!attached || !Near(*before, *attached) || !runtime.DetachNode(child.Value(), ReparentMode::KeepWorld))
    {
        return false;
    }
    const auto detached = runtime.GetWorldTransform(child.Value());
    return detached && Near(*before, *detached);
}

[[nodiscard]] bool TestKeepWorldRejectsUnrepresentablePrecisionLoss()
{
    const Transform parent_transform{
        Vec3{40655008.0f, -20218176.0f, -3486.79443f},
        Quat{0.0f, 0.0f, -0.0507934f, 0.998709f},
        Vec3{-0.111294828f, 1.3241837f, 0.0019051464f},
    };
    const Transform target_world{
        Vec3{100.997543f, -70.848259f, 570.347107f},
        Quat{0.0f, 0.0f, 0.130130172f, 0.991496921f},
        Vec3{2.42860937f, 0.95157814f, 164.323456f},
    };

    SceneRuntime attach_runtime;
    const auto parent = attach_runtime.CreateNode();
    const auto child = attach_runtime.CreateNode();
    if (!parent || !child || !attach_runtime.SetLocalTransform(parent.Value(), parent_transform) ||
        !attach_runtime.SetLocalTransform(child.Value(), target_world))
    {
        return false;
    }
    const auto attach_before = attach_runtime.CaptureSnapshot();
    const auto attach = attach_runtime.AttachNode(child.Value(), parent.Value(), ReparentMode::KeepWorld);
    if (attach || !attach.GetError().HasCode("scene.unrepresentable_world_transform") ||
        !SameSnapshot(attach_before, attach_runtime.CaptureSnapshot()))
    {
        return false;
    }

    SceneRuntime write_runtime;
    const auto write_parent = write_runtime.CreateNode();
    const auto write_child = write_runtime.CreateNode();
    if (!write_parent || !write_child || !write_runtime.SetLocalTransform(write_parent.Value(), parent_transform) ||
        !write_runtime.AttachNode(write_child.Value(), write_parent.Value(), ReparentMode::KeepLocal))
    {
        return false;
    }
    const auto write_before = write_runtime.CaptureSnapshot();
    const auto write = write_runtime.SetWorldTransform(
        write_child.Value(), target_world, WorldTransformWriteMode::RejectNonInvertibleParent);
    return !write && write.GetError().HasCode("scene.unrepresentable_world_transform") &&
           SameSnapshot(write_before, write_runtime.CaptureSnapshot());
}

[[nodiscard]] bool TestWorldRangeOverflowIsTransactional()
{
    const float huge_scale = std::numeric_limits<float>::max() / 2.0f;

    SceneRuntime attach_runtime;
    const auto attach_parent = attach_runtime.CreateNode();
    const auto attach_child = attach_runtime.CreateNode();
    if (!attach_parent || !attach_child ||
        !attach_runtime.SetLocalTransform(
            attach_parent.Value(), Transform{Vec3{}, Quat{}, Vec3{huge_scale, 1.0f, 1.0f}}) ||
        !attach_runtime.SetLocalTransform(
            attach_child.Value(), Transform{Vec3{4.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }
    const auto attach_before = attach_runtime.CaptureSnapshot();
    const auto attach = attach_runtime.AttachNode(attach_child.Value(), attach_parent.Value(), ReparentMode::KeepLocal);
    if (attach || !attach.GetError().HasCode("scene.world_transform_out_of_range") ||
        !SameSnapshot(attach_before, attach_runtime.CaptureSnapshot()))
    {
        return false;
    }

    SceneRuntime subtree_runtime;
    const auto root = subtree_runtime.CreateNode();
    const auto descendant = subtree_runtime.CreateNode();
    if (!root || !descendant || !subtree_runtime.AttachNode(descendant.Value(), root.Value(), ReparentMode::KeepLocal) ||
        !subtree_runtime.SetLocalTransform(
            descendant.Value(), Transform{Vec3{}, Quat{}, Vec3{4.0f, 1.0f, 1.0f}}))
    {
        return false;
    }
    const auto subtree_before = subtree_runtime.CaptureSnapshot();
    const auto transform = subtree_runtime.SetLocalTransform(
        root.Value(), Transform{Vec3{}, Quat{}, Vec3{huge_scale, 1.0f, 1.0f}});
    if (transform || !transform.GetError().HasCode("scene.world_transform_out_of_range") ||
        !SameSnapshot(subtree_before, subtree_runtime.CaptureSnapshot()))
    {
        return false;
    }

    SceneRuntime bounds_runtime;
    const auto bounded = bounds_runtime.CreateNode();
    if (!bounded ||
        !bounds_runtime.SetLocalTransform(
            bounded.Value(), Transform{Vec3{}, Quat{}, Vec3{huge_scale, 1.0f, 1.0f}}))
    {
        return false;
    }
    const auto bounds_before = bounds_runtime.CaptureSnapshot();
    const auto bounds = bounds_runtime.SetLocalBounds(
        bounded.Value(), Aabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{4.0f, 1.0f, 1.0f}});
    return !bounds && bounds.GetError().HasCode("scene.world_bounds_out_of_range") &&
           SameSnapshot(bounds_before, bounds_runtime.CaptureSnapshot());
}

[[nodiscard]] bool TestSnapshotIsDetachedAndEmptyStateIsStable()
{
    SceneRuntime empty;
    if (!empty.CaptureSnapshot().nodes.empty() || !empty.QueryAabb(Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}}).empty() ||
        !empty.QuerySphere(Vec3{}, 1.0f).empty())
    {
        return false;
    }

    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    if (!first || !second || !runtime.AttachNode(second.Value(), first.Value(), ReparentMode::KeepLocal) ||
        !runtime.SetLocalTransform(first.Value(), Transform{Vec3{3.0f, 0.0f, 0.0f}, Quat{}, Vec3{2.0f, 2.0f, 2.0f}}) ||
        !runtime.SetLocalBounds(second.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}}))
    {
        return false;
    }

    const auto snapshot = runtime.CaptureSnapshot();
    if (snapshot.nodes.size() != 2u)
    {
        return false;
    }
    const auto captured_revision = snapshot.revision;
    const auto captured_first = snapshot.nodes[0];
    const auto captured_second = snapshot.nodes[1];

    if (!runtime.SetLocalTransform(first.Value(), Transform{Vec3{100.0f, 0.0f, 0.0f}, Quat{}, Vec3{1.0f, 1.0f, 1.0f}}) ||
        !runtime.SetVisibility(second.Value(), SceneVisibilityState::Hidden) || !runtime.DestroyNode(first.Value()))
    {
        return false;
    }

    return snapshot.revision == captured_revision && snapshot.nodes.size() == 2u && snapshot.nodes[0].id == captured_first.id &&
           snapshot.nodes[0].local_transform == captured_first.local_transform && snapshot.nodes[0].world_transform == captured_first.world_transform &&
           snapshot.nodes[1].id == captured_second.id && snapshot.nodes[1].parent == captured_second.parent &&
           snapshot.nodes[1].local_bounds == captured_second.local_bounds && snapshot.nodes[1].world_bounds == captured_second.world_bounds &&
           snapshot.nodes[1].visibility == captured_second.visibility;
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
        {"LargeFiniteSphereQueryDoesNotOverflowToIntersection", TestLargeFiniteSphereQueryDoesNotOverflowToIntersection},
        {"SnapshotCapturesRevisionAndSortedNodes", TestSnapshotCapturesRevisionAndSortedNodes},
        {"FactoryCreatesSharedRuntimeServices", TestFactoryCreatesSharedRuntimeServices},
        {"FailedReparentIsTransactional", TestFailedReparentIsTransactional},
        {"IdentityLifecycleStaleAndDuplicateContracts", TestIdentityLifecycleStaleAndDuplicateContracts},
        {"CreateRevisionAndAllocationFailureDoNotPublishOrConsumeIdentity", TestCreateRevisionAndAllocationFailureDoNotPublishOrConsumeIdentity},
        {"ExplicitNoOpAndRepeatedWriteSemantics", TestExplicitNoOpAndRepeatedWriteSemantics},
        {"NonFiniteInputsAndQueryValidation", TestNonFiniteInputsAndQueryValidation},
        {"NonInvertibleParentWorldWriteIsTransactional", TestNonInvertibleParentWorldWriteIsTransactional},
        {"SubtreeTransformPropagation", TestSubtreeTransformPropagation},
        {"NegativeNonUniformRotatedBounds", TestNegativeNonUniformRotatedBounds},
        {"KeepWorldPreservesFullApproximateTrs", TestKeepWorldPreservesFullApproximateTrs},
        {"KeepWorldRejectsUnrepresentablePrecisionLoss", TestKeepWorldRejectsUnrepresentablePrecisionLoss},
        {"WorldRangeOverflowIsTransactional", TestWorldRangeOverflowIsTransactional},
        {"SnapshotIsDetachedAndEmptyStateIsStable", TestSnapshotIsDetachedAndEmptyStateIsStable},
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
