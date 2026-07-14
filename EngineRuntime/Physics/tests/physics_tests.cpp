#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "physics_runtime_impl.h"

#include <type_traits>

namespace
{
using epidemic::runtime::Aabb;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::Vec3;
using epidemic::runtime::physics::CollisionShapeId;
using epidemic::runtime::physics::ContactEvent;
using epidemic::runtime::physics::ICollisionShapeRegistry;
using epidemic::runtime::physics::IPhysicsEventBuffer;
using epidemic::runtime::physics::IPhysicsQuery;
using epidemic::runtime::physics::IPhysicsScene;
using epidemic::runtime::physics::OverlapQuery;
using epidemic::runtime::physics::PhysicsBodyDesc;
using epidemic::runtime::physics::PhysicsBodyId;
using epidemic::runtime::physics::PhysicsBodyState;
using epidemic::runtime::physics::PhysicsBodyType;
using epidemic::runtime::physics::PhysicsRuntime;
using epidemic::runtime::physics::RaycastQuery;

bool RegisterDefaultShape(PhysicsRuntime& runtime, CollisionShapeId id, float max_extent = 1.0f)
{
    const auto result = runtime.RegisterShape({id, Aabb{{0.0f, 0.0f, 0.0f}, {max_extent, max_extent, max_extent}}});
    return static_cast<bool>(result);
}

PhysicsBodyDesc MakeBodyDesc(CollisionShapeId shape, PhysicsBodyType type)
{
    PhysicsBodyDesc desc{};
    desc.owner = RuntimeObjectId{42};
    desc.transform_node = SceneNodeId{7};
    desc.shape = shape;
    desc.type = type;
    desc.mass = type == PhysicsBodyType::Static ? 0.0f : 5.0f;
    return desc;
}

bool TestCreateAndDestroyBody()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{1};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }

    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body || runtime.GetBodyState(body.Value()) != PhysicsBodyState::Dynamic)
    {
        return false;
    }

    const auto destroy = runtime.DestroyBody(body.Value());
    return destroy && runtime.GetBodyState(body.Value()) == PhysicsBodyState::PendingDestroy;
}

bool TestApplyImpulseOnInvalidBodyReturnsError()
{
    PhysicsRuntime runtime;
    const auto result = runtime.ApplyImpulse(PhysicsBodyId{999}, Vec3{1.0f, 0.0f, 0.0f});
    return !result && result.GetError().HasCode("physics.body_not_found");
}

bool TestStateTransitionsOnImpulse()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{2};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }

    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body)
    {
        return false;
    }

    const auto impulse = runtime.ApplyImpulse(body.Value(), Vec3{3.0f, 0.0f, 0.0f});
    return impulse && runtime.GetBodyState(body.Value()) == PhysicsBodyState::Active;
}

bool TestQueriesReturnDeterministicResults()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{3};
    if (!RegisterDefaultShape(runtime, shape, 2.0f))
    {
        return false;
    }

    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Static));
    if (!body)
    {
        return false;
    }

    const auto raycast = runtime.Raycast(RaycastQuery{Vec3{1.0f, 1.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}, 10.0f});
    const auto overlap = runtime.Overlap(OverlapQuery{Aabb{{1.0f, 1.0f, 1.0f}, {3.0f, 3.0f, 3.0f}}});
    return raycast && raycast.Value().hit && raycast.Value().body == body.Value() &&
           overlap && overlap.Value().bodies.size() == 1 && overlap.Value().bodies.front() == body.Value();
}

bool TestEventBufferStoresAndClearsContacts()
{
    PhysicsRuntime runtime;
    runtime.QueueContact(ContactEvent{PhysicsBodyId{1}, PhysicsBodyId{2}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, 5.0f});
    if (runtime.Contacts().size() != 1)
    {
        return false;
    }

    runtime.Clear();
    return runtime.Contacts().empty();
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<ICollisionShapeRegistry>);
    static_assert(std::is_abstract_v<IPhysicsScene>);
    static_assert(std::is_abstract_v<IPhysicsQuery>);
    static_assert(std::is_abstract_v<IPhysicsEventBuffer>);

    if (!TestCreateAndDestroyBody())
    {
        return 1;
    }

    if (!TestApplyImpulseOnInvalidBodyReturnsError())
    {
        return 2;
    }

    if (!TestStateTransitionsOnImpulse())
    {
        return 3;
    }

    if (!TestQueriesReturnDeterministicResults())
    {
        return 4;
    }

    if (!TestEventBufferStoresAndClearsContacts())
    {
        return 5;
    }

    return 0;
}
