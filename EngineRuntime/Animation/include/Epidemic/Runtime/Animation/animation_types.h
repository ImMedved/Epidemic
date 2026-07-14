#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

#include <cstdint>
#include <functional>
#include <string>

namespace epidemic::runtime::animation
{
// File note:
// Public value types for the Animation major. They model runtime animation data identities,
// animator lifecycle states, generic events and LOD hints without defining higher-level actor behavior.

struct SkeletonId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const SkeletonId&) const noexcept = default;
};

struct AnimationClipId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const AnimationClipId&) const noexcept = default;
};

struct AnimatorInstanceId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const AnimatorInstanceId&) const noexcept = default;
};

enum class AnimatorState
{
    Uninitialized,
    Ready,
    Playing,
    Blending,
    Paused,
    Finished,
    ResourceMissing,
    Disabled
};

enum class PoseState
{
    Clean,
    Dirty,
    Evaluating,
    Ready
};

enum class AnimationLodLevel
{
    Full,
    Reduced,
    Frozen
};

struct SkeletonDesc
{
    SkeletonId id{};
    std::uint32_t joint_count = 0;
};

struct AnimationClipDesc
{
    AnimationClipId id{};
    SkeletonId skeleton{};
    float duration_seconds = 0.0f;
    bool loop = false;
};

struct AnimatorDesc
{
    RuntimeObjectId owner{};
    SkeletonId skeleton{};
    AnimationLodLevel lod = AnimationLodLevel::Full;
};

struct AnimationEvent
{
    AnimatorInstanceId animator{};
    std::string name;
    float time = 0.0f;
};

struct AnimationOptions
{
    bool enable_mock_pose_evaluation = true;
};
} // namespace epidemic::runtime::animation

namespace std
{
template <> struct hash<epidemic::runtime::animation::SkeletonId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::animation::SkeletonId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::animation::AnimationClipId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::animation::AnimationClipId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::animation::AnimatorInstanceId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::animation::AnimatorInstanceId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std

