#pragma once

#include "Epidemic/Runtime/Foundation/runtime_time.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/spatial.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace epidemic::runtime::animation
{
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

using FrameDuration = RuntimeFrameDuration;

enum class AnimatorLifecycle
{
    Uninitialized,
    Ready,
    Disabled
};

enum class AnimatorReadiness
{
    Ready,
    ResourceMissing
};

enum class AnimatorPlaybackState
{
    Stopped,
    Playing,
    Paused,
    Blending,
    Finished
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
};

struct AnimatorPlayback
{
    AnimationClipId clip{};
    bool loop = false;
    double playback_rate = 1.0;
    FrameDuration local_time{};
    double fractional_microseconds = 0.0;
};

struct CrossfadeState
{
    AnimationClipId source_clip{};
    AnimationClipId target_clip{};
    FrameDuration source_time{};
    FrameDuration target_time{};
    FrameDuration elapsed{};
    FrameDuration duration{};
    float source_weight = 1.0f;
    float target_weight = 0.0f;
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

struct AnimatorHandle
{
    AnimatorInstanceId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid() && generation != 0; }
    [[nodiscard]] constexpr bool operator==(const AnimatorHandle&) const noexcept = default;
};

struct PoseSnapshot
{
    AnimatorInstanceId animator{};
    AnimatorHandle handle{};
    PoseState state = PoseState::Clean;
    AnimationLodLevel lod = AnimationLodLevel::Full;
    std::uint64_t revision = 0;
};

struct PoseBuffer
{
    AnimatorHandle animator{};
    std::vector<Transform> bone_transforms{};
    std::uint64_t revision = 0;
};

struct AnimatorSnapshot
{
    AnimatorHandle handle{};
    AnimatorLifecycle lifecycle = AnimatorLifecycle::Uninitialized;
    AnimatorReadiness readiness = AnimatorReadiness::ResourceMissing;
    AnimatorPlaybackState playback = AnimatorPlaybackState::Stopped;
    AnimationLodLevel lod = AnimationLodLevel::Full;
    AnimationClipId clip{};
    FrameDuration local_time{};
    std::uint64_t revision = 0;
};

struct AnimationEvaluationRequest
{
    AnimatorHandle animator{};
    SkeletonDesc skeleton{};
    AnimationClipDesc source_clip{};
    AnimationClipDesc target_clip{};
    FrameDuration source_time{};
    FrameDuration target_time{};
    float source_weight = 1.0f;
    float target_weight = 0.0f;
    AnimationLodLevel lod = AnimationLodLevel::Full;
    std::uint64_t revision = 0;
};

struct AnimationPlaybackCommand
{
    AnimatorHandle animator{};
    AnimationClipId clip{};
    bool loop = false;
    double playback_rate = 1.0;
};

struct AnimationOptions
{
    bool enable_mock_pose_evaluation = false;
    std::size_t event_capacity = 64;
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

