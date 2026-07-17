#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "physics_runtime_impl.h"

#include <memory>
#include <type_traits>

namespace
{
using epidemic::foundation::Error;
using epidemic::foundation::Result;
using epidemic::runtime::Aabb;
using epidemic::runtime::GameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;
using epidemic::runtime::physics::BackendBodyHandle;
using epidemic::runtime::physics::BackendBodySnapshot;
using epidemic::runtime::physics::BackendShapeHandle;
using epidemic::runtime::physics::CollisionShapeDesc;
using epidemic::runtime::physics::CollisionShapeId;
using epidemic::runtime::physics::ContactEvent;
using epidemic::runtime::physics::CreatePhysicsServices;
using epidemic::runtime::physics::IPhysicsBackend;
using epidemic::runtime::physics::ICollisionShapeRegistry;
using epidemic::runtime::physics::IPhysicsEventBuffer;
using epidemic::runtime::physics::IPhysicsQuery;
using epidemic::runtime::physics::IPhysicsScene;
using epidemic::runtime::physics::IPhysicsStepper;
using epidemic::runtime::physics::IPhysicsTransformSink;
using epidemic::runtime::physics::IPhysicsTransformSource;
using epidemic::runtime::physics::OverlapQuery;
using epidemic::runtime::physics::PhysicsActivityState;
using epidemic::runtime::physics::PhysicsBackendOptions;
using epidemic::runtime::physics::PhysicsBodyDesc;
using epidemic::runtime::physics::PhysicsBodyHandle;
using epidemic::runtime::physics::PhysicsBodyId;
using epidemic::runtime::physics::PhysicsBodyLifecycle;
using epidemic::runtime::physics::PhysicsBodySnapshot;
using epidemic::runtime::physics::PhysicsBodyType;
using epidemic::runtime::physics::PhysicsDirtyFlags;
using epidemic::runtime::physics::PhysicsRuntime;
using epidemic::runtime::physics::PhysicsTransformId;
using epidemic::runtime::physics::RaycastHit;
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
    desc.transform_node = PhysicsTransformId{7};
    desc.shape = shape;
    desc.type = type;
    desc.mass = type == PhysicsBodyType::Static ? 0.0f : 5.0f;
    return desc;
}

class FailingBackend final : public IPhysicsBackend
{
  public:
    Result<void> Initialize(const PhysicsBackendOptions&) override { return Result<void>::Success(); }
    Result<BackendShapeHandle> CreateShape(const CollisionShapeDesc&) override { return Result<BackendShapeHandle>::Success(BackendShapeHandle{1}); }
    Result<BackendBodyHandle> CreateBody(const PhysicsBodyDesc&, BackendShapeHandle) override { return Result<BackendBodyHandle>::Success(BackendBodyHandle{2}); }
    Result<void> DestroyBody(BackendBodyHandle) override { return Result<void>::Success(); }
    Result<void> ApplyImpulse(BackendBodyHandle, const Vec3&) override { return Result<void>::Success(); }
    Result<void> SimulateFixed(GameDuration) override
    {
        return Result<void>::Failure(Error::Create("physics.backend_failed", "backend failed for test"));
    }
    Result<BackendBodySnapshot> GetBodySnapshot(BackendBodyHandle) const override { return Result<BackendBodySnapshot>::Success({}); }
    Result<RaycastHit> Raycast(const RaycastQuery&) const override { return Result<RaycastHit>::Success({}); }
};

class TransformAdapter final : public IPhysicsTransformSource, public IPhysicsTransformSink
{
  public:
    Result<Transform> ReadTransform(PhysicsTransformId) const override
    {
        ++reads;
        Transform transform{};
        transform.position = Vec3{3.0f, 4.0f, 5.0f};
        return Result<Transform>::Success(transform);
    }

    Result<void> WriteTransform(PhysicsTransformId, const Transform&) override
    {
        ++writes;
        return Result<void>::Success();
    }

    mutable int reads = 0;
    int writes = 0;
};

bool TestGenerationSnapshotAndDestroyLifecycle()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{1};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }

    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body || !body.Value().IsValid())
    {
        return false;
    }
    const auto snapshot = runtime.GetBodySnapshot(body.Value());
    if (!snapshot || snapshot.Value().handle != body.Value() ||
        snapshot.Value().lifecycle != PhysicsBodyLifecycle::Alive ||
        snapshot.Value().activity != PhysicsActivityState::Awake ||
        snapshot.Value().type != PhysicsBodyType::Dynamic)
    {
        return false;
    }

    const PhysicsBodyHandle stale{body.Value().id, body.Value().generation + 1};
    const auto stale_snapshot = runtime.GetBodySnapshot(stale);
    const auto destroy = runtime.DestroyBody(body.Value());
    const auto unknown_snapshot = runtime.GetBodySnapshot(body.Value());
    return !stale_snapshot && stale_snapshot.GetError().HasCode("physics.stale_handle") &&
           destroy && !unknown_snapshot && unknown_snapshot.GetError().HasCode("physics.unknown_handle");
}

bool TestUnknownHandleAndImpulseRules()
{
    PhysicsRuntime runtime;
    const auto invalid = runtime.ApplyImpulse(PhysicsBodyHandle{PhysicsBodyId{999}, 1}, Vec3{1.0f, 0.0f, 0.0f});
    if (invalid || !invalid.GetError().HasCode("physics.unknown_handle"))
    {
        return false;
    }

    const CollisionShapeId shape{2};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }
    const auto static_body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Static));
    const auto rejected = runtime.ApplyImpulse(static_body.Value(), Vec3{1.0f, 0.0f, 0.0f});
    return static_body && !rejected && rejected.GetError().HasCode("physics.impulse_not_allowed") &&
           runtime.Contacts().empty();
}

bool TestFixedStepAccumulatorAndMaxSubsteps()
{
    PhysicsRuntime runtime;
    runtime.SetFixedStep(GameDuration{10});
    runtime.SetMaxSubsteps(2);
    const auto first = runtime.Tick(GameDuration{25});
    const auto second = runtime.Tick(GameDuration{25});
    const auto invalid = runtime.StepFixed(GameDuration{0});
    return first && first.Value().substeps == 2 && first.Value().step_index == 2 &&
           first.Value().accumulated_time.ticks == 5 &&
           second && second.Value().substeps == 2 && second.Value().dropped_time.ticks == 10 &&
           !invalid && invalid.GetError().HasCode("physics.invalid_step");
}

bool TestBackendFailurePropagation()
{
    auto backend = std::make_shared<FailingBackend>();
    PhysicsRuntime runtime(backend);
    const auto step = runtime.StepFixed(GameDuration{1});
    return !step && step.GetError().HasCode("physics.backend_failed");
}

bool TestTransformSourceAndSinkSynchronization()
{
    PhysicsRuntime runtime;
    TransformAdapter adapter;
    runtime.SetTransformSource(&adapter);
    runtime.SetTransformSink(&adapter);
    const CollisionShapeId shape{3};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }
    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    const auto step = runtime.StepFixed(GameDuration{1});
    const auto snapshot = body ? runtime.GetBodySnapshot(body.Value()) : Result<PhysicsBodySnapshot>::Failure(Error::Create("test.no_body", "missing"));
    return body && step && snapshot && adapter.reads == 1 && adapter.writes == 1 &&
           snapshot.Value().world_transform.position.x == 3.0f;
}

bool TestContactBufferStoresExplicitBeginPersistEnd()
{
    PhysicsRuntime runtime;
    ContactEvent begin{};
    begin.a = PhysicsBodyHandle{PhysicsBodyId{1}, 1};
    begin.b = PhysicsBodyHandle{PhysicsBodyId{2}, 1};
    begin.state = epidemic::runtime::physics::PhysicsEventState::Begin;
    ContactEvent persist = begin;
    persist.state = epidemic::runtime::physics::PhysicsEventState::Persist;
    ContactEvent end = begin;
    end.state = epidemic::runtime::physics::PhysicsEventState::End;
    runtime.QueueContact(begin);
    runtime.QueueContact(persist);
    runtime.QueueContact(end);
    const auto contacts = runtime.Contacts();
    if (contacts.size() != 3 || contacts[0].state != epidemic::runtime::physics::PhysicsEventState::Begin ||
        contacts[1].state != epidemic::runtime::physics::PhysicsEventState::Persist ||
        contacts[2].state != epidemic::runtime::physics::PhysicsEventState::End)
    {
        return false;
    }
    runtime.Clear();
    return runtime.Contacts().empty();
}

bool TestRaycastRespectsDirectionAndMaxDistance()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{4};
    if (!RegisterDefaultShape(runtime, shape, 2.0f))
    {
        return false;
    }
    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Static));
    const auto hit = runtime.Raycast(RaycastQuery{Vec3{-5.0f, 1.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}, 10.0f});
    const auto miss_direction = runtime.Raycast(RaycastQuery{Vec3{-5.0f, 1.0f, 1.0f}, Vec3{-1.0f, 0.0f, 0.0f}, 10.0f});
    const auto miss_distance = runtime.Raycast(RaycastQuery{Vec3{-5.0f, 1.0f, 1.0f}, Vec3{1.0f, 0.0f, 0.0f}, 2.0f});
    return body && hit && hit.Value().hit && hit.Value().body == body.Value() &&
           miss_direction && !miss_direction.Value().hit &&
           miss_distance && !miss_distance.Value().hit;
}

bool TestDirtyFlagsAndServicesFactory()
{
    const auto flags = PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Shape;
    const auto services = CreatePhysicsServices();
    return HasFlag(flags, PhysicsDirtyFlags::Transform) && HasFlag(flags, PhysicsDirtyFlags::Shape) &&
           !HasFlag(flags, PhysicsDirtyFlags::Material) && services.shapes != nullptr &&
           services.scene != nullptr && services.stepper != nullptr && services.query != nullptr &&
           services.events != nullptr && services.backend != nullptr;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<ICollisionShapeRegistry>);
    static_assert(std::is_abstract_v<IPhysicsScene>);
    static_assert(std::is_abstract_v<IPhysicsStepper>);
    static_assert(std::is_abstract_v<IPhysicsBackend>);
    static_assert(std::is_abstract_v<IPhysicsTransformSource>);
    static_assert(std::is_abstract_v<IPhysicsTransformSink>);
    static_assert(std::is_abstract_v<IPhysicsQuery>);
    static_assert(std::is_abstract_v<IPhysicsEventBuffer>);
    static_assert(std::is_same_v<decltype(PhysicsBodyHandle{}.generation), std::uint32_t>);

    if (!TestGenerationSnapshotAndDestroyLifecycle()) return 1;
    if (!TestUnknownHandleAndImpulseRules()) return 2;
    if (!TestFixedStepAccumulatorAndMaxSubsteps()) return 3;
    if (!TestBackendFailurePropagation()) return 4;
    if (!TestTransformSourceAndSinkSynchronization()) return 5;
    if (!TestContactBufferStoresExplicitBeginPersistEnd()) return 6;
    if (!TestRaycastRespectsDirectionAndMaxDistance()) return 7;
    if (!TestDirtyFlagsAndServicesFactory()) return 8;
    return 0;
}
