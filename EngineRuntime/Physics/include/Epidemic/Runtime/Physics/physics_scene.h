#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

#include <memory>

namespace epidemic::runtime::physics
{
// File note:
// Public contracts for creating and managing physics bodies and collision shapes.
class ICollisionShapeRegistry
{
  public:
    virtual ~ICollisionShapeRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterShape(const CollisionShapeDesc& desc) = 0;
    [[nodiscard]] virtual bool HasShape(CollisionShapeId id) const = 0;
};

class IPhysicsScene
{
  public:
    virtual ~IPhysicsScene() = default;

    [[nodiscard]] virtual foundation::Result<PhysicsBodyId> CreateBody(const PhysicsBodyDesc& desc) = 0;
    [[nodiscard]] virtual foundation::Result<void> DestroyBody(PhysicsBodyId id) = 0;
    [[nodiscard]] virtual foundation::Result<void> ApplyImpulse(PhysicsBodyId id, Vec3 impulse) = 0;
    [[nodiscard]] virtual PhysicsBodyState GetBodyState(PhysicsBodyId id) const = 0;
};

class IPhysicsStepper
{
  public:
    virtual ~IPhysicsStepper() = default;

    [[nodiscard]] virtual foundation::Result<PhysicsStepResult> StepFixed(GameDuration fixed_delta) = 0;
};

class IPhysicsBackend
{
  public:
    virtual ~IPhysicsBackend() = default;

    [[nodiscard]] virtual foundation::Result<PhysicsStepResult> SimulateFixed(GameDuration fixed_delta) = 0;
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
};

[[nodiscard]] PhysicsServices CreatePhysicsServices();
} // namespace epidemic::runtime::physics
