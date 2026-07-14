#include "physics_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime::physics
{
foundation::Result<void> PhysicsRuntime::RegisterShape(const CollisionShapeDesc& desc)
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("physics.invalid_shape", "collision shape id must be valid before registration"));
    }

    shapes_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

bool PhysicsRuntime::HasShape(CollisionShapeId id) const
{
    return shapes_.contains(id);
}

foundation::Result<PhysicsBodyId> PhysicsRuntime::CreateBody(const PhysicsBodyDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<PhysicsBodyId>::Failure(
            foundation::Error::Create("physics.invalid_owner", "physics body owner must be valid before creation"));
    }

    if (!desc.transform_node.IsValid())
    {
        return foundation::Result<PhysicsBodyId>::Failure(
            foundation::Error::Create("physics.invalid_transform", "physics body must reference a valid scene node"));
    }

    const auto shape_it = shapes_.find(desc.shape);
    if (shape_it == shapes_.end())
    {
        return foundation::Result<PhysicsBodyId>::Failure(
            foundation::Error::Create("physics.shape_not_found", "physics body requires a registered collision shape"));
    }

    const PhysicsBodyId body_id{next_body_value_++};
    BodyRecord record{};
    record.desc = desc;
    record.state = ToBodyState(desc.type);
    record.sync_state = PhysicsSyncState::Clean;
    record.bounds = shape_it->second.local_bounds;
    bodies_.emplace(body_id, record);
    return foundation::Result<PhysicsBodyId>::Success(body_id);
}

foundation::Result<void> PhysicsRuntime::DestroyBody(PhysicsBodyId id)
{
    BodyRecord* body = FindBody(id);
    if (body == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("physics.body_not_found", "physics body was not found for destruction"));
    }

    body->state = PhysicsBodyState::PendingDestroy;
    bodies_.erase(id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> PhysicsRuntime::ApplyImpulse(PhysicsBodyId id, Vec3 impulse)
{
    BodyRecord* body = FindBody(id);
    if (body == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("physics.body_not_found", "physics body was not found for impulse application"));
    }

    if (body->state == PhysicsBodyState::Static || body->state == PhysicsBodyState::Disabled)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("physics.impulse_not_allowed", "impulse can only be applied to active movable bodies"));
    }

    body->last_impulse = impulse;
    body->state = PhysicsBodyState::Active;
    body->sync_state = PhysicsSyncState::TransformDirty;
    QueueContact(ContactEvent{id, {}, body->bounds.min, Vec3{0.0f, 1.0f, 0.0f}, impulse.x + impulse.y + impulse.z, PhysicsEventState::Queued});
    return foundation::Result<void>::Success();
}

PhysicsBodyState PhysicsRuntime::GetBodyState(PhysicsBodyId id) const
{
    const BodyRecord* body = FindBody(id);
    if (body == nullptr)
    {
        return PhysicsBodyState::PendingDestroy;
    }

    return body->state;
}

foundation::Result<RaycastHit> PhysicsRuntime::Raycast(const RaycastQuery& query) const
{
    if (query.max_distance < 0.0f)
    {
        return foundation::Result<RaycastHit>::Failure(
            foundation::Error::Create("physics.invalid_query", "raycast max distance must not be negative"));
    }

    for (const auto& [body_id, body] : bodies_)
    {
        if (PointInsideAabb(query.origin, body.bounds))
        {
            return foundation::Result<RaycastHit>::Success(
                RaycastHit{true, body_id, query.origin, Vec3{0.0f, 1.0f, 0.0f}, 0.0f});
        }
    }

    return foundation::Result<RaycastHit>::Success(RaycastHit{});
}

foundation::Result<OverlapResult> PhysicsRuntime::Overlap(const OverlapQuery& query) const
{
    OverlapResult result{};
    for (const auto& [body_id, body] : bodies_)
    {
        if (IntersectsAabb(query.bounds, body.bounds))
        {
            result.bodies.push_back(body_id);
        }
    }

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

PhysicsBodyState PhysicsRuntime::ToBodyState(PhysicsBodyType type)
{
    switch (type)
    {
    case PhysicsBodyType::Static:
        return PhysicsBodyState::Static;
    case PhysicsBodyType::Dynamic:
        return PhysicsBodyState::Dynamic;
    case PhysicsBodyType::Kinematic:
        return PhysicsBodyState::Kinematic;
    }

    return PhysicsBodyState::Disabled;
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

PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBody(PhysicsBodyId id)
{
    const auto iterator = bodies_.find(id);
    if (iterator == bodies_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const PhysicsRuntime::BodyRecord* PhysicsRuntime::FindBody(PhysicsBodyId id) const
{
    const auto iterator = bodies_.find(id);
    if (iterator == bodies_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}
} // namespace epidemic::runtime::physics
