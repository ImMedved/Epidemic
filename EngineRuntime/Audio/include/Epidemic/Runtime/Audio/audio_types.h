#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"
#include "Epidemic/Runtime/Foundation/spatial.h"
#include "Epidemic/Foundation/result.h"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace epidemic::runtime::audio
{
// Public value types for the Audio major. These records describe sound resources, emitters, listeners,
// mixer state and queued one-shot events without binding to a concrete audio backend.

struct SoundId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const SoundId&) const noexcept = default;
};

struct AudioEmitterId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const AudioEmitterId&) const noexcept = default;
};

struct AudioListenerId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const AudioListenerId&) const noexcept = default;
};

struct BackendVoiceHandle
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const BackendVoiceHandle&) const noexcept = default;
};

struct MixerGroupId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const MixerGroupId&) const noexcept = default;
};

struct AudioEmitterHandle
{
    AudioEmitterId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid() && generation != 0; }
    [[nodiscard]] constexpr bool operator==(const AudioEmitterHandle&) const noexcept = default;
};

struct AudioListenerHandle
{
    AudioListenerId id{};
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return id.IsValid() && generation != 0; }
    [[nodiscard]] constexpr bool operator==(const AudioListenerHandle&) const noexcept = default;
};

using AudioTransformId = RuntimeObjectId;

enum class AudioEventSpace
{
    NonSpatial,
    WorldPosition
};

enum class AudioEventOverflowPolicy
{
    DropNewest,
    DropOldest,
    FailSubmit
};

enum class SoundState
{
    Missing,
    Loading,
    Ready,
    Failed
};

enum class EmitterState
{
    Stopped,
    Playing,
    Paused,
    FadingIn,
    FadingOut,
    Virtualized,
    Destroyed
};

enum class MixerFadeState
{
    Stable,
    FadingIn,
    FadingOut
};

struct SoundDesc
{
    SoundId id{};
    SoundState state = SoundState::Ready;
};

struct AudioClipPayload
{
    SoundId sound{};
    SoundState state = SoundState::Ready;
    std::shared_ptr<const class IAudioClipResource> resource;
};

enum class AudioClipStorage
{
    InMemoryEncoded,
    Streaming
};

struct AudioClipFormat
{
    std::uint32_t channel_count = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t bits_per_sample = 0;
    bool compressed = false;
};

class IAudioClipResource
{
public:
    virtual ~IAudioClipResource() = default;

    [[nodiscard]] virtual AudioClipFormat GetFormat() const = 0;
    [[nodiscard]] virtual AudioClipStorage GetStorage() const = 0;
    [[nodiscard]] virtual std::span<const std::byte> GetEncodedData() const = 0;
    [[nodiscard]] virtual std::shared_ptr<class IAudioStreamSource> GetStreamSource() const = 0;
};

struct AudioStreamReadResult
{
    std::size_t bytes_read = 0;
    bool end_of_stream = false;
};

class IAudioStreamSource
{
public:
    virtual ~IAudioStreamSource() = default;

    [[nodiscard]] virtual std::uint64_t GetSizeBytes() const = 0;
    [[nodiscard]] virtual bool IsSeekable() const = 0;
    [[nodiscard]] virtual foundation::Result<AudioStreamReadResult> Read(std::uint64_t offset, std::span<std::byte> output) = 0;
};

struct AudioBackendOptions
{
    bool enabled = true;
};

struct AudioSpatialState
{
    Transform transform{};
    bool spatial = true;
};

struct AudioEmitterDesc
{
    RuntimeObjectId owner{};
    SoundId sound{};
    AudioTransformId transform{};
    bool loop = false;
    MixerGroupId mixer_group{};
    float gain = 1.0f;
    bool spatial = true;
};

struct AudioVoiceDesc
{
    AudioClipPayload clip{};
    bool loop = false;
    MixerGroupId mixer_group{};
    float initial_gain = 1.0f;
    AudioSpatialState spatial{};
};

struct AudioEmitterSnapshot
{
    AudioEmitterId id{};
    AudioEmitterHandle handle{};
    SoundId sound{};
    AudioTransformId transform{};
    EmitterState state = EmitterState::Destroyed;
    RuntimeFrameDuration fade_duration{};
    float fade_progress = 0.0f;
    std::uint64_t revision = 0;
};

struct AudioListenerDesc
{
    AudioTransformId transform{};
};

struct AudioEvent
{
    SoundId sound{};
    Vec3 position{};
    float volume = 1.0f;
    AudioEventSpace space = AudioEventSpace::WorldPosition;
};

struct MixerGroupState
{
    MixerGroupId id{};
    float volume = 1.0f;
    MixerFadeState fade_state = MixerFadeState::Stable;
    MixerGroupId parent{};
    RuntimeFrameDuration fade_duration{};
    float fade_progress = 0.0f;
};

struct AudioOptions
{
    bool enable_mock_backend = true;
    std::uint32_t max_queued_events = 64;
    AudioEventOverflowPolicy event_overflow_policy = AudioEventOverflowPolicy::FailSubmit;
    bool allow_mixer_boost = false;
};
} // namespace epidemic::runtime::audio

namespace std
{
template <> struct hash<epidemic::runtime::audio::SoundId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::audio::SoundId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::audio::AudioEmitterId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::audio::AudioEmitterId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::audio::AudioListenerId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::audio::AudioListenerId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::audio::BackendVoiceHandle>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::audio::BackendVoiceHandle value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};

template <> struct hash<epidemic::runtime::audio::MixerGroupId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::audio::MixerGroupId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
