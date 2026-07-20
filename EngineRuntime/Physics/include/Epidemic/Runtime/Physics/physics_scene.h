#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

#include <memory>
#include <vector>

namespace epidemic::runtime::physics
{
// Public contracts for creating and managing physics bodies and collision shapes.
class ICollisionShapeRegistry
{
  public:
    virtual ~ICollisionShapeRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterShape(const CollisionShapeDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> UnregisterShape(CollisionShapeId id) = 0;
    [[nodiscard]] virtual bool HasShape(CollisionShapeId id) const = 0;
};

class IPhysicsScene
{
  public:
    virtual ~IPhysicsScene() = default;

    [[nodiscard]] virtual foundation::Result<PhysicsBodyHandle> CreateBody(const PhysicsBodyDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyBody(PhysicsBodyHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> ApplyImpulse(PhysicsBodyHandle handle, Vec3 impulse) = 0;
    [[nodiscard]] virtual foundation::Result<PhysicsBodySnapshot> GetBodySnapshot(PhysicsBodyHandle handle) const = 0;
};

class IPhysicsStepper
{
  public:
    virtual ~IPhysicsStepper() = default;

    [[nodiscard]] virtual foundation::Result<PhysicsStepResult> StepFixed(RuntimeFrameDuration fixed_delta) = 0;
    [[nodiscard]] virtual foundation::Result<PhysicsStepResult> Tick(RuntimeFrameDuration delta) = 0;
};

class IPhysicsRuntimeLifecycle
{
  public:
    virtual ~IPhysicsRuntimeLifecycle() = default;

    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
};

class IPhysicsBackend
{
  public:
    virtual ~IPhysicsBackend() = default;

    [[nodiscard]] virtual foundation::Result<void> Initialize(const PhysicsBackendOptions& options) = 0;
    [[nodiscard]] virtual foundation::Result<BackendShapeHandle> CreateShape(const CollisionShapeDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyShape(BackendShapeHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<BackendBodyHandle> CreateBody(const PhysicsBodyDesc& desc, BackendShapeHandle shape) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyBody(BackendBodyHandle handle) = 0;
    [[nodiscard]] virtual foundation::Result<void> ApplyImpulse(BackendBodyHandle handle, const Vec3& impulse) = 0;
    [[nodiscard]] virtual foundation::Result<void> SimulateFixed(RuntimeFrameDuration fixed_delta) = 0;
    [[nodiscard]] virtual foundation::Result<BackendBodySnapshot> GetBodySnapshot(BackendBodyHandle handle) const = 0;
    [[nodiscard]] virtual foundation::Result<BackendRaycastHit> RaycastBackend(const RaycastQuery& query) const = 0;
    [[nodiscard]] virtual foundation::Result<std::vector<BackendContactEvent>> ConsumeContactEvents() = 0;
};

class IPhysicsTransformSource
{
  public:
    virtual ~IPhysicsTransformSource() = default;

    [[nodiscard]] virtual foundation::Result<Transform> ReadTransform(PhysicsTransformId id) const = 0;
};

class IPhysicsTransformSink
{
  public:
    virtual ~IPhysicsTransformSink() = default;

    [[nodiscard]] virtual foundation::Result<void> WriteTransform(PhysicsTransformId id, const Transform& transform) = 0;
};

struct PhysicsServices
{
    std::shared_ptr<ICollisionShapeRegistry> shapes;
    std::shared_ptr<IPhysicsScene> scene;
    std::shared_ptr<IPhysicsStepper> stepper;
    std::shared_ptr<IPhysicsQuery> query;
    std::shared_ptr<IPhysicsEventBuffer> events;
    std::shared_ptr<IPhysicsBackend> backend;
    std::shared_ptr<IPhysicsRuntimeLifecycle> lifecycle;
};

struct PhysicsDependencies
{
    std::shared_ptr<IPhysicsBackend> backend;
    std::shared_ptr<IPhysicsTransformSource> transform_source;
    std::shared_ptr<IPhysicsTransformSink> transform_sink;
    PhysicsOptions options{};
};

[[nodiscard]] foundation::Result<PhysicsServices> CreatePhysicsServices();
[[nodiscard]] foundation::Result<PhysicsServices> CreatePhysicsServices(std::shared_ptr<IPhysicsBackend> backend);
[[nodiscard]] foundation::Result<PhysicsServices> CreatePhysicsServices(PhysicsDependencies dependencies);
} // namespace epidemic::runtime::physics
