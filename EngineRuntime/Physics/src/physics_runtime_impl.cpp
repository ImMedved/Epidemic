#include "physics_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

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

PhysicsRuntime::PhysicsRuntime(PhysicsDependencies dependencies)
    : backend_(std::move(dependencies.backend)),
      owned_transform_source_(std::move(dependencies.transform_source)),
      owned_transform_sink_(std::move(dependencies.transform_sink)),
      transform_source_(owned_transform_source_.get()),
      transform_sink_(owned_transform_sink_.get())
{
    SetFixedStep(dependencies.options.fixed_step);
    SetMaxSubsteps(dependencies.options.max_substeps);
}

PhysicsRuntime::~PhysicsRuntime()
{
    (void)Shutdown();
}

foundation::Result<void> PhysicsRuntime::Shutdown()
{
    if (backend_ && backend_.get() != this)
    {
        std::optional<foundation::Error> first_error;
        for (auto iterator = bodies_.begin(); iterator != bodies_.end();)
        {
            const auto destroyed = backend_->DestroyBody(iterator->second.backend_handle);
            if (!destroyed && !first_error)
            {
                first_error = destroyed.GetError();
            }
            if (!destroyed)
            {
                ++iterator;
                continue;
            }
            backend_to_body_.erase(iterator->second.backend_handle);
            iterator = bodies_.erase(iterator);
        }
        if (!bodies_.empty())
        {
            return foundation::Result<void>::Failure(*first_error);
        }
        for (auto iterator = shapes_.begin(); iterator != shapes_.end();)
        {
            const auto destroyed = backend_->DestroyShape(iterator->second.backend_handle);
            if (!destroyed && !first_error)
            {
                first_error = destroyed.GetError();
            }
            if (!destroyed)
            {
                ++iterator;
                continue;
            }
            iterator = shapes_.erase(iterator);
        }
        if (first_error)
        {
            return foundation::Result<void>::Failure(*first_error);
        }
    }
    if (!backend_ || backend_.get() == this)
    {
        bodies_.clear();
        shapes_.clear();
        backend_to_body_.clear();
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::RegisterShape(const CollisionShapeDesc& desc)
{
    if (!desc.id.IsValid() || !IsValidAabb(desc.local_bounds))
    {
        return PhysicsFailure("physics.invalid_shape", "collision shape id and bounds must be valid before registration");
    }
    if (shapes_.contains(desc.id))
    {
        return PhysicsFailure("physics.duplicate_shape", "collision shape id is already registered");
    }

    BackendShapeHandle backend_shape{};
    if (backend_ && backend_.get() != this)
    {
        const auto created = backend_->CreateShape(desc);
        if (!created)
        {
            return foundation::Result<void>::Failure(created.GetError());
        }
        if (!created.Value().IsValid())
        {
            return PhysicsFailure("physics.invalid_backend_handle", "physics backend returned an invalid shape handle");
        }
        backend_shape = created.Value();
    }
    else
    {
        const auto allocated = AllocateMonotonicId(
            next_backend_shape_value_,
            "physics.backend_shape_id_overflow",
            "backend shape id allocator is exhausted");
        if (!allocated)
        {
            return foundation::Result<void>::Failure(allocated.GetError());
        }
        backend_shape = BackendShapeHandle{allocated.Value()};
    }
    const auto [shape_iterator, inserted] = shapes_.emplace(desc.id, ShapeRecord{desc, backend_shape});
    if (!inserted)
    {
        if (backend_ && backend_.get() != this)
        {
            (void)backend_->DestroyShape(backend_shape);
        }
        return PhysicsFailure("physics.duplicate_shape", "collision shape id is already registered");
    }
    (void)shape_iterator;
    return foundation::Result<void>::Success();
}

bool PhysicsRuntime::HasShape(CollisionShapeId id) const
{
    return shapes_.contains(id);
}

foundation::Result<void> PhysicsRuntime::UnregisterShape(CollisionShapeId id)
{
    const auto iterator = shapes_.find(id);
    if (!id.IsValid())
    {
        return PhysicsFailure("physics.invalid_shape", "collision shape id must be valid before unregister");
    }
    if (iterator == shapes_.end())
    {
        return PhysicsFailure("physics.shape_not_found", "collision shape was not registered");
    }
    for (const auto& [body_id, body] : bodies_)
    {
        (void)body_id;
        if (body.desc.shape == id)
        {
            return PhysicsFailure("physics.shape_in_use", "collision shape cannot be unregistered while bodies use it");
        }
    }
    if (backend_ && backend_.get() != this)
    {
        const auto destroyed = backend_->DestroyShape(iterator->second.backend_handle);
        if (!destroyed)
        {
            return destroyed;
        }
    }
    shapes_.erase(iterator);
    return foundation::Result<void>::Success();
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
    if (!std::isfinite(desc.mass) || desc.mass < 0.0f || (desc.type == PhysicsBodyType::Dynamic && desc.mass <= 0.0f))
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_mass", "physics body mass must be finite and positive for dynamic bodies");
    }

    const auto shape_it = shapes_.find(desc.shape);
    if (shape_it == shapes_.end())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.shape_not_found", "physics body requires a registered collision shape");
    }

    Transform initial_transform = desc.initial_transform;
    if (transform_source_)
    {
        const auto transform = transform_source_->ReadTransform(desc.transform_node);
        if (!transform)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(transform.GetError());
        }
        initial_transform = transform.Value();
    }

    PhysicsBodyDesc backend_desc = desc;
    backend_desc.initial_transform = initial_transform;
    const bool uses_external_backend = backend_ && backend_.get() != this;
    if (!CanAllocateMonotonicId(next_body_value_) || !CanAllocateMonotonicId(next_body_generation_) ||
        (!uses_external_backend && !CanAllocateMonotonicId(next_backend_body_value_)) ||
        revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.body_id_overflow", "physics body id allocator is exhausted");
    }

    BackendBodyHandle backend_handle{};
    if (uses_external_backend)
    {
        const auto backend_body = backend_->CreateBody(backend_desc, shape_it->second.backend_handle);
        if (!backend_body)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(backend_body.GetError());
        }
        if (!backend_body.Value().IsValid())
        {
            return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_backend_handle", "physics backend returned an invalid body handle");
        }
        backend_handle = backend_body.Value();
    }
    else
    {
        const auto backend_value = AllocateMonotonicId(
            next_backend_body_value_,
            "physics.backend_body_id_overflow",
            "backend body id allocator is exhausted");
        if (!backend_value)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(backend_value.GetError());
        }
        backend_handle = BackendBodyHandle{backend_value.Value()};
    }

    const auto body_value = AllocateMonotonicId(next_body_value_, "physics.body_id_overflow", "physics body id allocator is exhausted");
    const auto body_generation = AllocateMonotonicId(next_body_generation_, "physics.body_id_overflow", "physics body generation allocator is exhausted");
    if (!body_value || !body_generation)
    {
        if (uses_external_backend)
        {
            (void)backend_->DestroyBody(backend_handle);
        }
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.body_id_overflow", "physics body id allocator is exhausted");
    }
    const PhysicsBodyHandle handle{PhysicsBodyId{body_value.Value()}, body_generation.Value()};
    BodyRecord record{};
    record.handle = handle;
    record.backend_handle = backend_handle;
    record.desc = desc;
    record.activity = ActivityForType(desc.type);
    record.dirty = PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Shape;
    record.world_transform = initial_transform;
    record.bounds = TransformAabb(record.world_transform, shape_it->second.desc.local_bounds);
    record.revision = ++revision_;
    if (backend_to_body_.contains(backend_handle))
    {
        if (uses_external_backend)
        {
            (void)backend_->DestroyBody(backend_handle);
        }
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_backend_body_handle", "physics backend returned a duplicate body handle");
    }
    const auto [body_iterator, body_inserted] = bodies_.emplace(handle.id, record);
    if (!body_inserted)
    {
        if (uses_external_backend)
        {
            (void)backend_->DestroyBody(backend_handle);
        }
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_body_id", "allocated physics body id already exists");
    }
    (void)body_iterator;
    const auto [backend_iterator, backend_inserted] = backend_to_body_.emplace(backend_handle, handle.id);
    if (!backend_inserted)
    {
        bodies_.erase(handle.id);
        if (uses_external_backend)
        {
            (void)backend_->DestroyBody(backend_handle);
        }
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_backend_body_handle", "physics backend returned a duplicate body handle");
    }
    (void)backend_iterator;
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
    if (!IsFinite(impulse))
    {
        return PhysicsFailure("physics.invalid_impulse", "physics impulse must contain finite values");
    }
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
            return foundation::Result<PhysicsBodySnapshot>::Failure(snapshot.GetError());
        }
        const PhysicsBodySnapshot merged = BuildSnapshotFromBackend(*body, snapshot.Value());
        if (!IsValidTransform(merged.world_transform) || !IsFinite(merged.linear_velocity) || !IsFinite(merged.angular_velocity) ||
            !std::isfinite(merged.mass))
        {
            return PhysicsFailureValue<PhysicsBodySnapshot>("physics.invalid_backend_snapshot", "physics backend returned non-finite body snapshot data");
        }
        return foundation::Result<PhysicsBodySnapshot>::Success(merged);
    }
    return foundation::Result<PhysicsBodySnapshot>::Success(BuildSnapshot(*body));
}

foundation::Result<PhysicsStepResult> PhysicsRuntime::StepFixed(RuntimeFrameDuration fixed_delta)
{
    if (fixed_delta.value.count() <= 0)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_step", "fixed physics step must be positive");
    }
    return CompleteFixedStep(fixed_delta, 1);
}

foundation::Result<PhysicsStepResult> PhysicsRuntime::Tick(RuntimeFrameDuration delta)
{
    if (delta.value.count() < 0)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_step", "physics tick delta must not be negative");
    }

    accumulator_.value += delta.value;
    std::uint32_t substeps = 0;
    while (fixed_step_.value.count() > 0 && accumulator_.value >= fixed_step_.value && substeps < max_substeps_)
    {
        const auto stepped = CompleteFixedStep(fixed_step_, 1);
        if (!stepped)
        {
            return foundation::Result<PhysicsStepResult>::Failure(stepped.GetError());
        }
        accumulator_.value -= fixed_step_.value;
        ++substeps;
    }

    if (fixed_step_.value.count() > 0 && accumulator_.value >= fixed_step_.value)
    {
        dropped_time_.value += accumulator_.value;
        accumulator_.value = std::chrono::microseconds{0};
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
    const auto allocated = AllocateMonotonicId(
        next_backend_shape_value_,
        "physics.backend_shape_id_overflow",
        "backend shape id allocator is exhausted");
    if (!allocated)
    {
        return foundation::Result<BackendShapeHandle>::Failure(allocated.GetError());
    }
    return foundation::Result<BackendShapeHandle>::Success(BackendShapeHandle{allocated.Value()});
}

foundation::Result<void> PhysicsRuntime::DestroyShape(BackendShapeHandle handle)
{
    if (!handle.IsValid())
    {
        return PhysicsFailure("physics.invalid_handle", "backend shape handle must be valid");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<BackendBodyHandle> PhysicsRuntime::CreateBody(const PhysicsBodyDesc& desc, BackendShapeHandle shape)
{
    if (!shape.IsValid() || !desc.owner.IsValid())
    {
        return PhysicsFailureValue<BackendBodyHandle>("physics.invalid_body", "backend body requires valid shape and owner");
    }
    const auto allocated = AllocateMonotonicId(
        next_backend_body_value_,
        "physics.backend_body_id_overflow",
        "backend body id allocator is exhausted");
    if (!allocated)
    {
        return foundation::Result<BackendBodyHandle>::Failure(allocated.GetError());
    }
    return foundation::Result<BackendBodyHandle>::Success(BackendBodyHandle{allocated.Value()});
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

foundation::Result<void> PhysicsRuntime::SimulateFixed(RuntimeFrameDuration fixed_delta)
{
    if (fixed_delta.value.count() <= 0)
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
    return foundation::Result<BackendBodySnapshot>::Success(BackendBodySnapshot{
        body->world_transform,
        body->linear_velocity,
        body->angular_velocity,
        body->activity});
}

foundation::Result<std::vector<BackendContactEvent>> PhysicsRuntime::ConsumeContactEvents()
{
    std::vector<BackendContactEvent> consumed;
    consumed.reserve(contacts_.size());
    for (const ContactEvent& contact : contacts_)
    {
        const BodyRecord* left = FindBody(contact.a);
        const BodyRecord* right = FindBody(contact.b);
        if (left != nullptr && right != nullptr)
        {
            consumed.push_back(BackendContactEvent{left->backend_handle, right->backend_handle, contact.point, contact.normal, contact.impulse, contact.state});
        }
    }
    contacts_.clear();
    return foundation::Result<std::vector<BackendContactEvent>>::Success(std::move(consumed));
}

foundation::Result<RaycastHit> PhysicsRuntime::Raycast(const RaycastQuery& query) const
{
    if (query.max_distance < 0.0f || !IsFinite(query.origin) || !IsFinite(query.direction) || !std::isfinite(query.max_distance))
    {
        return PhysicsFailureValue<RaycastHit>("physics.invalid_query", "raycast query must be finite and max distance must not be negative");
    }
    if (Length(query.direction) <= std::numeric_limits<float>::epsilon())
    {
        return PhysicsFailureValue<RaycastHit>("physics.invalid_query", "raycast direction must be non-zero");
    }
    const RaycastQuery normalized = NormalizeRaycastQuery(query);

    if (backend_ && backend_.get() != this)
    {
        const auto hit = backend_->RaycastBackend(normalized);
        if (!hit)
        {
            return foundation::Result<RaycastHit>::Failure(hit.GetError());
        }
        if ((hit.Value().hit && (!IsFinite(hit.Value().point) || !IsFinite(hit.Value().normal))) ||
            !std::isfinite(hit.Value().distance) || hit.Value().distance < 0.0f || hit.Value().distance > normalized.max_distance)
        {
            return PhysicsFailureValue<RaycastHit>("physics.invalid_backend_hit", "physics backend returned an invalid raycast hit");
        }
        RaycastHit mapped{};
        mapped.hit = hit.Value().hit;
        mapped.point = hit.Value().point;
        mapped.normal = hit.Value().normal;
        mapped.distance = hit.Value().distance;
        if (hit.Value().hit)
        {
            const auto body = MapBackendBody(hit.Value().body);
            if (!body)
            {
                return foundation::Result<RaycastHit>::Failure(body.GetError());
            }
            mapped.body = body.Value();
        }
        return foundation::Result<RaycastHit>::Success(mapped);
    }

    RaycastHit best{};
    best.distance = std::numeric_limits<float>::max();
    for (const auto& [body_id, body] : bodies_)
    {
        (void)body_id;
        float distance = 0.0f;
        if (RayIntersectsAabb(normalized, body.bounds, distance) && distance <= normalized.max_distance && distance < best.distance)
        {
            best = RaycastHit{
                true,
                body.handle,
                Vec3{
                    normalized.origin.x + normalized.direction.x * distance,
                    normalized.origin.y + normalized.direction.y * distance,
                    normalized.origin.z + normalized.direction.z * distance},
                Vec3{0.0f, 1.0f, 0.0f},
                distance};
        }
    }
    if (!best.hit)
    {
        best.distance = 0.0f;
    }
    return foundation::Result<RaycastHit>::Success(best);
}

foundation::Result<BackendRaycastHit> PhysicsRuntime::RaycastBackend(const RaycastQuery& query) const
{
    if (query.max_distance < 0.0f || !IsFinite(query.origin) || !IsFinite(query.direction) || !std::isfinite(query.max_distance))
    {
        return PhysicsFailureValue<BackendRaycastHit>("physics.invalid_query", "backend raycast query must be finite and max distance must not be negative");
    }
    if (Length(query.direction) <= std::numeric_limits<float>::epsilon())
    {
        return PhysicsFailureValue<BackendRaycastHit>("physics.invalid_query", "backend raycast direction must be non-zero");
    }
    const RaycastQuery normalized = NormalizeRaycastQuery(query);

    BackendRaycastHit best{};
    best.distance = std::numeric_limits<float>::max();
    for (const auto& [body_id, body] : bodies_)
    {
        (void)body_id;
        float distance = 0.0f;
        if (RayIntersectsAabb(normalized, body.bounds, distance) && distance <= normalized.max_distance && distance < best.distance)
        {
            best = BackendRaycastHit{
                true,
                body.backend_handle,
                Vec3{
                    normalized.origin.x + normalized.direction.x * distance,
                    normalized.origin.y + normalized.direction.y * distance,
                    normalized.origin.z + normalized.direction.z * distance},
                Vec3{0.0f, 1.0f, 0.0f},
                distance};
        }
    }
    if (!best.hit)
    {
        best.distance = 0.0f;
    }
    return foundation::Result<BackendRaycastHit>::Success(best);
}

foundation::Result<OverlapResult> PhysicsRuntime::Overlap(const OverlapQuery& query) const
{
    if (!IsValidAabb(query.bounds))
    {
        return PhysicsFailureValue<OverlapResult>("physics.invalid_query", "overlap query bounds must be valid");
    }
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

void PhysicsRuntime::SetFixedStep(RuntimeFrameDuration step) noexcept
{
    if (step.value.count() > 0)
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

bool PhysicsRuntime::IsFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool PhysicsRuntime::IsValidActivity(PhysicsActivityState value) noexcept
{
    switch (value)
    {
    case PhysicsActivityState::Static:
    case PhysicsActivityState::Awake:
    case PhysicsActivityState::Sleeping:
    case PhysicsActivityState::Disabled:
        return true;
    }
    return false;
}

bool PhysicsRuntime::IsValidEventState(PhysicsEventState value) noexcept
{
    switch (value)
    {
    case PhysicsEventState::Begin:
    case PhysicsEventState::Persist:
    case PhysicsEventState::End:
        return true;
    }
    return false;
}

bool PhysicsRuntime::IsValidBackendSnapshot(const BackendBodySnapshot& snapshot) noexcept
{
    return IsValidTransform(snapshot.world_transform) &&
           IsFinite(snapshot.linear_velocity) &&
           IsFinite(snapshot.angular_velocity) &&
           IsValidActivity(snapshot.activity);
}

bool PhysicsRuntime::IsValidBackendContactPayload(const BackendContactEvent& contact) noexcept
{
    return IsFinite(contact.point) &&
           IsFinite(contact.normal) &&
           std::isfinite(contact.impulse) &&
           contact.impulse >= 0.0f &&
           IsValidEventState(contact.state);
}

float PhysicsRuntime::Length(Vec3 value) noexcept
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 PhysicsRuntime::Normalize(Vec3 value) noexcept
{
    const float length = Length(value);
    if (length <= std::numeric_limits<float>::epsilon())
    {
        return {};
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

RaycastQuery PhysicsRuntime::NormalizeRaycastQuery(const RaycastQuery& query) noexcept
{
    RaycastQuery normalized = query;
    normalized.direction = Normalize(query.direction);
    return normalized;
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

foundation::Result<PhysicsBodyHandle> PhysicsRuntime::MapBackendBody(BackendBodyHandle handle) const
{
    if (!handle.IsValid())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_backend_handle", "backend body handle must be valid");
    }
    const BodyRecord* body = FindBackendBody(handle);
    if (body == nullptr)
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.unknown_backend_handle", "backend body handle is unknown");
    }
    return foundation::Result<PhysicsBodyHandle>::Success(body->handle);
}

PhysicsBodySnapshot PhysicsRuntime::BuildSnapshot(const BodyRecord& body) const
{
    return PhysicsBodySnapshot{
        body.handle,
        body.desc.owner,
        body.desc.transform_node,
        body.desc.type,
        body.activity,
        body.dirty,
        body.world_transform,
        body.linear_velocity,
        body.angular_velocity,
        body.desc.mass,
        body.revision};
}

PhysicsBodySnapshot PhysicsRuntime::BuildSnapshotFromBackend(const BodyRecord& body, const BackendBodySnapshot& backend_snapshot) const
{
    return PhysicsBodySnapshot{
        body.handle,
        body.desc.owner,
        body.desc.transform_node,
        body.desc.type,
        backend_snapshot.activity,
        body.dirty,
        backend_snapshot.world_transform,
        backend_snapshot.linear_velocity,
        backend_snapshot.angular_velocity,
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

foundation::Result<PhysicsStepResult> PhysicsRuntime::CompleteFixedStep(RuntimeFrameDuration fixed_delta, std::uint32_t substeps)
{
    if (!backend_step_pending_sync_)
    {
        const auto simulated = SimulateFixed(fixed_delta);
        if (!simulated)
        {
            return foundation::Result<PhysicsStepResult>::Failure(simulated.GetError());
        }
        backend_step_pending_sync_ = backend_ && backend_.get() != this;
    }
    std::vector<std::pair<PhysicsBodyId, BackendBodySnapshot>> backend_snapshots;
    if (backend_ && backend_.get() != this)
    {
        backend_snapshots.reserve(bodies_.size());
        for (const auto& [id, body] : bodies_)
        {
            const auto snapshot = backend_->GetBodySnapshot(body.backend_handle);
            if (!snapshot)
            {
                return foundation::Result<PhysicsStepResult>::Failure(snapshot.GetError());
            }
            if (!IsValidBackendSnapshot(snapshot.Value()))
            {
                return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_backend_snapshot", "physics backend returned invalid body snapshot data");
            }
            backend_snapshots.push_back(std::pair{id, snapshot.Value()});
        }
    }
    for (const auto& [id, snapshot] : backend_snapshots)
    {
        auto body = bodies_.find(id);
        if (body == bodies_.end())
        {
            continue;
        }
        const auto applied = ApplyBackendSnapshot(body->second, snapshot);
        if (!applied)
        {
            return foundation::Result<PhysicsStepResult>::Failure(applied.GetError());
        }
    }
    for (auto& [id, body] : bodies_)
    {
        (void)id;
        if (body.desc.type == PhysicsBodyType::Dynamic && transform_sink_)
        {
            const auto written = transform_sink_->WriteTransform(body.desc.transform_node, body.world_transform);
            if (!written)
            {
                return foundation::Result<PhysicsStepResult>::Failure(written.GetError());
            }
        }
    }
    if (backend_ && backend_.get() != this)
    {
        const auto backend_contacts = backend_->ConsumeContactEvents();
        if (!backend_contacts)
        {
            return foundation::Result<PhysicsStepResult>::Failure(backend_contacts.GetError());
        }
        std::vector<ContactEvent> mapped_contacts;
        mapped_contacts.reserve(backend_contacts.Value().size());
        for (const BackendContactEvent& contact : backend_contacts.Value())
        {
            if (!IsValidBackendContactPayload(contact))
            {
                ++invalid_backend_contact_count_;
                continue;
            }
            const auto left = MapBackendBody(contact.a);
            const auto right = MapBackendBody(contact.b);
            if (!left || !right)
            {
                ++invalid_backend_contact_count_;
                continue;
            }
            mapped_contacts.push_back(ContactEvent{left.Value(), right.Value(), contact.point, contact.normal, contact.impulse, contact.state});
        }
        contacts_.insert(contacts_.end(), mapped_contacts.begin(), mapped_contacts.end());
    }
    backend_step_pending_sync_ = false;
    if (fixed_step_count_ == std::numeric_limits<std::uint64_t>::max() || revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.revision_overflow", "physics step counter or revision overflow");
    }
    ++fixed_step_count_;
    ++revision_;
    return foundation::Result<PhysicsStepResult>::Success(
        PhysicsStepResult{fixed_delta, fixed_step_count_, substeps, accumulator_, dropped_time_, revision_});
}

foundation::Result<void> PhysicsRuntime::SynchronizeBackendBody(BodyRecord& body)
{
    const auto snapshot = backend_->GetBodySnapshot(body.backend_handle);
    if (!snapshot)
    {
        return foundation::Result<void>::Failure(snapshot.GetError());
    }
    const BackendBodySnapshot candidate = snapshot.Value();
    if (!IsValidBackendSnapshot(candidate))
    {
        return PhysicsFailure("physics.invalid_backend_snapshot", "physics backend returned invalid body snapshot data");
    }
    return ApplyBackendSnapshot(body, candidate);
}

foundation::Result<void> PhysicsRuntime::ApplyBackendSnapshot(BodyRecord& body, const BackendBodySnapshot& snapshot)
{
    if (!IsValidBackendSnapshot(snapshot))
    {
        return PhysicsFailure("physics.invalid_backend_snapshot", "physics backend returned invalid body snapshot data");
    }
    body.world_transform = snapshot.world_transform;
    body.linear_velocity = snapshot.linear_velocity;
    body.angular_velocity = snapshot.angular_velocity;
    body.activity = snapshot.activity;
    body.dirty = PhysicsDirtyFlags::None;
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        return PhysicsFailure("physics.revision_overflow", "physics revision overflow");
    }
    body.revision = ++revision_;
    const auto shape = shapes_.find(body.desc.shape);
    if (shape != shapes_.end())
    {
        body.bounds = TransformAabb(body.world_transform, shape->second.desc.local_bounds);
    }
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime::physics
