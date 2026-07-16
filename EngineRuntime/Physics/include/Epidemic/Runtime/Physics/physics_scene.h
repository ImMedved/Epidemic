#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

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
} // namespace epidemic::runtime::physics
