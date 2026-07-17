#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/spatial.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace epidemic::runtime::physics
{
// File note:
// Shared public value types for the Physics major. These describe body identity,
// shape data, query payloads and event snapshots without binding the module to a real physics SDK.

struct PhysicsBodyId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const PhysicsBodyId&) const noexcept = default;
};

struct CollisionShapeId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const CollisionShapeId&) const noexcept = default;
};

enum class PhysicsBodyType
{
    Static,
    Dynamic,
    Kinematic
};

enum class PhysicsBodyLifecycle
{
    Creating,
    Alive,
    Destroying,
    Destroyed
};

enum class PhysicsActivityState
{
    Static,
    Awake,
    Sleeping,
    Disabled
};

enum class PhysicsBodyState
{
    PendingCreate,
    Static,
    Dynamic,
    Kinematic,
    Sleeping,
    Active,
    Disabled,
    PendingDestroy
};

enum class PhysicsSyncState
{
    Clean,
    TransformDirty,
    ShapeDirty,
    MaterialDirty
};

enum class PhysicsEventState
{
    Queued,
    Consumed,
    Expired
};

enum class PhysicsDirtyFlags : std::uint32_t
{
    None = 0,
    Transform = 1 << 0,
    Shape = 1 << 1,
    Material = 1 << 2,
    Activity = 1 << 3
};

[[nodiscard]] constexpr PhysicsDirtyFlags operator|(PhysicsDirtyFlags left, PhysicsDirtyFlags right) noexcept
{
    return static_cast<PhysicsDirtyFlags>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool HasFlag(PhysicsDirtyFlags value, PhysicsDirtyFlags flag) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

using PhysicsTransformId = RuntimeObjectId;

struct PhysicsBodyDesc
{
    RuntimeObjectId owner{};
    PhysicsTransformId transform_node{};
    CollisionShapeId shape{};
    PhysicsBodyType type = PhysicsBodyType::Static;
    float mass = 0.0f;
};

struct CollisionShapeDesc
{
    CollisionShapeId id{};
    Aabb local_bounds{};
};

struct PhysicsMaterial
{
    float friction = 0.5f;
    float restitution = 0.0f;
};

struct ContactEvent
{
    PhysicsBodyId a{};
    PhysicsBodyId b{};
    Vec3 point{};
    Vec3 normal{};
    float impulse = 0.0f;
    PhysicsEventState state = PhysicsEventState::Queued;
};

struct RaycastQuery
{
    Vec3 origin{};
    Vec3 direction{};
    float max_distance = 0.0f;
};

struct RaycastHit
{
    bool hit = false;
    PhysicsBodyId body{};
    Vec3 point{};
    Vec3 normal{};
    float distance = 0.0f;
};

struct OverlapQuery
{
    Aabb bounds{};
};

struct OverlapResult
{
    std::vector<PhysicsBodyId> bodies;
};

struct PhysicsStepResult
{
    GameDuration fixed_delta{};
    std::uint64_t step_index = 0;
    std::uint64_t revision = 0;
};

struct PhysicsBodySnapshot
{
    PhysicsBodyId id{};
    RuntimeObjectId owner{};
    PhysicsTransformId transform_node{};
    PhysicsBodyType type = PhysicsBodyType::Static;
    PhysicsBodyLifecycle lifecycle = PhysicsBodyLifecycle::Destroyed;
    PhysicsActivityState activity = PhysicsActivityState::Disabled;
    PhysicsDirtyFlags dirty = PhysicsDirtyFlags::None;
    std::uint64_t revision = 0;
};
} // namespace epidemic::runtime::physics

namespace std
{
template <> struct hash<epidemic::runtime::physics::PhysicsBodyId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::physics::PhysicsBodyId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::physics::CollisionShapeId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::physics::CollisionShapeId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
