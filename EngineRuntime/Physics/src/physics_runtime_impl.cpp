#include "physics_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
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

template <typename TResult, typename TCallable>
[[nodiscard]] TResult InvokeExternal(TCallable&& callable,
                                     std::string_view operation)
{
    try
    {
        return std::forward<TCallable>(callable)();
    }
    catch (const std::exception& exception)
    {
        return TResult::Failure(
            foundation::Error::Create("physics.backend_exception",
                                      "external physics dependency threw an exception",
                                      std::string(operation) + ": " + exception.what()));
    }
    catch (...)
    {
        return TResult::Failure(
            foundation::Error::Create("physics.backend_exception",
                                      "external physics dependency threw an exception",
                                      operation));
    }
}

[[nodiscard]] bool CanAdvanceRevision(std::uint64_t current, std::uint64_t count = 1) noexcept
{
    return count <= std::numeric_limits<std::uint64_t>::max() - current;
}

[[nodiscard]] bool CheckedAddDuration(RuntimeFrameDuration left,
                                      RuntimeFrameDuration right,
                                      RuntimeFrameDuration& result) noexcept
{
    const auto lhs = left.value.count();
    const auto rhs = right.value.count();
    using Rep = decltype(lhs);
    if ((rhs > 0 && lhs > std::numeric_limits<Rep>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<Rep>::min() - rhs))
    {
        return false;
    }
    result.value = std::chrono::microseconds{lhs + rhs};
    return true;
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

foundation::Result<void> PhysicsRuntime::DrainPendingBackendCleanup()
{
    if (!backend_ || backend_.get() == this)
    {
        pending_backend_body_cleanup_.clear();
        pending_backend_shape_cleanup_.clear();
        return foundation::Result<void>::Success();
    }

    std::optional<foundation::Error> first;
    for (auto iterator = pending_backend_body_cleanup_.begin(); iterator != pending_backend_body_cleanup_.end();)
    {
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyBody(*iterator); },
            "DestroyBody pending rollback");
        if (destroyed)
        {
            iterator = pending_backend_body_cleanup_.erase(iterator);
        }
        else
        {
            if (!first)
            {
                first = destroyed.GetError();
            }
            ++iterator;
        }
    }
    for (auto iterator = pending_backend_shape_cleanup_.begin(); iterator != pending_backend_shape_cleanup_.end();)
    {
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyShape(*iterator); },
            "DestroyShape pending rollback");
        if (destroyed)
        {
            iterator = pending_backend_shape_cleanup_.erase(iterator);
        }
        else
        {
            if (!first)
            {
                first = destroyed.GetError();
            }
            ++iterator;
        }
    }
    return first ? foundation::Result<void>::Failure(*first) : foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::Shutdown()
{
    if (const auto pending = DrainPendingBackendCleanup(); !pending)
    {
        return pending;
    }
    if (backend_ && backend_.get() != this)
    {
        std::optional<foundation::Error> first_error;
        for (auto iterator = bodies_.begin(); iterator != bodies_.end();)
        {
            const auto destroyed = InvokeExternal<foundation::Result<void>>(
                [&] { return backend_->DestroyBody(iterator->second.backend_handle); },
                "DestroyBody during shutdown");
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
            const auto destroyed = InvokeExternal<foundation::Result<void>>(
                [&] { return backend_->DestroyShape(iterator->second.backend_handle); },
                "DestroyShape during shutdown");
            if (!destroyed && !first_error)
            {
                first_error = destroyed.GetError();
            }
            if (!destroyed)
            {
                ++iterator;
                continue;
            }
            backend_to_shape_.erase(iterator->second.backend_handle.value);
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
        backend_to_shape_.clear();
        backend_to_body_.clear();
    }
    pending_transform_projections_.clear();
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
    if (const auto pending = DrainPendingBackendCleanup(); !pending)
    {
        return pending;
    }

    const bool uses_external_backend = backend_ && backend_.get() != this;
    if (!uses_external_backend && !CanAllocateMonotonicId(next_backend_shape_value_))
    {
        return PhysicsFailure("physics.backend_shape_id_overflow", "backend shape id allocator is exhausted");
    }

    try
    {
        shapes_.reserve(shapes_.size() + 1);
        backend_to_shape_.reserve(backend_to_shape_.size() + 1);
        pending_backend_shape_cleanup_.reserve(pending_backend_shape_cleanup_.size() + 1);
    }
    catch (const std::bad_alloc&)
    {
        return PhysicsFailure("physics.allocation_failure", "could not reserve collision-shape bookkeeping");
    }

    BackendShapeHandle backend_shape{};
    std::uint64_t next_backend_shape_candidate = next_backend_shape_value_;
    if (uses_external_backend)
    {
        const auto created = InvokeExternal<foundation::Result<BackendShapeHandle>>(
            [&] { return backend_->CreateShape(desc); },
            "CreateShape");
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
        backend_shape = BackendShapeHandle{next_backend_shape_candidate};
        next_backend_shape_candidate = next_backend_shape_candidate == std::numeric_limits<std::uint64_t>::max()
                                           ? 0
                                           : next_backend_shape_candidate + 1;
    }

    auto rollback_backend = [&]() -> foundation::Result<void> {
        if (!uses_external_backend)
        {
            return foundation::Result<void>::Success();
        }
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyShape(backend_shape); },
            "DestroyShape rollback");
        if (!destroyed)
        {
            pending_backend_shape_cleanup_.push_back(backend_shape);
            return PhysicsFailure("physics.cleanup_pending", "backend shape rollback is pending");
        }
        return foundation::Result<void>::Success();
    };

    if (fail_next_shape_publication_for_testing_)
    {
        fail_next_shape_publication_for_testing_ = false;
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailure("physics.allocation_failure", "simulated collision-shape publication failure")
                       : cleanup;
    }

    if (backend_to_shape_.contains(backend_shape.value))
    {
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailure("physics.duplicate_backend_shape_handle", "physics backend returned a duplicate shape handle")
                       : cleanup;
    }

    try
    {
        const auto [shape_iterator, inserted] = shapes_.emplace(desc.id, ShapeRecord{desc, backend_shape});
        if (!inserted)
        {
            const auto cleanup = rollback_backend();
            return cleanup ? PhysicsFailure("physics.duplicate_shape", "collision shape id is already registered")
                           : cleanup;
        }
        (void)shape_iterator;
        const auto [backend_iterator, backend_inserted] = backend_to_shape_.emplace(backend_shape.value, desc.id);
        if (!backend_inserted)
        {
            shapes_.erase(desc.id);
            const auto cleanup = rollback_backend();
            return cleanup ? PhysicsFailure("physics.duplicate_backend_shape_handle", "physics backend returned a duplicate shape handle")
                           : cleanup;
        }
        (void)backend_iterator;
    }
    catch (const std::bad_alloc&)
    {
        shapes_.erase(desc.id);
        backend_to_shape_.erase(backend_shape.value);
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailure("physics.allocation_failure", "could not publish collision-shape bookkeeping")
                       : cleanup;
    }

    if (!uses_external_backend)
    {
        next_backend_shape_value_ = next_backend_shape_candidate;
    }
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
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyShape(iterator->second.backend_handle); },
            "DestroyShape");
        if (!destroyed)
        {
            return destroyed;
        }
    }
    backend_to_shape_.erase(iterator->second.backend_handle.value);
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
    if (!desc.shape.IsValid())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_shape", "physics body shape id must be valid");
    }
    if (!IsValidBodyType(desc.type))
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_body_type", "physics body type is outside the declared enum domain");
    }
    if (!std::isfinite(desc.mass) || desc.mass < 0.0f || (desc.type == PhysicsBodyType::Dynamic && desc.mass <= 0.0f))
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_mass", "physics body mass must be finite and positive for dynamic bodies");
    }
    if (!IsValidTransform(desc.initial_transform))
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_transform", "physics initial transform must contain finite valid TRS values");
    }

    const auto shape_it = shapes_.find(desc.shape);
    if (shape_it == shapes_.end())
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.shape_not_found", "physics body requires a registered collision shape");
    }
    if (const auto pending = DrainPendingBackendCleanup(); !pending)
    {
        return foundation::Result<PhysicsBodyHandle>::Failure(pending.GetError());
    }

    Transform initial_transform = desc.initial_transform;
    if (transform_source_)
    {
        const auto transform = InvokeExternal<foundation::Result<Transform>>(
            [&] { return transform_source_->ReadTransform(desc.transform_node); },
            "ReadTransform for body creation");
        if (!transform)
        {
            return foundation::Result<PhysicsBodyHandle>::Failure(transform.GetError());
        }
        if (!IsValidTransform(transform.Value()))
        {
            return PhysicsFailureValue<PhysicsBodyHandle>("physics.invalid_transform", "physics transform source returned invalid TRS values");
        }
        initial_transform = transform.Value();
    }

    const bool uses_external_backend = backend_ && backend_.get() != this;
    if (!CanAllocateMonotonicId(next_body_value_) || !CanAllocateMonotonicId(next_body_generation_) ||
        (!uses_external_backend && !CanAllocateMonotonicId(next_backend_body_value_)) ||
        !CanAdvanceRevision(revision_))
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.body_id_overflow", "physics body id/generation/revision allocator is exhausted");
    }

    try
    {
        bodies_.reserve(bodies_.size() + 1);
        backend_to_body_.reserve(backend_to_body_.size() + 1);
        pending_backend_body_cleanup_.reserve(pending_backend_body_cleanup_.size() + 1);
    }
    catch (const std::bad_alloc&)
    {
        return PhysicsFailureValue<PhysicsBodyHandle>("physics.allocation_failure", "could not reserve physics body bookkeeping");
    }

    const std::uint64_t body_value = next_body_value_;
    const std::uint32_t body_generation = next_body_generation_;
    const std::uint64_t next_body_after = body_value == std::numeric_limits<std::uint64_t>::max() ? 0 : body_value + 1;
    const std::uint32_t next_generation_after = body_generation == std::numeric_limits<std::uint32_t>::max() ? 0 : body_generation + 1;
    const PhysicsBodyHandle handle{PhysicsBodyId{body_value}, body_generation};

    PhysicsBodyDesc backend_desc = desc;
    backend_desc.initial_transform = initial_transform;
    BackendBodyHandle backend_handle{};
    std::uint64_t next_backend_body_after = next_backend_body_value_;
    if (uses_external_backend)
    {
        const auto backend_body = InvokeExternal<foundation::Result<BackendBodyHandle>>(
            [&] { return backend_->CreateBody(backend_desc, shape_it->second.backend_handle); },
            "CreateBody");
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
        backend_handle = BackendBodyHandle{next_backend_body_value_};
        next_backend_body_after = next_backend_body_value_ == std::numeric_limits<std::uint64_t>::max()
                                      ? 0
                                      : next_backend_body_value_ + 1;
    }

    auto rollback_backend = [&]() -> foundation::Result<void> {
        if (!uses_external_backend)
        {
            return foundation::Result<void>::Success();
        }
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyBody(backend_handle); },
            "DestroyBody rollback");
        if (!destroyed)
        {
            pending_backend_body_cleanup_.push_back(backend_handle);
            return PhysicsFailure("physics.cleanup_pending", "backend body rollback is pending");
        }
        return foundation::Result<void>::Success();
    };

    if (fail_next_body_publication_for_testing_)
    {
        fail_next_body_publication_for_testing_ = false;
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailureValue<PhysicsBodyHandle>("physics.allocation_failure", "simulated physics body publication failure")
                       : foundation::Result<PhysicsBodyHandle>::Failure(cleanup.GetError());
    }

    if (backend_to_body_.contains(backend_handle))
    {
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_backend_body_handle", "physics backend returned a duplicate body handle")
                       : foundation::Result<PhysicsBodyHandle>::Failure(cleanup.GetError());
    }

    BodyRecord record{};
    record.handle = handle;
    record.backend_handle = backend_handle;
    record.desc = desc;
    record.activity = ActivityForType(desc.type);
    record.dirty = PhysicsDirtyFlags::Transform | PhysicsDirtyFlags::Shape;
    record.world_transform = initial_transform;
    record.bounds = TransformAabb(record.world_transform, shape_it->second.desc.local_bounds);
    record.revision = revision_ + 1;

    try
    {
        const auto [body_iterator, body_inserted] = bodies_.emplace(handle.id, record);
        if (!body_inserted)
        {
            const auto cleanup = rollback_backend();
            return cleanup ? PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_body_id", "allocated physics body id already exists")
                           : foundation::Result<PhysicsBodyHandle>::Failure(cleanup.GetError());
        }
        (void)body_iterator;
        const auto [backend_iterator, backend_inserted] = backend_to_body_.emplace(backend_handle, handle.id);
        if (!backend_inserted)
        {
            bodies_.erase(handle.id);
            const auto cleanup = rollback_backend();
            return cleanup ? PhysicsFailureValue<PhysicsBodyHandle>("physics.duplicate_backend_body_handle", "physics backend returned a duplicate body handle")
                           : foundation::Result<PhysicsBodyHandle>::Failure(cleanup.GetError());
        }
        (void)backend_iterator;
    }
    catch (const std::bad_alloc&)
    {
        backend_to_body_.erase(backend_handle);
        bodies_.erase(handle.id);
        const auto cleanup = rollback_backend();
        return cleanup ? PhysicsFailureValue<PhysicsBodyHandle>("physics.allocation_failure", "could not publish physics body bookkeeping")
                       : foundation::Result<PhysicsBodyHandle>::Failure(cleanup.GetError());
    }

    // Commit allocator/revision state only after every local ownership mapping exists.
    next_body_value_ = next_body_after;
    next_body_generation_ = next_generation_after;
    if (!uses_external_backend)
    {
        next_backend_body_value_ = next_backend_body_after;
    }
    ++revision_;
    return foundation::Result<PhysicsBodyHandle>::Success(handle);
}

foundation::Result<void> PhysicsRuntime::DestroyBody(PhysicsBodyHandle handle)
{
    BodyRecord* body = FindBody(handle);
    if (body == nullptr)
    {
        return ValidateHandle(handle);
    }

    if (!CanAdvanceRevision(revision_))
    {
        return PhysicsFailure("physics.revision_overflow", "physics revision is exhausted");
    }

    if (backend_ && backend_.get() != this)
    {
        const auto destroyed = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->DestroyBody(body->backend_handle); },
            "DestroyBody");
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
    if (!CanAdvanceRevision(revision_))
    {
        return PhysicsFailure("physics.revision_overflow", "physics revision is exhausted");
    }

    if (backend_ && backend_.get() != this)
    {
        const auto applied = InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->ApplyImpulse(body->backend_handle, impulse); },
            "ApplyImpulse");
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
        const auto snapshot = InvokeExternal<foundation::Result<BackendBodySnapshot>>(
            [&] { return backend_->GetBodySnapshot(body->backend_handle); },
            "GetBodySnapshot");
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

    RuntimeFrameDuration candidate_accumulator{};
    if (!CheckedAddDuration(accumulator_, delta, candidate_accumulator))
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.time_overflow", "physics fixed-step accumulator overflow");
    }

    // Preflight dropped-time arithmetic before any backend/local step can commit.
    RuntimeFrameDuration projected_accumulator = candidate_accumulator;
    std::uint32_t projected_substeps = 0;
    while (fixed_step_.value.count() > 0 && projected_accumulator.value >= fixed_step_.value &&
           projected_substeps < max_substeps_)
    {
        projected_accumulator.value -= fixed_step_.value;
        ++projected_substeps;
    }
    if (fixed_step_.value.count() > 0 && projected_accumulator.value >= fixed_step_.value)
    {
        RuntimeFrameDuration ignored{};
        if (!CheckedAddDuration(dropped_time_, projected_accumulator, ignored))
        {
            return PhysicsFailureValue<PhysicsStepResult>("physics.time_overflow", "physics dropped-time counter overflow");
        }
    }

    accumulator_ = candidate_accumulator;
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
        RuntimeFrameDuration candidate_dropped{};
        if (!CheckedAddDuration(dropped_time_, accumulator_, candidate_dropped))
        {
            return PhysicsFailureValue<PhysicsStepResult>("physics.time_overflow", "physics dropped-time counter overflow");
        }
        dropped_time_ = candidate_dropped;
        accumulator_.value = std::chrono::microseconds{0};
    }

    return foundation::Result<PhysicsStepResult>::Success(
        PhysicsStepResult{fixed_step_, fixed_step_count_, substeps, accumulator_, dropped_time_, revision_,
                          static_cast<std::uint32_t>(std::min<std::size_t>(pending_transform_projections_.size(),
                                                                         std::numeric_limits<std::uint32_t>::max()))});
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
    if (!shape.IsValid() || !desc.owner.IsValid() || !desc.transform_node.IsValid() || !desc.shape.IsValid())
    {
        return PhysicsFailureValue<BackendBodyHandle>("physics.invalid_body", "backend body requires valid shape and owner");
    }
    if (!IsValidBodyType(desc.type))
    {
        return PhysicsFailureValue<BackendBodyHandle>("physics.invalid_body_type", "backend body type is outside the declared enum domain");
    }
    if (!std::isfinite(desc.mass) || desc.mass < 0.0f || (desc.type == PhysicsBodyType::Dynamic && desc.mass <= 0.0f) ||
        !IsValidTransform(desc.initial_transform))
    {
        return PhysicsFailureValue<BackendBodyHandle>("physics.invalid_body", "backend body descriptor contains invalid mass or transform data");
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

foundation::Result<void> PhysicsRuntime::ApplyImpulse(BackendBodyHandle handle, const Vec3& impulse)
{
    if (!handle.IsValid())
    {
        return PhysicsFailure("physics.invalid_handle", "backend body handle must be valid");
    }
    if (!IsFinite(impulse))
    {
        return PhysicsFailure("physics.invalid_impulse", "backend impulse must contain finite values");
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
        return InvokeExternal<foundation::Result<void>>(
            [&] { return backend_->SimulateFixed(fixed_delta); },
            "SimulateFixed");
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
        const auto hit = InvokeExternal<foundation::Result<BackendRaycastHit>>(
            [&] { return backend_->RaycastBackend(normalized); },
            "RaycastBackend");
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

bool PhysicsRuntime::IsValidBodyType(PhysicsBodyType value) noexcept
{
    switch (value)
    {
    case PhysicsBodyType::Static:
    case PhysicsBodyType::Dynamic:
    case PhysicsBodyType::Kinematic:
        return true;
    }
    return false;
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
    const bool uses_external_backend = backend_ && backend_.get() != this;
    const std::uint64_t body_revision_count = uses_external_backend
                                                  ? static_cast<std::uint64_t>(bodies_.size())
                                                  : 0;
    if (fixed_step_count_ == std::numeric_limits<std::uint64_t>::max())
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.step_overflow", "physics fixed-step counter is exhausted");
    }
    if (body_revision_count == std::numeric_limits<std::uint64_t>::max() ||
        !CanAdvanceRevision(revision_, body_revision_count + 1))
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.revision_overflow", "physics revision is exhausted");
    }

    // Retry lagging Scene projection independently from authoritative physics progress.
    FlushPendingTransformProjections();

    if (!backend_step_pending_sync_)
    {
        const auto simulated = SimulateFixed(fixed_delta);
        if (!simulated)
        {
            return foundation::Result<PhysicsStepResult>::Failure(simulated.GetError());
        }
        backend_step_pending_sync_ = uses_external_backend;
    }

    struct StagedBody
    {
        PhysicsBodyId id{};
        BodyRecord record{};
    };

    std::vector<StagedBody> staged_bodies;
    std::vector<PhysicsBodyId> ordered_ids;
    std::vector<ContactEvent> mapped_contacts;
    std::vector<PendingTransformProjection> candidate_projections;
    std::uint64_t invalid_contact_delta = 0;

    try
    {
        ordered_ids.reserve(bodies_.size());
        for (const auto& [id, body] : bodies_)
        {
            (void)body;
            ordered_ids.push_back(id);
        }
        std::sort(ordered_ids.begin(), ordered_ids.end(), [](PhysicsBodyId left, PhysicsBodyId right) {
            return left.value < right.value;
        });
        staged_bodies.reserve(ordered_ids.size());
    }
    catch (const std::bad_alloc&)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.allocation_failure", "could not allocate fixed-step staging state");
    }

    std::uint64_t revision_cursor = revision_;
    if (uses_external_backend)
    {
        for (PhysicsBodyId id : ordered_ids)
        {
            const auto body = bodies_.find(id);
            if (body == bodies_.end())
            {
                return PhysicsFailureValue<PhysicsStepResult>("physics.body_state_changed", "physics body disappeared during fixed-step staging");
            }
            const auto snapshot = InvokeExternal<foundation::Result<BackendBodySnapshot>>(
                [&] { return backend_->GetBodySnapshot(body->second.backend_handle); },
                "GetBodySnapshot during fixed step");
            if (!snapshot)
            {
                return foundation::Result<PhysicsStepResult>::Failure(snapshot.GetError());
            }
            if (!IsValidBackendSnapshot(snapshot.Value()))
            {
                return PhysicsFailureValue<PhysicsStepResult>("physics.invalid_backend_snapshot", "physics backend returned invalid body snapshot data");
            }
            const auto shape = shapes_.find(body->second.desc.shape);
            if (shape == shapes_.end())
            {
                return PhysicsFailureValue<PhysicsStepResult>("physics.shape_not_found", "physics body references a missing collision shape");
            }

            BodyRecord staged = body->second;
            staged.world_transform = snapshot.Value().world_transform;
            staged.linear_velocity = snapshot.Value().linear_velocity;
            staged.angular_velocity = snapshot.Value().angular_velocity;
            staged.activity = snapshot.Value().activity;
            staged.dirty = PhysicsDirtyFlags::None;
            staged.bounds = TransformAabb(staged.world_transform, shape->second.desc.local_bounds);
            staged.revision = ++revision_cursor;
            staged_bodies.push_back(StagedBody{id, staged});
        }

        if (!backend_contacts_pending_sync_)
        {
            const auto backend_contacts = InvokeExternal<foundation::Result<std::vector<BackendContactEvent>>>(
                [&] { return backend_->ConsumeContactEvents(); },
                "ConsumeContactEvents");
            if (!backend_contacts)
            {
                return foundation::Result<PhysicsStepResult>::Failure(backend_contacts.GetError());
            }
            pending_backend_contacts_ = std::move(backend_contacts).Value();
            backend_contacts_pending_sync_ = true;
        }

        try
        {
            mapped_contacts.reserve(pending_backend_contacts_.size());
        }
        catch (const std::bad_alloc&)
        {
            return PhysicsFailureValue<PhysicsStepResult>("physics.allocation_failure", "could not allocate contact-event staging state");
        }

        for (const BackendContactEvent& contact : pending_backend_contacts_)
        {
            if (!IsValidBackendContactPayload(contact))
            {
                ++invalid_contact_delta;
                continue;
            }
            const auto left = MapBackendBody(contact.a);
            const auto right = MapBackendBody(contact.b);
            if (!left || !right)
            {
                ++invalid_contact_delta;
                continue;
            }
            mapped_contacts.push_back(ContactEvent{left.Value(), right.Value(), contact.point, contact.normal,
                                                   contact.impulse, contact.state});
        }
    }

    try
    {
        if (mapped_contacts.size() > contacts_.max_size() - contacts_.size())
        {
            return PhysicsFailureValue<PhysicsStepResult>("physics.contact_capacity_overflow", "physics contact buffer size overflow");
        }
        contacts_.reserve(contacts_.size() + mapped_contacts.size());

        candidate_projections = pending_transform_projections_;
        candidate_projections.reserve(candidate_projections.size() + bodies_.size());

        auto stage_projection = [&](PhysicsTransformId id, const Transform& transform) {
            const auto existing = std::find_if(candidate_projections.begin(), candidate_projections.end(),
                                               [&](const PendingTransformProjection& pending) {
                                                   return pending.id == id;
                                               });
            if (existing != candidate_projections.end())
            {
                existing->transform = transform;
            }
            else
            {
                candidate_projections.push_back(PendingTransformProjection{id, transform});
            }
        };

        if (uses_external_backend)
        {
            for (const StagedBody& staged : staged_bodies)
            {
                if (staged.record.desc.type == PhysicsBodyType::Dynamic && transform_sink_)
                {
                    stage_projection(staged.record.desc.transform_node, staged.record.world_transform);
                }
            }
        }
        else
        {
            for (PhysicsBodyId id : ordered_ids)
            {
                const auto body = bodies_.find(id);
                if (body != bodies_.end() && body->second.desc.type == PhysicsBodyType::Dynamic && transform_sink_)
                {
                    stage_projection(body->second.desc.transform_node, body->second.world_transform);
                }
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return PhysicsFailureValue<PhysicsStepResult>("physics.allocation_failure", "could not allocate fixed-step commit buffers");
    }

    // All fallible validation/allocation work is complete. Commit local authoritative state.
    if (uses_external_backend)
    {
        for (const StagedBody& staged : staged_bodies)
        {
            auto body = bodies_.find(staged.id);
            if (body != bodies_.end())
            {
                body->second = staged.record;
            }
        }
    }
    contacts_.insert(contacts_.end(), mapped_contacts.begin(), mapped_contacts.end());
    invalid_backend_contact_count_ += invalid_contact_delta;
    pending_backend_contacts_.clear();
    backend_contacts_pending_sync_ = false;
    backend_step_pending_sync_ = false;

    ++revision_cursor; // revision for the fixed-step commit itself
    revision_ = revision_cursor;
    ++fixed_step_count_;

    pending_transform_projections_.swap(candidate_projections);
    FlushPendingTransformProjections();

    return foundation::Result<PhysicsStepResult>::Success(
        PhysicsStepResult{fixed_delta,
                          fixed_step_count_,
                          substeps,
                          accumulator_,
                          dropped_time_,
                          revision_,
                          static_cast<std::uint32_t>(std::min<std::size_t>(pending_transform_projections_.size(),
                                                                         std::numeric_limits<std::uint32_t>::max()))});
}

void PhysicsRuntime::FlushPendingTransformProjections() noexcept
{
    if (!transform_sink_)
    {
        pending_transform_projections_.clear();
        return;
    }

    for (std::size_t index = 0; index < pending_transform_projections_.size();)
    {
        bool completed = false;
        try
        {
            const auto written = transform_sink_->WriteTransform(pending_transform_projections_[index].id,
                                                                 pending_transform_projections_[index].transform);
            completed = written.HasValue();
        }
        catch (...)
        {
            completed = false;
        }

        if (completed)
        {
            pending_transform_projections_.erase(pending_transform_projections_.begin() +
                                                 static_cast<std::ptrdiff_t>(index));
        }
        else
        {
            ++index;
        }
    }
}

foundation::Result<void> PhysicsRuntime::SynchronizeBackendBody(BodyRecord& body)
{
    const auto snapshot = InvokeExternal<foundation::Result<BackendBodySnapshot>>(
        [&] { return backend_->GetBodySnapshot(body.backend_handle); },
        "GetBodySnapshot during body synchronization");
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
    if (!CanAdvanceRevision(revision_))
    {
        return PhysicsFailure("physics.revision_overflow", "physics revision overflow");
    }
    const auto shape = shapes_.find(body.desc.shape);
    if (shape == shapes_.end())
    {
        return PhysicsFailure("physics.shape_not_found", "physics body references a missing collision shape");
    }
    const Aabb staged_bounds = TransformAabb(snapshot.world_transform, shape->second.desc.local_bounds);

    body.world_transform = snapshot.world_transform;
    body.linear_velocity = snapshot.linear_velocity;
    body.angular_velocity = snapshot.angular_velocity;
    body.activity = snapshot.activity;
    body.dirty = PhysicsDirtyFlags::None;
    body.bounds = staged_bounds;
    body.revision = ++revision_;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime::physics
