#pragma once

#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"

#include <chrono>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::physics
{
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
    explicit PhysicsRuntime(std::shared_ptr<IPhysicsBackend> backend = {});
    explicit PhysicsRuntime(PhysicsDependencies dependencies);
    ~PhysicsRuntime() override;
    [[nodiscard]] foundation::Result<void> Shutdown();

    [[nodiscard]] foundation::Result<void> RegisterShape(const CollisionShapeDesc& desc) override;
    [[nodiscard]] foundation::Result<void> UnregisterShape(CollisionShapeId id) override;
    [[nodiscard]] bool HasShape(CollisionShapeId id) const override;

    [[nodiscard]] foundation::Result<PhysicsBodyHandle> CreateBody(const PhysicsBodyDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyBody(PhysicsBodyHandle handle) override;
    [[nodiscard]] foundation::Result<void> ApplyImpulse(PhysicsBodyHandle handle, Vec3 impulse) override;
    [[nodiscard]] foundation::Result<PhysicsBodySnapshot> GetBodySnapshot(PhysicsBodyHandle handle) const override;
    [[nodiscard]] foundation::Result<PhysicsStepResult> StepFixed(RuntimeFrameDuration fixed_delta) override;
    [[nodiscard]] foundation::Result<PhysicsStepResult> Tick(RuntimeFrameDuration delta) override;

    [[nodiscard]] foundation::Result<void> Initialize(const PhysicsBackendOptions& options) override;
    [[nodiscard]] foundation::Result<BackendShapeHandle> CreateShape(const CollisionShapeDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyShape(BackendShapeHandle handle) override;
    [[nodiscard]] foundation::Result<BackendBodyHandle> CreateBody(const PhysicsBodyDesc& desc, BackendShapeHandle shape) override;
    [[nodiscard]] foundation::Result<void> DestroyBody(BackendBodyHandle handle) override;
    [[nodiscard]] foundation::Result<void> ApplyImpulse(BackendBodyHandle handle, const Vec3& impulse) override;
    [[nodiscard]] foundation::Result<void> SimulateFixed(RuntimeFrameDuration fixed_delta) override;
    [[nodiscard]] foundation::Result<BackendBodySnapshot> GetBodySnapshot(BackendBodyHandle handle) const override;

    [[nodiscard]] foundation::Result<RaycastHit> Raycast(const RaycastQuery& query) const override;
    [[nodiscard]] foundation::Result<OverlapResult> Overlap(const OverlapQuery& query) const override;

    [[nodiscard]] std::span<const ContactEvent> Contacts() const override;
    void Clear() override;

    void QueueContact(ContactEvent event);
    void SetTransformSource(IPhysicsTransformSource* source) noexcept;
    void SetTransformSink(IPhysicsTransformSink* sink) noexcept;
    void SetFixedStep(RuntimeFrameDuration step) noexcept;
    void SetMaxSubsteps(std::uint32_t substeps) noexcept;

  private:
    struct BodyRecord
    {
        PhysicsBodyHandle handle{};
        BackendBodyHandle backend_handle{};
        PhysicsBodyDesc desc{};
        PhysicsBodyLifecycle lifecycle = PhysicsBodyLifecycle::Alive;
        PhysicsActivityState activity = PhysicsActivityState::Disabled;
        PhysicsDirtyFlags dirty = PhysicsDirtyFlags::None;
        Vec3 last_impulse{};
        Transform world_transform{};
        Vec3 linear_velocity{};
        Vec3 angular_velocity{};
        Aabb bounds{};
        std::uint64_t revision = 0;
    };

    struct ShapeRecord
    {
        CollisionShapeDesc desc{};
        BackendShapeHandle backend_handle{};
    };

    [[nodiscard]] static PhysicsActivityState ActivityForType(PhysicsBodyType type);
    [[nodiscard]] static bool PointInsideAabb(const Vec3& point, const Aabb& bounds) noexcept;
    [[nodiscard]] static bool IntersectsAabb(const Aabb& left, const Aabb& right) noexcept;
    [[nodiscard]] static bool RayIntersectsAabb(const RaycastQuery& query, const Aabb& bounds, float& distance) noexcept;
    [[nodiscard]] static bool IsFinite(Vec3 value) noexcept;
    [[nodiscard]] static float Length(Vec3 value) noexcept;
    [[nodiscard]] static Vec3 Normalize(Vec3 value) noexcept;
    [[nodiscard]] static RaycastQuery NormalizeRaycastQuery(const RaycastQuery& query) noexcept;
    [[nodiscard]] BodyRecord* FindBody(PhysicsBodyHandle handle);
    [[nodiscard]] const BodyRecord* FindBody(PhysicsBodyHandle handle) const;
    [[nodiscard]] BodyRecord* FindBackendBody(BackendBodyHandle handle);
    [[nodiscard]] const BodyRecord* FindBackendBody(BackendBodyHandle handle) const;
    [[nodiscard]] PhysicsBodySnapshot BuildSnapshot(const BodyRecord& body) const;
    [[nodiscard]] PhysicsBodySnapshot BuildSnapshotFromBackend(const BodyRecord& body, const BackendBodySnapshot& backend_snapshot) const;
    [[nodiscard]] foundation::Result<void> ValidateHandle(PhysicsBodyHandle handle) const;
    [[nodiscard]] foundation::Result<PhysicsStepResult> CompleteFixedStep(RuntimeFrameDuration fixed_delta, std::uint32_t substeps);
    [[nodiscard]] foundation::Result<void> SynchronizeBackendBody(BodyRecord& body);

    std::unordered_map<CollisionShapeId, ShapeRecord> shapes_;
    std::unordered_map<PhysicsBodyId, BodyRecord> bodies_;
    std::unordered_map<BackendBodyHandle, PhysicsBodyId> backend_to_body_;
    std::vector<ContactEvent> contacts_;
    std::shared_ptr<IPhysicsBackend> backend_;
    std::shared_ptr<IPhysicsTransformSource> owned_transform_source_;
    std::shared_ptr<IPhysicsTransformSink> owned_transform_sink_;
    IPhysicsTransformSource* transform_source_ = nullptr;
    IPhysicsTransformSink* transform_sink_ = nullptr;
    std::uint64_t next_body_value_ = 1;
    std::uint64_t next_backend_shape_value_ = 1;
    std::uint64_t next_backend_body_value_ = 1;
    std::uint32_t next_body_generation_ = 1;
    std::uint64_t fixed_step_count_ = 0;
    std::uint64_t revision_ = 0;
    RuntimeFrameDuration fixed_step_{std::chrono::microseconds{16667}};
    RuntimeFrameDuration accumulator_{};
    RuntimeFrameDuration dropped_time_{};
    std::uint32_t max_substeps_ = 4;
};
} // namespace epidemic::runtime::physics
