#include "scene_runtime_impl.h"

#include "Epidemic/Runtime/Scene/bounds.h"
#include "Epidemic/Runtime/Scene/scene_node.h"
#include "Epidemic/Runtime/Scene/scene_node_registry.h"
#include "Epidemic/Runtime/Scene/scene_query.h"
#include "Epidemic/Runtime/Scene/scene_state.h"
#include "Epidemic/Runtime/Scene/spatial_index.h"
#include "Epidemic/Runtime/Scene/transform.h"
#include "Epidemic/Runtime/Scene/transform_registry.h"

#include <type_traits>

namespace
{
using epidemic::runtime::Aabb;
using epidemic::runtime::ITransformRegistry;
using epidemic::runtime::ISceneNodeRegistry;
using epidemic::runtime::ISceneQuery;
using epidemic::runtime::ISpatialIndex;
using epidemic::runtime::Quat;
using epidemic::runtime::SceneNode;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::SceneNodeState;
using epidemic::runtime::SceneRuntime;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;

bool ContainsNode(const std::vector<SceneNodeId>& nodes, SceneNodeId target)
{
    for (const SceneNodeId node : nodes)
    {
        if (node == target)
        {
            return true;
        }
    }

    return false;
}

bool TestDefaultTransformHasIdentityRotationAndUnitScale()
{
    const Transform transform{};
    return transform.position == Vec3{} && transform.rotation == Quat{} && transform.scale == Vec3{1.0f, 1.0f, 1.0f};
}

bool TestDefaultBoundsAreZeroed()
{
    const Aabb bounds{};
    return bounds.min == Vec3{} && bounds.max == Vec3{};
}

bool TestSceneNodeIdStartsInvalid()
{
    const SceneNodeId id{};
    return !id.IsValid();
}

bool TestSceneNodeDefaultsToDetached()
{
    const SceneNode node{};
    return !node.id.IsValid() && node.state == SceneNodeState::Detached;
}

bool TestCreateNodeReturnsValidId()
{
    SceneRuntime runtime;
    const auto result = runtime.CreateNode();
    return result.HasValue() && result.Value().IsValid() && runtime.Exists(result.Value());
}

bool TestDestroyNodeRemovesNode()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created.HasValue())
    {
        return false;
    }

    const SceneNodeId node = created.Value();
    const auto destroyed = runtime.DestroyNode(node);
    return destroyed.HasValue() && !runtime.Exists(node);
}

bool TestSetGetTransform()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created.HasValue())
    {
        return false;
    }

    const Transform expected{Vec3{1.0f, 2.0f, 3.0f}, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Vec3{2.0f, 2.0f, 2.0f}};
    const auto set_result = runtime.SetTransform(created.Value(), expected);
    const auto transform = runtime.GetTransform(created.Value());

    return set_result.HasValue() && transform.has_value() && transform.value() == expected;
}

bool TestTransformDirtyFlagSetAndClear()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created.HasValue())
    {
        return false;
    }

    const SceneNodeId node = created.Value();
    const auto set_result = runtime.SetTransform(node, Transform{});
    if (!set_result.HasValue() || !runtime.IsTransformDirty(node))
    {
        return false;
    }

    runtime.MarkClean(node);
    return !runtime.IsTransformDirty(node);
}

bool TestSetGetBounds()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created.HasValue())
    {
        return false;
    }

    const Aabb expected{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    const auto set_result = runtime.SetBounds(created.Value(), expected);
    const auto bounds = runtime.GetBounds(created.Value());

    return set_result.HasValue() && bounds.has_value() && bounds.value() == expected;
}

bool TestBoundsDirtyFlagSetAndClear()
{
    SceneRuntime runtime;
    const auto created = runtime.CreateNode();
    if (!created.HasValue())
    {
        return false;
    }

    const SceneNodeId node = created.Value();
    const auto set_result = runtime.SetBounds(node, Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}});
    if (!set_result.HasValue() || !runtime.IsBoundsDirty(node))
    {
        return false;
    }

    runtime.MarkBoundsClean(node);
    return !runtime.IsBoundsDirty(node);
}

bool TestQueryAabbReturnsExpectedNodes()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    if (!first.HasValue() || !second.HasValue())
    {
        return false;
    }

    const auto first_bounds = runtime.SetBounds(first.Value(), Aabb{Vec3{0.0f, 0.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f}});
    const auto second_bounds = runtime.SetBounds(second.Value(), Aabb{Vec3{10.0f, 10.0f, 10.0f}, Vec3{12.0f, 12.0f, 12.0f}});
    if (!first_bounds.HasValue() || !second_bounds.HasValue())
    {
        return false;
    }

    const auto result = runtime.QueryAabb(Aabb{Vec3{1.0f, 1.0f, 1.0f}, Vec3{3.0f, 3.0f, 3.0f}});
    return ContainsNode(result, first.Value()) && !ContainsNode(result, second.Value());
}

bool TestQuerySphereReturnsExpectedNodes()
{
    SceneRuntime runtime;
    const auto first = runtime.CreateNode();
    const auto second = runtime.CreateNode();
    if (!first.HasValue() || !second.HasValue())
    {
        return false;
    }

    const auto first_bounds = runtime.SetBounds(first.Value(), Aabb{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}});
    const auto second_bounds = runtime.SetBounds(second.Value(), Aabb{Vec3{8.0f, 8.0f, 8.0f}, Vec3{10.0f, 10.0f, 10.0f}});
    if (!first_bounds.HasValue() || !second_bounds.HasValue())
    {
        return false;
    }

    const auto result = runtime.QuerySphere(Vec3{0.0f, 0.0f, 0.0f}, 2.5f);
    return ContainsNode(result, first.Value()) && !ContainsNode(result, second.Value());
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

    if (!TestDefaultTransformHasIdentityRotationAndUnitScale())
    {
        return 1;
    }

    if (!TestDefaultBoundsAreZeroed())
    {
        return 2;
    }

    if (!TestSceneNodeIdStartsInvalid())
    {
        return 3;
    }

    if (!TestSceneNodeDefaultsToDetached())
    {
        return 4;
    }

    if (!TestCreateNodeReturnsValidId())
    {
        return 5;
    }

    if (!TestDestroyNodeRemovesNode())
    {
        return 6;
    }

    if (!TestSetGetTransform())
    {
        return 7;
    }

    if (!TestTransformDirtyFlagSetAndClear())
    {
        return 8;
    }

    if (!TestSetGetBounds())
    {
        return 9;
    }

    if (!TestBoundsDirtyFlagSetAndClear())
    {
        return 10;
    }

    if (!TestQueryAabbReturnsExpectedNodes())
    {
        return 11;
    }

    if (!TestQuerySphereReturnsExpectedNodes())
    {
        return 12;
    }

    return 0;
}
