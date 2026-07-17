#pragma once

#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"

#include <unordered_map>
#include <vector>

namespace epidemic::runtime::physics
{
// File note:
// In-memory Physics foundation used for deterministic tests and early integration.
// It stores collision shapes and body proxies, provides fake-but-stable queries and publishes contact events.
class PhysicsRuntime final : public ICollisionShapeRegistry,
                             public IPhysicsScene,
                             public IPhysicsStepper,
                             public IPhysicsQuery,
                             public IPhysicsEventBuffer,
                             public IPhysicsBackend
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterShape(const CollisionShapeDesc& desc) override;
    [[nodiscard]] bool HasShape(CollisionShapeId id) const override;

    [[nodiscard]] foundation::Result<PhysicsBodyId> CreateBody(const PhysicsBodyDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyBody(PhysicsBodyId id) override;
    [[nodiscard]] foundation::Result<void> ApplyImpulse(PhysicsBodyId id, Vec3 impulse) override;
    [[nodiscard]] PhysicsBodyState GetBodyState(PhysicsBodyId id) const override;
    [[nodiscard]] foundation::Result<PhysicsStepResult> StepFixed(GameDuration fixed_delta) override;
    [[nodiscard]] foundation::Result<PhysicsStepResult> SimulateFixed(GameDuration fixed_delta) override;

    [[nodiscard]] foundation::Result<RaycastHit> Raycast(const RaycastQuery& query) const override;
    [[nodiscard]] foundation::Result<OverlapResult> Overlap(const OverlapQuery& query) const override;

    [[nodiscard]] std::span<const ContactEvent> Contacts() const override;
    void Clear() override;

    void QueueContact(ContactEvent event);

  private:
    struct BodyRecord
    {
        PhysicsBodyDesc desc{};
        PhysicsBodyState state = PhysicsBodyState::PendingCreate;
        PhysicsSyncState sync_state = PhysicsSyncState::Clean;
        Vec3 last_impulse{};
        Aabb bounds{};
    };

    [[nodiscard]] static PhysicsBodyState ToBodyState(PhysicsBodyType type);
    [[nodiscard]] static bool PointInsideAabb(const Vec3& point, const Aabb& bounds) noexcept;
    [[nodiscard]] static bool IntersectsAabb(const Aabb& left, const Aabb& right) noexcept;
    [[nodiscard]] BodyRecord* FindBody(PhysicsBodyId id);
    [[nodiscard]] const BodyRecord* FindBody(PhysicsBodyId id) const;

    std::unordered_map<CollisionShapeId, CollisionShapeDesc> shapes_;
    std::unordered_map<PhysicsBodyId, BodyRecord> bodies_;
    std::vector<ContactEvent> contacts_;
    std::uint64_t next_body_value_ = 1;
    std::uint64_t fixed_step_count_ = 0;
    std::uint64_t revision_ = 0;
};
} // namespace epidemic::runtime::physics
