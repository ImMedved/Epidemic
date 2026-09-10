#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "physics_runtime_impl.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
using epidemic::foundation::Error;
using epidemic::foundation::Result;
using epidemic::runtime::Aabb;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::RuntimeFrameDuration;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;
using epidemic::runtime::physics::BackendBodyHandle;
using epidemic::runtime::physics::BackendBodySnapshot;
using epidemic::runtime::physics::BackendContactEvent;
using epidemic::runtime::physics::BackendRaycastHit;
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
using epidemic::runtime::physics::PhysicsBodySnapshot;
using epidemic::runtime::physics::PhysicsBodyType;
using epidemic::runtime::physics::PhysicsDependencies;
using epidemic::runtime::physics::PhysicsDirtyFlags;
using epidemic::runtime::physics::PhysicsOptions;
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
    Result<void> DestroyShape(BackendShapeHandle) override { return Result<void>::Success(); }
    Result<BackendBodyHandle> CreateBody(const PhysicsBodyDesc&, BackendShapeHandle) override { return Result<BackendBodyHandle>::Success(BackendBodyHandle{2}); }
    Result<void> DestroyBody(BackendBodyHandle) override { return Result<void>::Success(); }
    Result<void> ApplyImpulse(BackendBodyHandle, const Vec3&) override { return Result<void>::Success(); }
    Result<void> SimulateFixed(RuntimeFrameDuration) override
    {
        return Result<void>::Failure(Error::Create("physics.backend_failed", "backend failed for test"));
    }
    Result<BackendBodySnapshot> GetBodySnapshot(BackendBodyHandle) const override { return Result<BackendBodySnapshot>::Success({}); }
    Result<BackendRaycastHit> RaycastBackend(const RaycastQuery&) const override { return Result<BackendRaycastHit>::Success({}); }
    Result<std::vector<BackendContactEvent>> ConsumeContactEvents() override { return Result<std::vector<BackendContactEvent>>::Success({}); }
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
        if (fail_write)
        {
            return Result<void>::Failure(Error::Create("physics.transform_write_failed", "transform write failed for test"));
        }
        return Result<void>::Success();
    }

    mutable int reads = 0;
    int writes = 0;
    bool fail_write = false;
};

class JournalBackend final : public IPhysicsBackend
{
  public:
    Result<void> Initialize(const PhysicsBackendOptions&) override
    {
        ++initializes;
        if (fail_initialize)
        {
            return Result<void>::Failure(Error::Create("physics.init_failed", "init failed for test"));
        }
        return Result<void>::Success();
    }

    Result<BackendShapeHandle> CreateShape(const CollisionShapeDesc&) override
    {
        ++create_shapes;
        if (throw_create_shape) throw std::runtime_error("create shape exception");
        if (fail_create_shape) return Result<BackendShapeHandle>::Failure(Error::Create("physics.create_shape_failed", "create shape failed for test"));
        return Result<BackendShapeHandle>::Success(duplicate_shape_handles ? shape : BackendShapeHandle{shape.value + static_cast<std::uint64_t>(create_shapes - 1)});
    }

    Result<void> DestroyShape(BackendShapeHandle handle) override
    {
        ++destroy_shapes;
        last_destroyed_shape = handle;
        if (throw_destroy_shape) throw std::runtime_error("destroy shape exception");
        if (fail_destroy_shape)
        {
            return Result<void>::Failure(Error::Create("physics.destroy_shape_failed", "destroy shape failed for test"));
        }
        return Result<void>::Success();
    }

    Result<BackendBodyHandle> CreateBody(const PhysicsBodyDesc& desc, BackendShapeHandle handle) override
    {
        ++create_bodies;
        last_shape_for_body = handle;
        last_initial_transform = desc.initial_transform;
        if (throw_create_body) throw std::runtime_error("create body exception");
        if (fail_create_body) return Result<BackendBodyHandle>::Failure(Error::Create("physics.create_body_failed", "create body failed for test"));
        return Result<BackendBodyHandle>::Success(duplicate_body_handles ? body : BackendBodyHandle{body.value + static_cast<std::uint64_t>(create_bodies)});
    }

    Result<void> DestroyBody(BackendBodyHandle handle) override
    {
        ++destroy_bodies;
        last_destroyed_body = handle;
        if (throw_destroy_body) throw std::runtime_error("destroy body exception");
        if (fail_destroy_body)
        {
            return Result<void>::Failure(Error::Create("physics.destroy_body_failed", "destroy body failed for test"));
        }
        return Result<void>::Success();
    }

    Result<void> ApplyImpulse(BackendBodyHandle, const Vec3&) override
    {
        ++impulses;
        if (throw_impulse) throw std::runtime_error("impulse exception");
        if (fail_impulse) return Result<void>::Failure(Error::Create("physics.impulse_failed", "impulse failed for test"));
        return Result<void>::Success();
    }

    Result<void> SimulateFixed(RuntimeFrameDuration) override
    {
        ++simulates;
        if (throw_simulate) throw std::runtime_error("simulate exception");
        if (fail_simulate) return Result<void>::Failure(Error::Create("physics.simulate_failed", "simulate failed for test"));
        snapshot.world_transform.position.x += 2.0f;
        return Result<void>::Success();
    }

    Result<BackendBodySnapshot> GetBodySnapshot(BackendBodyHandle) const override
    {
        ++snapshots;
        if (throw_snapshot) throw std::runtime_error("snapshot exception");
        if (fail_snapshot || (fail_snapshot_at != 0 && snapshots == fail_snapshot_at))
        {
            return Result<BackendBodySnapshot>::Failure(Error::Create("physics.snapshot_failed", "snapshot failed for test"));
        }
        return Result<BackendBodySnapshot>::Success(snapshot);
    }

    Result<BackendRaycastHit> RaycastBackend(const RaycastQuery& query) const override
    {
        ++raycasts;
        last_raycast_direction = query.direction;
        return Result<BackendRaycastHit>::Success(raycast_hit);
    }

    Result<std::vector<BackendContactEvent>> ConsumeContactEvents() override
    {
        ++contact_consumes;
        std::vector<BackendContactEvent> result = std::move(contact_events);
        contact_events.clear();
        return Result<std::vector<BackendContactEvent>>::Success(std::move(result));
    }

    bool fail_initialize = false;
    bool fail_create_shape = false;
    bool fail_create_body = false;
    bool fail_destroy_body = false;
    bool fail_destroy_shape = false;
    bool fail_impulse = false;
    bool fail_simulate = false;
    bool fail_snapshot = false;
    bool throw_create_shape = false;
    bool throw_create_body = false;
    bool throw_destroy_body = false;
    bool throw_destroy_shape = false;
    bool throw_impulse = false;
    bool throw_simulate = false;
    bool throw_snapshot = false;
    bool duplicate_shape_handles = false;
    bool duplicate_body_handles = false;
    int fail_snapshot_at = 0;
    BackendShapeHandle shape{101};
    BackendBodyHandle body{202};
    BackendShapeHandle last_shape_for_body{};
    BackendShapeHandle last_destroyed_shape{};
    BackendBodyHandle last_destroyed_body{};
    Transform last_initial_transform{};
    mutable int initializes = 0;
    int create_shapes = 0;
    int create_bodies = 0;
    int destroy_bodies = 0;
    int destroy_shapes = 0;
    int impulses = 0;
    int simulates = 0;
    mutable int snapshots = 0;
    mutable int raycasts = 0;
    int contact_consumes = 0;
    mutable Vec3 last_raycast_direction{};
    BackendBodySnapshot snapshot{};
    BackendRaycastHit raycast_hit{};
    std::vector<BackendContactEvent> contact_events{};
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
    runtime.SetFixedStep(RuntimeFrameDuration{std::chrono::microseconds{10}});
    runtime.SetMaxSubsteps(2);
    const auto first = runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{25}});
    const auto second = runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{25}});
    const auto invalid = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{0}});
    return first && first.Value().substeps == 2 && first.Value().step_index == 2 &&
           first.Value().accumulated_time.value.count() == 5 &&
           second && second.Value().substeps == 2 && second.Value().dropped_time.value.count() == 10 &&
           !invalid && invalid.GetError().HasCode("physics.invalid_step");
}

bool TestDefaultAndConfiguredFixedStep()
{
    PhysicsRuntime runtime;
    const auto default_tick = runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{16667}});
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr, PhysicsOptions{RuntimeFrameDuration{std::chrono::microseconds{10}}, 2}});
    if (!default_tick || default_tick.Value().substeps != 1 || !services)
    {
        return false;
    }

    const auto configured_tick = services.Value().stepper->Tick(RuntimeFrameDuration{std::chrono::microseconds{25}});
    return configured_tick && configured_tick.Value().fixed_delta.value.count() == 10 &&
           configured_tick.Value().substeps == 2 && configured_tick.Value().accumulated_time.value.count() == 5 &&
           configured_tick.Value().dropped_time.value.count() == 0;
}

bool TestInitialTransformFallsBackToDescWithoutSource()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    const CollisionShapeId shape{11};
    if (!runtime || !runtime->RegisterShape({shape, Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}))
    {
        return false;
    }

    PhysicsBodyDesc desc = MakeBodyDesc(shape, PhysicsBodyType::Dynamic);
    desc.initial_transform.position = Vec3{8.0f, 9.0f, 10.0f};
    const auto body = runtime->CreateBody(desc);
    return body && backend->last_initial_transform.position.x == 8.0f &&
           backend->last_initial_transform.position.y == 9.0f &&
           backend->last_initial_transform.position.z == 10.0f;
}

bool TestUnregisterShapeRejectsInUseAndDestroysFreeShape()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    const CollisionShapeId shape{12};
    if (!runtime || !runtime->RegisterShape({shape, Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}))
    {
        return false;
    }

    const auto body = runtime->CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Static));
    const auto rejected = runtime->UnregisterShape(shape);
    const auto destroyed = body ? runtime->DestroyBody(body.Value()) : Result<void>::Failure(Error::Create("test.no_body", "body missing"));
    const auto unregistered = runtime->UnregisterShape(shape);
    return body && !rejected && rejected.GetError().HasCode("physics.shape_in_use") &&
           destroyed && unregistered && !runtime->HasShape(shape) && backend->destroy_shapes == 1;
}

bool TestShutdownBestEffortAndIdempotent()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !runtime->RegisterShape({CollisionShapeId{13}, Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}) ||
        !runtime->RegisterShape({CollisionShapeId{14}, Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}))
    {
        return false;
    }

    backend->fail_destroy_shape = true;
    const auto shutdown = runtime->Shutdown();
    const int first_destroy_count = backend->destroy_shapes;
    backend->fail_destroy_shape = false;
    const auto second_shutdown = runtime->Shutdown();
    return !shutdown && shutdown.GetError().HasCode("physics.destroy_shape_failed") &&
           first_destroy_count == 2 && second_shutdown && backend->destroy_shapes == first_destroy_count + 2;
}

bool TestBackendFailurePropagation()
{
    auto backend = std::make_shared<FailingBackend>();
    PhysicsRuntime runtime(backend);
    const auto step = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
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
    const auto step = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
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

bool TestExternalBackendLifecycleAndSynchronization()
{
    auto backend = std::make_shared<JournalBackend>();
    backend->snapshot.activity = PhysicsActivityState::Awake;
    backend->snapshot.world_transform.position = Vec3{10.0f, 0.0f, 0.0f};
    backend->raycast_hit.hit = true;
    bool ok = false;
    {
        auto adapter = std::make_shared<TransformAdapter>();
        const auto services = CreatePhysicsServices(PhysicsDependencies{backend, adapter, adapter});
        if (!services || backend->initializes != 1)
        {
            return false;
        }

        auto runtime = std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene);
        if (!runtime)
        {
            return false;
        }

        const CollisionShapeId shape{9};
        if (!runtime->RegisterShape({shape, Aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}}}))
        {
            return false;
        }
        const auto body = runtime->CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
        if (!body || backend->create_shapes != 1 || backend->create_bodies != 1 || backend->last_shape_for_body != backend->shape)
        {
            return false;
        }
        backend->raycast_hit.body = BackendBodyHandle{backend->body.value + 1u};

        const auto step = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
        const auto snapshot = runtime->GetBodySnapshot(body.Value());
        const auto raycast = runtime->Raycast(RaycastQuery{Vec3{0.0f, 0.0f, 0.0f}, Vec3{2.0f, 0.0f, 0.0f}, 5.0f});
        const auto destroyed = runtime->DestroyBody(body.Value());
        ok = step && snapshot && raycast && backend->simulates == 1 && backend->snapshots >= 2 && adapter->writes == 1 &&
             snapshot.Value().world_transform.position.x == 12.0f && backend->raycasts == 1 &&
             backend->last_initial_transform.position.x == 3.0f && backend->last_raycast_direction.x == 1.0f && destroyed && backend->destroy_bodies >= 1 &&
             services.Value().backend == backend;
    }
    return ok && backend->destroy_shapes == 1 && backend->last_destroyed_shape == backend->shape;
}

bool TestExternalBackendContactsArePublished()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{15}))
    {
        return false;
    }
    const auto first = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{15}, PhysicsBodyType::Dynamic));
    const auto second = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{15}, PhysicsBodyType::Dynamic));
    if (!first || !second)
    {
        return false;
    }
    BackendContactEvent event{};
    event.a = BackendBodyHandle{backend->body.value + 1u};
    event.b = BackendBodyHandle{backend->body.value + 2u};
    event.state = epidemic::runtime::physics::PhysicsEventState::Begin;
    backend->contact_events.push_back(event);
    const auto step = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return step && backend->contact_consumes == 1 && runtime->Contacts().size() == 1 &&
           runtime->Contacts()[0].state == epidemic::runtime::physics::PhysicsEventState::Begin;
}

bool TestExternalBackendInvalidContactIsDroppedAtomically()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{16}))
    {
        return false;
    }
    const auto first = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{16}, PhysicsBodyType::Dynamic));
    const auto second = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{16}, PhysicsBodyType::Dynamic));
    if (!first || !second)
    {
        return false;
    }

    BackendContactEvent valid{};
    valid.a = BackendBodyHandle{backend->body.value + 1u};
    valid.b = BackendBodyHandle{backend->body.value + 2u};
    valid.state = epidemic::runtime::physics::PhysicsEventState::Begin;
    BackendContactEvent invalid = valid;
    invalid.b = BackendBodyHandle{99999};
    backend->contact_events.push_back(valid);
    backend->contact_events.push_back(invalid);

    const auto step = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return step && runtime->Contacts().size() == 1 && runtime->Contacts()[0].a == first.Value() &&
           runtime->Contacts()[0].b == second.Value();
}

bool TestExternalBackendRaycastRejectsUnknownBody()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{17}))
    {
        return false;
    }
    const auto body = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{17}, PhysicsBodyType::Dynamic));
    backend->raycast_hit.hit = true;
    backend->raycast_hit.body = BackendBodyHandle{99999};
    const auto raycast = runtime->Raycast(RaycastQuery{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f}, 10.0f});
    return body && !raycast && raycast.GetError().HasCode("physics.unknown_backend_handle");
}

bool TestBackendSyncFailureDoesNotResimulateBeforeRetry()
{
    auto backend = std::make_shared<JournalBackend>();
    backend->fail_snapshot = true;
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{18}))
    {
        return false;
    }
    const auto body = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{18}, PhysicsBodyType::Dynamic));
    const auto failed = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    const int simulates_after_failure = backend->simulates;
    backend->fail_snapshot = false;
    const auto retried = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return body && !failed && failed.GetError().HasCode("physics.snapshot_failed") &&
           retried && simulates_after_failure == 1 && backend->simulates == 1;
}

bool TestInvalidBackendSnapshotDoesNotMutateBody()
{
    auto backend = std::make_shared<JournalBackend>();
    auto adapter = std::make_shared<TransformAdapter>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, adapter, adapter});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{20}))
    {
        return false;
    }
    const auto body = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{20}, PhysicsBodyType::Dynamic));
    const auto before = body ? runtime->GetBodySnapshot(body.Value()) : Result<PhysicsBodySnapshot>::Failure(Error::Create("test.no_body", "missing body"));
    const int writes_before_failed_step = adapter->writes;
    backend->snapshot.world_transform.position.x = std::numeric_limits<float>::quiet_NaN();
    const auto failed = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    const int simulates_after_failure = backend->simulates;
    if (!before.HasValue()) { std::cerr << "before snapshot missing\n"; return false; }
    if (failed.HasValue() || !failed.GetError().HasCode("physics.invalid_backend_snapshot")) { std::cerr << "invalid snapshot did not fail as expected\n"; return false; }
    if (adapter->writes != writes_before_failed_step) { std::cerr << "invalid snapshot reached sink\n"; return false; }
    backend->snapshot.world_transform.position = Vec3{9.0f, 0.0f, 0.0f};
    backend->snapshot.world_transform.rotation = {};
    backend->snapshot.world_transform.scale = Vec3{1.0f, 1.0f, 1.0f};
    backend->snapshot.activity = PhysicsActivityState::Awake;
    const auto retried = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    if (!retried.HasValue()) { std::cerr << "valid retry failed\n"; return false; }
    if (backend->simulates != simulates_after_failure) { std::cerr << "retry simulated again\n"; return false; }
    if (adapter->writes != writes_before_failed_step + 1) { std::cerr << "valid retry did not write sink\n"; return false; }
    return true;
}

bool TestBatchInvalidBackendSnapshotHasNoPartialWrites()
{
    auto backend = std::make_shared<JournalBackend>();
    auto adapter = std::make_shared<TransformAdapter>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, adapter, adapter}};
    const CollisionShapeId shape{37};
    if (!RegisterDefaultShape(runtime, shape))
    {
        return false;
    }

    const auto first = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    const auto second = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    const auto first_before = first ? runtime.GetBodySnapshot(first.Value()) : Result<PhysicsBodySnapshot>::Failure(Error::Create("test.no_body", "missing first"));
    const auto second_before = second ? runtime.GetBodySnapshot(second.Value()) : Result<PhysicsBodySnapshot>::Failure(Error::Create("test.no_body", "missing second"));
    if (!first || !second || !first_before || !second_before)
    {
        return false;
    }

    backend->snapshot.world_transform.position.x = 99.0f;
    backend->snapshots = 0;
    backend->fail_snapshot_at = 2;
    const auto failed = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{10}});
    return !failed && failed.GetError().HasCode("physics.snapshot_failed") &&
           adapter->writes == 0;
}

bool TestInvalidBackendContactPayloadIsFiltered()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{21}))
    {
        return false;
    }
    const auto first = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{21}, PhysicsBodyType::Dynamic));
    const auto second = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{21}, PhysicsBodyType::Dynamic));
    if (!first || !second)
    {
        return false;
    }
    BackendContactEvent valid{};
    valid.a = BackendBodyHandle{backend->body.value + 1u};
    valid.b = BackendBodyHandle{backend->body.value + 2u};
    valid.point = Vec3{1.0f, 2.0f, 3.0f};
    valid.normal = Vec3{0.0f, 1.0f, 0.0f};
    valid.impulse = 2.0f;
    valid.state = epidemic::runtime::physics::PhysicsEventState::Begin;

    BackendContactEvent invalid_nan = valid;
    invalid_nan.point.x = std::numeric_limits<float>::quiet_NaN();
    BackendContactEvent invalid_impulse = valid;
    invalid_impulse.impulse = -1.0f;
    BackendContactEvent invalid_state = valid;
    invalid_state.state = static_cast<epidemic::runtime::physics::PhysicsEventState>(99);
    backend->contact_events = {valid, invalid_nan, invalid_impulse, invalid_state};

    const auto step = runtime->StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return step && runtime->Contacts().size() == 1 &&
           runtime->Contacts()[0].point.x == 1.0f &&
           runtime->Contacts()[0].impulse == 2.0f;
}

bool TestShutdownRetriesBodyBeforeDestroyingShape()
{
    auto backend = std::make_shared<JournalBackend>();
    const auto services = CreatePhysicsServices(PhysicsDependencies{backend, nullptr, nullptr});
    auto runtime = services ? std::dynamic_pointer_cast<PhysicsRuntime>(services.Value().scene) : nullptr;
    if (!runtime || !RegisterDefaultShape(*runtime, CollisionShapeId{19}))
    {
        return false;
    }
    const auto body = runtime->CreateBody(MakeBodyDesc(CollisionShapeId{19}, PhysicsBodyType::Dynamic));
    backend->fail_destroy_body = true;
    const auto failed = runtime->Shutdown();
    const int destroyed_shapes_after_failure = backend->destroy_shapes;
    backend->fail_destroy_body = false;
    const auto retried = runtime->Shutdown();
    return body && !failed && failed.GetError().HasCode("physics.destroy_body_failed") &&
           destroyed_shapes_after_failure == 0 && retried && backend->destroy_bodies >= 2 && backend->destroy_shapes == 1;
}


bool TestInvalidBodyInputsAreRejectedBeforeBackend()
{
    auto backend = std::make_shared<JournalBackend>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, nullptr, nullptr}};
    const CollisionShapeId shape{41};
    if (!RegisterDefaultShape(runtime, shape)) return false;
    const int creates_before = backend->create_bodies;
    const auto rev_before = runtime.RevisionForTesting();

    auto invalid_type = MakeBodyDesc(shape, PhysicsBodyType::Dynamic);
    invalid_type.type = static_cast<PhysicsBodyType>(255);
    const auto type_result = runtime.CreateBody(invalid_type);
    auto invalid_transform = MakeBodyDesc(shape, PhysicsBodyType::Dynamic);
    invalid_transform.initial_transform.position.x = std::numeric_limits<float>::quiet_NaN();
    const auto transform_result = runtime.CreateBody(invalid_transform);
    return !type_result && type_result.GetError().HasCode("physics.invalid_body_type") &&
           !transform_result && transform_result.GetError().HasCode("physics.invalid_transform") &&
           backend->create_bodies == creates_before && runtime.RevisionForTesting() == rev_before;
}

bool TestBackendPublicationRollbackAndDuplicateHandles()
{
    auto backend = std::make_shared<JournalBackend>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, nullptr, nullptr}};
    runtime.FailNextShapePublicationForTesting();
    const auto failed_shape = runtime.RegisterShape({CollisionShapeId{42}, Aabb{{0,0,0},{1,1,1}}});
    if (failed_shape || !failed_shape.GetError().HasCode("physics.allocation_failure") || backend->destroy_shapes != 1 || runtime.HasShape(CollisionShapeId{42}))
        return false;
    if (!RegisterDefaultShape(runtime, CollisionShapeId{42})) return false;

    runtime.FailNextBodyPublicationForTesting();
    const auto rev_before = runtime.RevisionForTesting();
    const int destroy_before = backend->destroy_bodies;
    const auto failed_body = runtime.CreateBody(MakeBodyDesc(CollisionShapeId{42}, PhysicsBodyType::Dynamic));
    if (failed_body || !failed_body.GetError().HasCode("physics.allocation_failure") ||
        backend->destroy_bodies != destroy_before + 1 || runtime.RevisionForTesting() != rev_before)
        return false;

    backend->duplicate_body_handles = true;
    const auto first = runtime.CreateBody(MakeBodyDesc(CollisionShapeId{42}, PhysicsBodyType::Dynamic));
    const auto after_first = runtime.RevisionForTesting();
    const auto second = runtime.CreateBody(MakeBodyDesc(CollisionShapeId{42}, PhysicsBodyType::Dynamic));
    return first && !second && second.GetError().HasCode("physics.duplicate_backend_body_handle") &&
           runtime.RevisionForTesting() == after_first;
}

bool TestRevisionAndAllocatorExhaustionAreAtomic()
{
    auto backend = std::make_shared<JournalBackend>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, nullptr, nullptr}};
    const CollisionShapeId shape{43};
    if (!RegisterDefaultShape(runtime, shape)) return false;
    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body) return false;

    const auto local_before = runtime.GetLocalBodySnapshotForTesting(body.Value());
    runtime.SetRevisionForTesting(std::numeric_limits<std::uint64_t>::max());
    const int impulses_before = backend->impulses;
    const int destroys_before = backend->destroy_bodies;
    const int simulates_before = backend->simulates;
    const auto impulse = runtime.ApplyImpulse(body.Value(), Vec3{1,0,0});
    const auto destroy = runtime.DestroyBody(body.Value());
    const auto step = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    const auto local_after = runtime.GetLocalBodySnapshotForTesting(body.Value());
    if (impulse || destroy || step || backend->impulses != impulses_before || backend->destroy_bodies != destroys_before ||
        backend->simulates != simulates_before || !local_before || !local_after ||
        local_before.Value().world_transform.position != local_after.Value().world_transform.position)
        return false;

    PhysicsRuntime exhausted;
    if (!RegisterDefaultShape(exhausted, CollisionShapeId{44})) return false;
    exhausted.SetAllocatorStateForTesting(0, 1, 1);
    const auto create = exhausted.CreateBody(MakeBodyDesc(CollisionShapeId{44}, PhysicsBodyType::Dynamic));
    return !create && create.GetError().HasCode("physics.body_id_overflow");
}

bool TestTransformProjectionFailureIsRetryableAfterAuthoritativeCommit()
{
    auto backend = std::make_shared<JournalBackend>();
    auto adapter = std::make_shared<TransformAdapter>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, adapter, adapter}};
    const CollisionShapeId shape{45};
    if (!RegisterDefaultShape(runtime, shape)) return false;
    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body) return false;
    adapter->fail_write = true;
    const auto first = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    const auto snapshot = runtime.GetLocalBodySnapshotForTesting(body.Value());
    if (!first || first.Value().pending_transform_projections != 1 || runtime.PendingProjectionCountForTesting() != 1 ||
        !snapshot || snapshot.Value().world_transform.position.x != backend->snapshot.world_transform.position.x)
        return false;
    adapter->fail_write = false;
    const auto second = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return second && runtime.PendingProjectionCountForTesting() == 0 && adapter->writes >= 3;
}

bool TestTimeAndStepCounterOverflowAreRejectedBeforeMutation()
{
    PhysicsRuntime runtime;
    runtime.SetFixedStep(RuntimeFrameDuration{std::chrono::microseconds{10}});
    runtime.SetTimingStateForTesting(RuntimeFrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}},
                                     RuntimeFrameDuration{std::chrono::microseconds{0}});
    const auto accumulator_before = runtime.AccumulatorForTesting();
    const auto overflow = runtime.Tick(RuntimeFrameDuration{std::chrono::microseconds{1}});
    if (overflow || !overflow.GetError().HasCode("physics.time_overflow") || runtime.AccumulatorForTesting().value != accumulator_before.value)
        return false;
    runtime.SetTimingStateForTesting({}, {});
    runtime.SetFixedStepCountForTesting(std::numeric_limits<std::uint64_t>::max());
    const auto step = runtime.StepFixed(RuntimeFrameDuration{std::chrono::microseconds{1}});
    return !step && step.GetError().HasCode("physics.step_overflow");
}

bool TestBackendExceptionsStayInsideResultBoundary()
{
    auto backend = std::make_shared<JournalBackend>();
    PhysicsRuntime runtime{PhysicsDependencies{backend, nullptr, nullptr}};
    backend->throw_create_shape = true;
    const auto shape = runtime.RegisterShape({CollisionShapeId{46}, Aabb{{0,0,0},{1,1,1}}});
    if (shape || !shape.GetError().HasCode("physics.backend_exception")) return false;
    backend->throw_create_shape = false;
    if (!RegisterDefaultShape(runtime, CollisionShapeId{46})) return false;
    const auto body = runtime.CreateBody(MakeBodyDesc(CollisionShapeId{46}, PhysicsBodyType::Dynamic));
    if (!body) return false;
    backend->throw_impulse = true;
    const auto rev = runtime.RevisionForTesting();
    const auto impulse = runtime.ApplyImpulse(body.Value(), Vec3{1,0,0});
    return !impulse && impulse.GetError().HasCode("physics.backend_exception") && runtime.RevisionForTesting() == rev;
}

bool TestDefaultStaleAndRepeatedMutatorHandles()
{
    PhysicsRuntime runtime;
    const CollisionShapeId shape{47};
    if (!RegisterDefaultShape(runtime, shape)) return false;
    const auto body = runtime.CreateBody(MakeBodyDesc(shape, PhysicsBodyType::Dynamic));
    if (!body) return false;
    const PhysicsBodyHandle stale{body.Value().id, body.Value().generation + 1};
    const auto default_impulse = runtime.ApplyImpulse(PhysicsBodyHandle{}, Vec3{1,0,0});
    const auto stale_destroy = runtime.DestroyBody(stale);
    const auto first_destroy = runtime.DestroyBody(body.Value());
    const auto second_destroy = runtime.DestroyBody(body.Value());
    return !default_impulse && !stale_destroy && stale_destroy.GetError().HasCode("physics.stale_handle") &&
           first_destroy && !second_destroy && second_destroy.GetError().HasCode("physics.unknown_handle");
}

bool TestFactoryReportsBackendInitializationFailure()
{
    auto backend = std::make_shared<JournalBackend>();
    backend->fail_initialize = true;
    const auto services = CreatePhysicsServices(backend);
    return !services && services.GetError().HasCode("physics.init_failed");
}

bool TestDirtyFlagsAndServicesFactory()
{
    const auto flags = PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Shape;
    const auto services = CreatePhysicsServices();
    return HasFlag(flags, PhysicsDirtyFlags::Transform) && HasFlag(flags, PhysicsDirtyFlags::Shape) &&
           !HasFlag(flags, PhysicsDirtyFlags::Material) && services &&
           services.Value().shapes != nullptr && services.Value().scene != nullptr &&
           services.Value().stepper != nullptr && services.Value().query != nullptr &&
           services.Value().events != nullptr && services.Value().backend != nullptr;
}

template <typename T, typename = void>
struct HasBackendSnapshotBody : std::false_type
{
};

template <typename T>
struct HasBackendSnapshotBody<T, std::void_t<decltype(std::declval<T>().body)>> : std::true_type
{
};
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
    static_assert(!HasBackendSnapshotBody<BackendBodySnapshot>::value);

    if (!TestGenerationSnapshotAndDestroyLifecycle()) return 1;
    if (!TestUnknownHandleAndImpulseRules()) return 2;
    if (!TestFixedStepAccumulatorAndMaxSubsteps()) return 3;
    if (!TestDefaultAndConfiguredFixedStep()) return 11;
    if (!TestInitialTransformFallsBackToDescWithoutSource()) return 12;
    if (!TestUnregisterShapeRejectsInUseAndDestroysFreeShape()) return 13;
    if (!TestShutdownBestEffortAndIdempotent()) return 14;
    if (!TestBackendFailurePropagation()) return 4;
    if (!TestTransformSourceAndSinkSynchronization()) return 5;
    if (!TestContactBufferStoresExplicitBeginPersistEnd()) return 6;
    if (!TestRaycastRespectsDirectionAndMaxDistance()) return 7;
    if (!TestExternalBackendLifecycleAndSynchronization()) return 8;
    if (!TestExternalBackendContactsArePublished()) return 15;
    if (!TestExternalBackendInvalidContactIsDroppedAtomically()) return 16;
    if (!TestExternalBackendRaycastRejectsUnknownBody()) return 17;
    if (!TestBackendSyncFailureDoesNotResimulateBeforeRetry()) return 18;
    if (!TestInvalidBackendSnapshotDoesNotMutateBody()) return 20;
    if (!TestInvalidBackendContactPayloadIsFiltered()) return 21;
    if (!TestBatchInvalidBackendSnapshotHasNoPartialWrites()) return 22;
    if (!TestShutdownRetriesBodyBeforeDestroyingShape()) return 19;
    if (!TestFactoryReportsBackendInitializationFailure()) return 9;
    if (!TestDirtyFlagsAndServicesFactory()) return 10;
    if (!TestInvalidBodyInputsAreRejectedBeforeBackend()) return 23;
    if (!TestBackendPublicationRollbackAndDuplicateHandles()) return 24;
    if (!TestRevisionAndAllocatorExhaustionAreAtomic()) return 25;
    if (!TestTransformProjectionFailureIsRetryableAfterAuthoritativeCommit()) return 26;
    if (!TestTimeAndStepCounterOverflowAreRejectedBeforeMutation()) return 27;
    if (!TestBackendExceptionsStayInsideResultBoundary()) return 28;
    if (!TestDefaultStaleAndRepeatedMutatorHandles()) return 29;
    return 0;
}
