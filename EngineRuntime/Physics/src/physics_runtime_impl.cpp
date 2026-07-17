#include "physics_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace epidemic::runtime::physics
{
namespace
{
[[nodiscard]] foundation::Result<void> PhysicsFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> PhysicsFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}
} // namespace

PhysicsRuntime::PhysicsRuntime(std::shared_ptr<IPhysicsBackend> backend) : backend_(std::move(backend))
{
}

foundation::Result<void> PhysicsRuntime::RegisterShape(const CollisionShapeDesc& desc)
{
    if (!desc.id.IsValid() || !IsValidAabb(desc.local_bounds))
    {
        return PhysicsFailure("physics.invalid_shape", "collision shape id and bounds must be valid before registration");
    }

    shapes_[desc.id] = desc;
    if (backend_ && backend_.get() != this)
    {
        const auto backend_shape = backend_->CreateShape(desc);
        if (!backend_shape)
        {
            return foundation::Result<void>::Failure(backend_shape.GetError());
        }
    }
    return foundation::Result<void>::Success();
}

bool PhysicsRuntime::HasShape(CollisionShapeId id) const
{
    return shapes_.contains(id);
}

foundation::Result<PhysicsBodyHandle> PhysicsRuntime::CreateBody(const PhysicsBodyDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_owner", "physics body owner must be valid before creation");
    }
    if (!desc.transform_node.IsValid())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_transform", "physics body must reference a valid transform projection");
    }

    const auto shape_it = shapes_.find(desc.shape);
    if (shape_it == shapes_.end())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.shape_not_found", "physics body requires a registered collision shape");
    }

    BackendBodyHandle backend_handle{next_backend_body_value_++};
    if (backend_ && backend_.get() != this)
    {
        const auto backend_shape = backend_->CreateShape(shape_it->second);
        if (!backend_shape)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(backend_shape.GetError());
        }
        const auto backend_body = backend_->CreateBody(desc, backend_shape.Value());
        if (!backend_body)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(backend_body.GetError());
        }
        backend_handle = backend_body.Value();
    }

    const PhysicsBodyHandle handle{PhysicsBodyId{next_body_value_++}, next_body_generation_++};
    BodyRecord record{};
    record.handle = handle;
    record.backend_handle = backend_handle;
    record.desc = desc;
    record.lifecycle = PhysicsBodyLifecycle::Alive;
    record.activity = ActivityForType(desc.type);
    record.dirty = PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Shape;
    record.bounds = shape_it->second.local_bounds;
    record.revision = ++revision_;
    if (transform_source_)
    {
        const auto transform = transform_source_->ReadTransform(desc.transform_node);
        if (transform)
        {
            record.world_transform = transform.Value();
        }
    }
    backend_to_body_[backend_handle] = handle.id;
    bodies_.emplace(handle.id, record);
    return foundation::Result<PhysicsBodyHandle>::Success(handle);
}

foundation::Result<void> PhysicsRuntime::DestroyBody(PhysicsBodyHandle handle)
{
    BodyRecord* body = FindBody(handle);
    if (body == nullptr)
    {
        return ValidateHandle(handle);
    }

    if (backend_ && backend_.get() != this)
    {
        const auto destroyed = backend_->DestroyBody(body->backend_handle);
        if (!destroyed)
        {
            return destroyed;
        }
    }

    backend_to_body_.erase(body->backend_handle);
    bodies_.erase(handle.id);
    ++revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::ApplyImpulse(PhysicsBodyHandle handle, Vec3 impulse)
{
    BodyRecord* body = FindBody(handle);
    if (body == nullptr)
    {
        return ValidateHandle(handle);
    }
    if (body->desc.type == PhysicsBodyType::Static || body->activity == PhysicsActivityState::Disabled)
    {
        return PhysicsFailure("physics.impulse_not_allowed", "impulse can only be applied to movable enabled bodies");
    }

    if (backend_ && backend_.get() != this)
    {
        const auto applied = backend_->ApplyImpulse(body->backend_handle, impulse);
        if (!applied)
        {
            return applied;
        }
    }

    body->last_impulse = impulse;
    body->linear_velocity = impulse;
    body->activity = PhysicsActivityState::Awake;
    body->dirty = body->dirty | PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Activity;
    body->revision = ++revision_;
    return foundation::Result<void>::Success();
}

foundation::Result<PhysicsBodySnapshot> PhysicsRuntime::GetBodySnapshot(PhysicsBodyHandle handle) const
{
    const BodyRecord* body = FindBody(handle);
    if (body == nullptr)
    {
        const auto valid = ValidateHandle(handle);
        return foundation::Result<PhysicsBodySnapshot>::Failure(valid.GetError());
    }

    if (backend_ && backend_.get() != this)
    {
        const auto snapshot = backend_->GetBodySnapshot(body->backend_handle);
        if (!snapshot)
        {
            return snapshot;
        }
    }
    return foundation::Result<PhysicsBodySnapshot>::Success(BuildSnapshot(*body));
}

foundation::Result<PhysicsStepResult> PhysicsRuntime::StepFixed(GameDuration fixed_delta)
{
    if (fixed_delta.ticks <= 0)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_step", "fixed physics step must be positive");
    }
    return CompleteFixedStep(fixed_delta, 1);
}

foundation::Result<PhysicsStepResult> PhysicsRuntime::Tick(GameDuration delta)
{
    if (delta.ticks < 0)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_step", "physics tick delta must not be negative");
    }

    accumulator_.ticks += delta.ticks;
    std::uint32_t substeps = 0;
    while (fixed_step_.ticks > 0 && accumulator_.ticks >= fixed_step_.ticks && substeps < max_substeps_)
    {
        const auto simulated = SimulateFixed(fixed_step_);
        if (!simulated)
        {
            return foundation::Result<PhysicsStepResult>::Failure(simulated.GetError());
        }
        accumulator_.ticks -= fixed_step_.ticks;
        ++substeps;
        ++fixed_step_count_;
        ++revision_;
    }

    if (fixed_step_.ticks > 0 && accumulator_.ticks >= fixed_step_.ticks)
    {
        dropped_time_.ticks += accumulator_.ticks;
        accumulator_.ticks = 0;
    }

    return foundation::Result<PhysicsStepResult>::Success(
        PhysicsStepResult{fixed_step_, fixed_step_count_, substeps, accumulator_, dropped_time_, revision_});
}

foundation::Result<void> PhysicsRuntime::Initialize(const PhysicsBackendOptions&)
{
    return foundation::Result<void>::Success();
}

foundation::Result<BackendShapeHandle> PhysicsRuntime::CreateShape(const CollisionShapeDesc& desc)
{
    if (!desc.id.IsValid() || !IsValidAabb(desc.local_bounds))
    {
        return PhysicsFailureValue<BackendShapeHandle>("physics.invalid_shape", "backend shape requires valid id and bounds");
    }
    return foundation::Result<BackendShapeHandle>::Success(BackendShapeHandle{next_backend_shape_value_++});
}

foundation::Result<BackendBodyHandle> PhysicsRuntime::CreateBody(const PhysicsBodyDesc& desc, BackendShapeHandle shape)
{
    if (!shape.IsValid() || !desc.owner.IsValid())
    {
        return PhysicsFailureValue<BackendBodyHandle>("physics.invalid_body", "backend body requires valid shape and owner");
    }
    return foundation::Result<BackendBodyHandle>::Success(BackendBodyHandle{next_backend_body_value_++});
}

foundation::Result<void> PhysicsRuntime::DestroyBody(BackendBodyHandle handle)
{
    if (!handle.IsValid())
    {
        return PhysicsFailure("physics.invalid_handle", "backend body handle must be valid");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::ApplyImpulse(BackendBodyHandle handle, const Vec3&)
{
    if (!handle.IsValid())
    {
        return PhysicsFailure("physics.invalid_handle", "backend body handle must be valid");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::SimulateFixed(GameDuration fixed_delta)
{
    if (fixed_delta.ticks <= 0)
    {
        return PhysicsFailure("physics.invalid_step", "fixed physics step must be positive");
    }
    if (backend_ && backend_.get() != this)
    {
        return backend_->SimulateFixed(fixed_delta);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<BackendBodySnapshot> PhysicsRuntime::GetBodySnapshot(BackendBodyHandle handle) const
{
    const BodyRecord* body = FindBackendBody(handle);
    if (body == nullptr)
    {
        return PhysicsFailureValue<BackendBodySnapshot>("physics.unknown_handle", "backend body handle is unknown");
    }
    return foundation::Result<BackendBodySnapshot>::Success(BuildSnapshot(*body));
}

foundation::Result<RaycastHit> PhysicsRuntime::Raycast(const RaycastQuery& query) const
{
    if (query.max_distance < 0.0f)
    {
        return PhysicsFailureValue<RaycastHit>("physics.invalid_query", "raycast max distance must not be negative");
    }
    if (query.max_distance > 0.0f && query.direction.x == 0.0f && query.direction.y == 0.0f && query.direction.z == 0.0f)
    {
        return PhysicsFailureValue<RaycastHit>("physics.invalid_query", "raycast direction must be non-zero");
    }

    RaycastHit best{};
    best.distance = std::numeric_limits<float>::max();
    for (const auto& [body_id, body] : bodies_)
    {
        (void)body_id;
        float distance = 0.0f;
        if (RayIntersectsAabb(query, body.bounds, distance) && distance <= query.max_distance && distance < best.distance)
        {
            best = RaycastHit{true, body.handle, query.origin, Vec3{0.0f, 1.0f, 0.0f}, distance};
        }
    }
    if (!best.hit)
    {
        best.distance = 0.0f;
    }
    return foundation::Result<RaycastHit>::Success(best);
}

foundation::Result<OverlapResult> PhysicsRuntime::Overlap(const OverlapQuery& query) const
{
    OverlapResult result{};
    for (const auto& [body_id, body] : bodies_)
    {
        (void)body_id;
        if (IntersectsAabb(query.bounds, body.bounds))
        {
            result.bodies.push_back(body.handle);
        }
    }
    std::sort(result.bodies.begin(), result.bodies.end(), [](const auto& left, const auto& right) {
        return left.id.value < right.id.value;
    });
    return foundation::Result<OverlapResult>::Success(std::move(result));
}

std::span<const ContactEvent> PhysicsRuntime::Contacts() const
{
    return contacts_;
}

void PhysicsRuntime::Clear()
{
    contacts_.clear();
}

void PhysicsRuntime::QueueContact(ContactEvent event)
{
    contacts_.push_back(event);
}

void PhysicsRuntime::SetTransformSource(IPhysicsTransformSource* source) noexcept
{
    transform_source_ = source;
}

void PhysicsRuntime::SetTransformSink(IPhysicsTransformSink* sink) noexcept
{
    transform_sink_ = sink;
}

void PhysicsRuntime::SetFixedStep(GameDuration step) noexcept
{
    if (step.ticks > 0)
    {
        fixed_step_ = step;
    }
}

void PhysicsRuntime::SetMaxSubsteps(std::uint32_t substeps) noexcept
{
    max_substeps_ = substeps == 0 ? 1 : substeps;
}

PhysicsActivityState PhysicsRuntime::ActivityForType(PhysicsBodyType type)
{
    switch (type)
    {
    case PhysicsBodyType::Static:
        return PhysicsActivityState::Static;
    case PhysicsBodyType::Dynamic:
        return PhysicsActivityState::Awake;
    case PhysicsBodyType::Kinematic:
        return PhysicsActivityState::Awake;
    }
    return PhysicsActivityState::Disabled;
}

bool PhysicsRuntime::PointInsideAabb(const Vec3& point, const Aabb& bounds) noexcept
{
    return point.x >= bounds.min.x && point.x <= bounds.max.x &&
           point.y >= bounds.min.y && point.y <= bounds.max.y &&
           point.z >= bounds.min.z && point.z <= bounds.max.z;
}

bool PhysicsRuntime::IntersectsAabb(const Aabb& left, const Aabb& right) noexcept
{
    return left.min.x <= right.max.x && left.max.x >= right.min.x &&
           left.min.y <= right.max.y && left.max.y >= right.min.y &&
           left.min.z <= right.max.z && left.max.z >= right.min.z;
}

bool PhysicsRuntime::RayIntersectsAabb(const RaycastQuery& query, const Aabb& bounds, float& distance) noexcept
{
    if (PointInsideAabb(query.origin, bounds))
    {
        distance = 0.0f;
        return true;
    }

    float t_min = 0.0f;
    float t_max = query.max_distance;
    const auto test_axis = [&](float origin, float direction, float min_value, float max_value) {
        if (direction == 0.0f)
        {
            return origin >= min_value && origin <= max_value;
        }
        const float inverse = 1.0f / direction;
        float near_t = (min_value - origin) * inverse;
        float far_t = (max_value - origin) * inverse;
        if (near_t > far_t)
        {
            std::swap(near_t, far_t);
        }
        t_min = std::max(t_min, near_t);
        t_max = std::min(t_max, far_t);
        return t_min <= t_max;
    };

    if (!test_axis(query.origin.x, query.direction.x, bounds.min.x, bounds.max.x) ||
        !test_axis(query.origin.y, query.direction.y, bounds.min.y, bounds.max.y) ||
        !test_axis(query.origin.z, query.direction.z, bounds.min.z, bounds.max.z))
    {
        return false;
    }
    distance = t_min;
    return true;
}

PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBody(PhysicsBodyHandle handle)
{
    const auto iterator = bodies_.find(handle.id);
    if (iterator == bodies_.end() || iterator->second.handle.generation != handle.generation)
    {
        return nullptr;
    }
    return &iterator->second;
}

const PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBody(PhysicsBodyHandle handle) const
{
    const auto iterator = bodies_.find(handle.id);
    if (iterator == bodies_.end() || iterator->second.handle.generation != handle.generation)
    {
        return nullptr;
    }
    return &iterator->second;
}

PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBackendBody(BackendBodyHandle handle)
{
    const auto index = backend_to_body_.find(handle);
    if (index == backend_to_body_.end())
    {
        return nullptr;
    }
    auto body = bodies_.find(index->second);
    return body == bodies_.end() ? nullptr : &body->second;
}

const PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBackendBody(BackendBodyHandle handle) const
{
    const auto index = backend_to_body_.find(handle);
    if (index == backend_to_body_.end())
    {
        return nullptr;
    }
    const auto body = bodies_.find(index->second);
    return body == bodies_.end() ? nullptr : &body->second;
}

PhysicsBodySnapshot PhysicsRuntime::BuildSnapshot(const BodyRecord& body) const
{
    return PhysicsBodySnapshot{
        body.handle,
        body.desc.owner,
        body.desc.transform_node,
        body.desc.type,
        body.lifecycle,
        body.activity,
        body.dirty,
        body.world_transform,
        body.linear_velocity,
        body.angular_velocity,
        body.desc.mass,
        body.revision};
}

foundation::Result<void> PhysicsRuntime::ValidateHandle(PhysicsBodyHandle handle) const
{
    if (!handle.IsValid())
    {
        return PhysicsFailure("physics.invalid_handle", "physics body handle must be valid");
    }
    if (!bodies_.contains(handle.id))
    {
        return PhysicsFailure("physics.unknown_handle", "physics body handle is unknown");
    }
    return PhysicsFailure("physics.stale_handle", "physics body handle generation is stale");
}

foundation::Result<PhysicsStepResult> PhysicsRuntime::CompleteFixedStep(GameDuration fixed_delta, std::uint32_t substeps)
{
    const auto simulated = SimulateFixed(fixed_delta);
    if (!simulated)
    {
        return foundation::Result<PhysicsStepResult>::Failure(simulated.GetError());
    }
    ++fixed_step_count_;
    ++revision_;
    for (auto& [id, body] : bodies_)
    {
        (void)id;
        if (body.desc.type == PhysicsBodyType::Dynamic && transform_sink_)
        {
            (void)transform_sink_->WriteTransform(body.desc.transform_node, body.world_transform);
        }
    }
    return foundation::Result<PhysicsStepResult>::Success(
        PhysicsStepResult{fixed_delta, fixed_step_count_, substeps, accumulator_, dropped_time_, revision_});
}
} // namespace epidemic::runtime::physics
