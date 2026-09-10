#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace epidemic::runtime::audio
{
namespace
{
class MockAudioClipResource final : public IAudioClipResource
{
public:
    [[nodiscard]] AudioClipFormat GetFormat() const override
    {
        return AudioClipFormat{2, 48000, 16, false};
    }

    [[nodiscard]] AudioClipStorage GetStorage() const override
    {
        return AudioClipStorage::InMemoryEncoded;
    }

    [[nodiscard]] std::span<const std::byte> GetEncodedData() const override
    {
        return data_;
    }

    [[nodiscard]] std::shared_ptr<IAudioStreamSource> GetStreamSource() const override
    {
        return {};
    }

private:
    std::array<std::byte, 4> data_{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};
};

[[nodiscard]] bool IsValid(AudioEventSpace value) noexcept
{
    return value == AudioEventSpace::NonSpatial || value == AudioEventSpace::WorldPosition;
}

[[nodiscard]] bool IsValid(AudioEventOverflowPolicy value) noexcept
{
    return value == AudioEventOverflowPolicy::DropNewest || value == AudioEventOverflowPolicy::DropOldest ||
           value == AudioEventOverflowPolicy::FailSubmit;
}

[[nodiscard]] bool IsValid(SoundState value) noexcept
{
    return value == SoundState::Missing || value == SoundState::Loading || value == SoundState::Ready || value == SoundState::Failed;
}

[[nodiscard]] bool IsValid(MixerFadeState value) noexcept
{
    return value == MixerFadeState::Stable || value == MixerFadeState::FadingIn || value == MixerFadeState::FadingOut;
}

[[nodiscard]] bool IsFiniteSpatial(const AudioSpatialState& state) noexcept
{
    return IsValidTransform(state.transform);
}

[[nodiscard]] std::uint64_t NextValue(std::uint64_t value) noexcept
{
    return value == std::numeric_limits<std::uint64_t>::max() ? 0u : value + 1u;
}

[[nodiscard]] std::uint32_t NextValue(std::uint32_t value) noexcept
{
    return value == std::numeric_limits<std::uint32_t>::max() ? 0u : static_cast<std::uint32_t>(value + 1u);
}

template <typename TCallable>
[[nodiscard]] auto CallAudioBoundary(TCallable&& callable, std::string_view code, std::string_view message) -> decltype(callable())
{
    using TResult = decltype(callable());
    try
    {
        return callable();
    }
    catch (const std::exception&)
    {
        return TResult::Failure(foundation::Error::Create(code, message));
    }
    catch (...)
    {
        return TResult::Failure(foundation::Error::Create(code, message));
    }
}

[[nodiscard]] RuntimeFrameDuration AdvanceFadeElapsed(RuntimeFrameDuration elapsed, RuntimeFrameDuration duration, RuntimeFrameDuration delta) noexcept
{
    const std::int64_t duration_count = duration.value.count();
    if (duration_count <= 0)
    {
        return RuntimeFrameDuration{};
    }
    const std::int64_t elapsed_count = std::clamp<std::int64_t>(elapsed.value.count(), 0, duration_count);
    const std::int64_t remaining = duration_count - elapsed_count;
    const std::int64_t delta_count = std::max<std::int64_t>(0, delta.value.count());
    if (delta_count >= remaining)
    {
        return duration;
    }
    return RuntimeFrameDuration{std::chrono::microseconds{elapsed_count + delta_count}};
}
} // namespace

AudioRuntime::AudioRuntime(AudioOptions options, AudioDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
}

foundation::Result<void> AudioRuntime::RegisterSound(SoundDesc desc)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_sound", "sound id must be valid before registration"));
    }
    if (!IsValid(desc.state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_sound_state", "sound state is outside the declared enum domain"));
    }
    if (sounds_.contains(desc.id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_already_registered", "sound id is already registered"));
    }
    try
    {
        sounds_.emplace(desc.id, desc);
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "sound registry allocation failed"));
    }
    return foundation::Result<void>::Success();
}

SoundState AudioRuntime::GetSoundState(SoundId id) const
{
    if (dependencies_.resources != nullptr)
    {
        try
        {
            const SoundState state = dependencies_.resources->GetSoundState(id);
            if (state != SoundState::Missing)
            {
                return IsValid(state) ? state : SoundState::Failed;
            }
        }
        catch (...)
        {
            return SoundState::Failed;
        }
    }

    const auto iterator = sounds_.find(id);
    if (iterator == sounds_.end())
    {
        return SoundState::Missing;
    }

    return iterator->second.state;
}

bool AudioRuntime::IsEnabled() const
{
    return options_.enable_mock_backend;
}

foundation::Result<void> AudioRuntime::Initialize(const AudioBackendOptions& options)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    options_.enable_mock_backend = options.enabled;
    return foundation::Result<void>::Success();
}

foundation::Result<BackendVoiceHandle> AudioRuntime::CreateVoice(const AudioVoiceDesc& desc)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(accepting.GetError());
    }
    if (!options_.enable_mock_backend)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.backend_disabled", "mock audio backend is disabled"));
    }
    if (!desc.clip.sound.IsValid() || desc.clip.state != SoundState::Ready || desc.clip.resource == nullptr)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio clip payload is not ready"));
    }
    if (!IsFinite(desc.initial_gain) || desc.initial_gain < 0.0f)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.invalid_gain", "audio voice gain must not be negative"));
    }
    if (!IsFiniteSpatial(desc.spatial))
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.invalid_spatial_state", "audio voice spatial state must contain a valid finite transform"));
    }
    if (next_voice_value_ == 0)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.voice_id_overflow", "mock backend voice id allocator is exhausted"));
    }

    const BackendVoiceHandle handle{next_voice_value_};
    try
    {
        const auto [_, inserted] = voices_.emplace(handle, MockVoiceRecord{desc, EmitterState::Stopped, desc.initial_gain, desc.spatial});
        if (!inserted)
        {
            return foundation::Result<BackendVoiceHandle>::Failure(
                foundation::Error::Create("audio.duplicate_voice_id", "allocated backend voice id already exists"));
        }
    }
    catch (const std::exception&)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.allocation_failed", "backend voice storage allocation failed"));
    }
    next_voice_value_ = NextValue(next_voice_value_);
    return foundation::Result<BackendVoiceHandle>::Success(handle);
}

foundation::Result<void> AudioRuntime::DestroyVoice(BackendVoiceHandle handle)
{
    if (voices_.erase(handle) == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Play(BackendVoiceHandle handle)
{
    auto iterator = voices_.find(handle);
    if (iterator == voices_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found for play"));
    }
    iterator->second.state = EmitterState::Playing;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Pause(BackendVoiceHandle handle)
{
    auto iterator = voices_.find(handle);
    if (iterator == voices_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found for pause"));
    }
    iterator->second.state = EmitterState::Paused;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Stop(BackendVoiceHandle handle)
{
    auto iterator = voices_.find(handle);
    if (iterator == voices_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found for stop"));
    }
    iterator->second.state = EmitterState::Stopped;
    return foundation::Result<void>::Success();
}

bool AudioRuntime::IsVoiceFinished(BackendVoiceHandle handle) const
{
    const auto iterator = voices_.find(handle);
    return iterator == voices_.end() || iterator->second.state == EmitterState::Stopped;
}

foundation::Result<void> AudioRuntime::SetGain(BackendVoiceHandle handle, float gain)
{
    auto iterator = voices_.find(handle);
    if (iterator == voices_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found for gain update"));
    }
    if (!IsFinite(gain) || gain < 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_gain", "backend voice gain must be finite and non-negative"));
    }
    iterator->second.gain = gain;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::SetSpatialState(BackendVoiceHandle handle, const AudioSpatialState& state)
{
    auto iterator = voices_.find(handle);
    if (iterator == voices_.end())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_voice_not_found", "backend voice was not found for spatial update"));
    }
    if (!IsFiniteSpatial(state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_spatial_state", "backend spatial state must contain a valid finite transform"));
    }
    iterator->second.spatial = state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::CreateBackendListener(AudioListenerHandle handle, const Transform& transform)
{
    if (!handle.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener", "backend listener handle must be valid"));
    }
    if (!IsValidTransform(transform))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener_transform", "backend listener transform must be finite and valid"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::DestroyBackendListener(AudioListenerHandle handle)
{
    if (!handle.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener", "backend listener handle must be valid"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::SetBackendListenerTransform(AudioListenerHandle handle, const Transform& transform)
{
    if (!handle.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener", "backend listener handle must be valid"));
    }
    if (!IsValidTransform(transform))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener_transform", "backend listener transform must be finite and valid"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Update(RuntimeFrameDuration)
{
    return foundation::Result<void>::Success();
}

foundation::Result<AudioEmitterHandle> AudioRuntime::CreateEmitterHandle(const AudioEmitterDesc& desc)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(accepting.GetError());
    }
    if (!desc.owner.IsValid())
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.invalid_owner", "audio emitter owner must be valid before creation"));
    }
    if (!desc.sound.IsValid() || GetSoundState(desc.sound) == SoundState::Missing)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.sound_not_found", "audio emitter must reference a registered sound"));
    }
    if (!desc.transform.IsValid())
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.invalid_transform", "audio emitter must reference a valid scene node"));
    }
    if (!IsFinite(desc.gain) || desc.gain < 0.0f || (!options_.allow_mixer_boost && desc.gain > 1.0f))
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.invalid_gain", "audio emitter gain is outside the supported range"));
    }
    if (next_emitter_value_ == 0 || next_emitter_generation_ == 0)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.emitter_id_overflow", "audio emitter id allocator is exhausted"));
    }

    const AudioEmitterId id{next_emitter_value_};
    const AudioEmitterHandle handle{id, next_emitter_generation_};
    EmitterRecord record{};
    record.desc = desc;
    record.handle = handle;
    record.state = EmitterState::Stopped;
    record.base_gain = desc.gain;
    record.fade_multiplier = 1.0f;
    record.fade_start_multiplier = 1.0f;
    record.fade_target_multiplier = 1.0f;
    record.revision = 1;
    try
    {
        const auto [_, inserted] = emitters_.emplace(id, record);
        if (!inserted)
        {
            return foundation::Result<AudioEmitterHandle>::Failure(
                foundation::Error::Create("audio.duplicate_emitter_id", "allocated audio emitter id already exists"));
        }
    }
    catch (const std::exception&)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.allocation_failed", "audio emitter storage allocation failed"));
    }
    next_emitter_value_ = NextValue(next_emitter_value_);
    next_emitter_generation_ = NextValue(next_emitter_generation_);
    return foundation::Result<AudioEmitterHandle>::Success(handle);
}

foundation::Result<void> AudioRuntime::DestroyEmitter(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        if (backend != nullptr)
        {
            const auto destroyed = CallAudioBoundary(
                [&] { return backend->DestroyVoice(emitter->voice); },
                "audio.backend_exception",
                "audio backend threw while destroying an emitter voice");
            if (!destroyed)
            {
                return foundation::Result<void>::Failure(destroyed.GetError());
            }
        }
    }
    emitters_.erase(handle.id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Play(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    const bool local_change = emitter->state != EmitterState::Playing || emitter->fade_duration.value.count() != 0 ||
                              emitter->fade_progress != 0.0f || emitter->fade_multiplier != 1.0f || emitter->paused_playback_state.has_value();
    if (local_change)
    {
        const auto revision = EnsureEmitterRevisionAvailable(*emitter);
        if (!revision)
        {
            return revision;
        }
    }

    const auto payload = ResolvePayload(emitter->desc.sound);
    if (!payload || payload.Value().state != SoundState::Ready || payload.Value().resource == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio emitter sound is not ready"));
    }
    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required for playback"));
    }
    const auto spatial_state = ReadSpatialState(emitter->desc.transform, emitter->desc.spatial);
    if (!spatial_state)
    {
        return foundation::Result<void>::Failure(spatial_state.GetError());
    }

    EmitterRecord candidate = *emitter;
    ResetFade(candidate);
    candidate.fade_multiplier = 1.0f;
    bool created_voice = false;
    if (!candidate.voice.IsValid())
    {
        const AudioVoiceDesc voice_desc{payload.Value(), candidate.desc.loop, candidate.desc.mixer_group, EffectiveGain(candidate), spatial_state.Value()};
        const auto voice = CallAudioBoundary(
            [&] { return backend->CreateVoice(voice_desc); },
            "audio.backend_exception",
            "audio backend threw while creating an emitter voice");
        if (!voice)
        {
            return foundation::Result<void>::Failure(voice.GetError());
        }
        candidate.voice = voice.Value();
        candidate.clip_resource = payload.Value().resource;
        created_voice = true;
    }

    const auto spatial = CallAudioBoundary(
        [&] { return backend->SetSpatialState(candidate.voice, spatial_state.Value()); },
        "audio.backend_exception",
        "audio backend threw while setting emitter spatial state");
    if (!spatial)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
        }
        return foundation::Result<void>::Failure(spatial.GetError());
    }
    const auto gain = CallAudioBoundary(
        [&] { return backend->SetGain(candidate.voice, EffectiveGain(candidate)); },
        "audio.backend_exception",
        "audio backend threw while setting emitter gain");
    if (!gain)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
        }
        return foundation::Result<void>::Failure(gain.GetError());
    }
    const auto played = CallAudioBoundary(
        [&] { return backend->Play(candidate.voice); },
        "audio.backend_exception",
        "audio backend threw while starting emitter playback");
    if (!played)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
        }
        return foundation::Result<void>::Failure(played.GetError());
    }

    candidate.state = EmitterState::Playing;
    if (local_change)
    {
        CommitEmitterRevision(candidate);
    }
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Stop(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (emitter->state == EmitterState::Stopped)
    {
        return foundation::Result<void>::Success();
    }
    const auto revision = EnsureEmitterRevisionAvailable(*emitter);
    if (!revision)
    {
        return revision;
    }
    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        const auto stopped = CallAudioBoundary(
            [&] { return backend->Stop(emitter->voice); },
            "audio.backend_exception",
            "audio backend threw while stopping emitter playback");
        if (!stopped)
        {
            return foundation::Result<void>::Failure(stopped.GetError());
        }
    }
    EmitterRecord candidate = *emitter;
    candidate.state = EmitterState::Stopped;
    ResetFade(candidate);
    CommitEmitterRevision(candidate);
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Pause(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (emitter->state != EmitterState::Playing && emitter->state != EmitterState::FadingIn && emitter->state != EmitterState::FadingOut)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_transition", "only active playback can be paused"));
    }
    const auto revision = EnsureEmitterRevisionAvailable(*emitter);
    if (!revision)
    {
        return revision;
    }
    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        const auto paused = CallAudioBoundary(
            [&] { return backend->Pause(emitter->voice); },
            "audio.backend_exception",
            "audio backend threw while pausing emitter playback");
        if (!paused)
        {
            return foundation::Result<void>::Failure(paused.GetError());
        }
    }
    EmitterRecord candidate = *emitter;
    candidate.paused_playback_state = candidate.state;
    candidate.state = EmitterState::Paused;
    CommitEmitterRevision(candidate);
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Resume(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (emitter->state != EmitterState::Paused)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_transition", "only paused playback can be resumed"));
    }
    const auto revision = EnsureEmitterRevisionAvailable(*emitter);
    if (!revision)
    {
        return revision;
    }
    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        const auto played = CallAudioBoundary(
            [&] { return backend->Play(emitter->voice); },
            "audio.backend_exception",
            "audio backend threw while resuming emitter playback");
        if (!played)
        {
            return foundation::Result<void>::Failure(played.GetError());
        }
    }
    EmitterRecord candidate = *emitter;
    candidate.state = candidate.paused_playback_state.value_or(EmitterState::Playing);
    candidate.paused_playback_state.reset();
    CommitEmitterRevision(candidate);
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::FadeOut(AudioEmitterHandle handle, RuntimeFrameDuration duration)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    EmitterRecord candidate = *emitter;
    const auto faded = BeginFade(candidate, EmitterState::FadingOut, duration);
    if (!faded)
    {
        return faded;
    }
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::FadeIn(AudioEmitterHandle handle, RuntimeFrameDuration duration)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (duration.value.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_fade", "fade duration must not be negative"));
    }

    EmitterRecord candidate = *emitter;
    bool created_voice = false;
    IAudioBackend* backend = Backend();
    if (candidate.state == EmitterState::Stopped || candidate.state == EmitterState::Virtualized)
    {
        const auto revision = EnsureEmitterRevisionAvailable(candidate);
        if (!revision)
        {
            return revision;
        }
        const auto payload = ResolvePayload(candidate.desc.sound);
        if (!payload || payload.Value().state != SoundState::Ready || payload.Value().resource == nullptr)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.sound_not_ready", "audio emitter sound is not ready"));
        }
        if (backend == nullptr)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.backend_missing", "audio backend is required for playback"));
        }
        const auto spatial_state = ReadSpatialState(candidate.desc.transform, candidate.desc.spatial);
        if (!spatial_state)
        {
            return foundation::Result<void>::Failure(spatial_state.GetError());
        }
        if (!candidate.voice.IsValid())
        {
            const AudioVoiceDesc voice_desc{payload.Value(), candidate.desc.loop, candidate.desc.mixer_group, 0.0f, spatial_state.Value()};
            const auto voice = CallAudioBoundary(
                [&] { return backend->CreateVoice(voice_desc); },
                "audio.backend_exception",
                "audio backend threw while creating a fade-in voice");
            if (!voice)
            {
                return foundation::Result<void>::Failure(voice.GetError());
            }
            candidate.voice = voice.Value();
            candidate.clip_resource = payload.Value().resource;
            created_voice = true;
        }
        const auto spatial = CallAudioBoundary(
            [&] { return backend->SetSpatialState(candidate.voice, spatial_state.Value()); },
            "audio.backend_exception",
            "audio backend threw while setting fade-in spatial state");
        if (!spatial)
        {
            if (created_voice) RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
            return foundation::Result<void>::Failure(spatial.GetError());
        }
        const auto gain = CallAudioBoundary(
            [&] { return backend->SetGain(candidate.voice, 0.0f); },
            "audio.backend_exception",
            "audio backend threw while setting initial fade-in gain");
        if (!gain)
        {
            if (created_voice) RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
            return foundation::Result<void>::Failure(gain.GetError());
        }
        const auto played = CallAudioBoundary(
            [&] { return backend->Play(candidate.voice); },
            "audio.backend_exception",
            "audio backend threw while starting fade-in playback");
        if (!played)
        {
            if (created_voice) RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
            return foundation::Result<void>::Failure(played.GetError());
        }
        candidate.state = EmitterState::Playing;
        candidate.fade_multiplier = 0.0f;
    }

    const auto faded = BeginFade(candidate, EmitterState::FadingIn, duration);
    if (!faded)
    {
        if (created_voice && backend != nullptr)
        {
            RollbackCreatedVoice(*backend, candidate.voice, candidate.clip_resource);
        }
        else if (backend != nullptr && emitter->state == EmitterState::Stopped && emitter->voice.IsValid())
        {
            (void)CallAudioBoundary([&] { return backend->Stop(emitter->voice); }, "audio.backend_exception", "audio backend threw during fade-in compensation");
        }
        return faded;
    }
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Virtualize(AudioEmitterHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }
    if (emitter->state == EmitterState::Virtualized)
    {
        return foundation::Result<void>::Success();
    }
    const auto revision = EnsureEmitterRevisionAvailable(*emitter);
    if (!revision)
    {
        return revision;
    }
    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        if (backend != nullptr)
        {
            const auto destroyed = CallAudioBoundary(
                [&] { return backend->DestroyVoice(emitter->voice); },
                "audio.backend_exception",
                "audio backend threw while virtualizing an emitter");
            if (!destroyed)
            {
                return foundation::Result<void>::Failure(destroyed.GetError());
            }
        }
    }
    EmitterRecord candidate = *emitter;
    candidate.voice = {};
    candidate.clip_resource.reset();
    candidate.state = EmitterState::Virtualized;
    ResetFade(candidate);
    CommitEmitterRevision(candidate);
    *emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<EmitterState> AudioRuntime::GetEmitterState(AudioEmitterHandle handle) const
{
    const EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<EmitterState>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }

    return foundation::Result<EmitterState>::Success(emitter->state);
}

foundation::Result<AudioEmitterSnapshot> AudioRuntime::GetEmitterSnapshot(AudioEmitterHandle handle) const
{
    const EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<AudioEmitterSnapshot>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }

    return foundation::Result<AudioEmitterSnapshot>::Success(AudioEmitterSnapshot{
        handle.id,
        emitter->handle,
        emitter->desc.sound,
        emitter->desc.transform,
        emitter->state,
        emitter->fade_duration,
        emitter->fade_progress,
        emitter->revision,
    });
}

foundation::Result<void> AudioRuntime::Tick(RuntimeFrameDuration delta)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    if (delta.value.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_delta", "audio tick delta must not be negative"));
    }
    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required for tick"));
    }

    struct EmitterCommand
    {
        BackendVoiceHandle voice{};
        AudioSpatialState spatial{};
        float gain = 1.0f;
        bool stop = false;
    };

    std::unordered_map<AudioEmitterId, EmitterRecord> staged_emitters;
    std::unordered_map<MixerGroupId, MixerGroupState> staged_mixer_groups;
    std::unordered_map<MixerGroupId, MixerFadeRecord> staged_mixer_fades;
    std::vector<EmitterCommand> commands;
    std::vector<AudioEmitterId> emitter_ids;
    std::vector<VoiceOwnership> new_one_shots;
    try
    {
        staged_emitters = emitters_;
        staged_mixer_groups = mixer_groups_;
        staged_mixer_fades = mixer_fades_;
        emitter_ids.reserve(staged_emitters.size());
        commands.reserve(staged_emitters.size());
        new_one_shots.reserve(events_.size());
        one_shot_voices_.reserve(one_shot_voices_.size() + events_.size());
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "audio tick staging allocation failed"));
    }

    // Stage mixer fade progress using saturating-to-duration arithmetic.
    std::vector<MixerGroupId> mixer_ids;
    try
    {
        mixer_ids.reserve(staged_mixer_fades.size());
        for (const auto& [id, fade] : staged_mixer_fades)
        {
            (void)fade;
            mixer_ids.push_back(id);
        }
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "audio mixer tick staging allocation failed"));
    }
    std::sort(mixer_ids.begin(), mixer_ids.end(), [](MixerGroupId left, MixerGroupId right) { return left.value < right.value; });
    for (MixerGroupId id : mixer_ids)
    {
        auto state_it = staged_mixer_groups.find(id);
        auto fade_it = staged_mixer_fades.find(id);
        if (state_it == staged_mixer_groups.end() || fade_it == staged_mixer_fades.end())
        {
            continue;
        }
        MixerGroupState& state = state_it->second;
        MixerFadeRecord& fade = fade_it->second;
        fade.elapsed = AdvanceFadeElapsed(fade.elapsed, state.fade_duration, delta);
        const std::int64_t duration = std::max<std::int64_t>(1, state.fade_duration.value.count());
        state.fade_progress = std::clamp(static_cast<float>(fade.elapsed.value.count()) / static_cast<float>(duration), 0.0f, 1.0f);
        state.volume = fade.start_volume + ((fade.target_volume - fade.start_volume) * state.fade_progress);
        if (state.fade_progress >= 1.0f)
        {
            state.volume = fade.target_volume;
            state.fade_state = MixerFadeState::Stable;
            staged_mixer_fades.erase(id);
        }
    }

    const auto staged_effective_gain = [&](const EmitterRecord& emitter) {
        float gain = emitter.base_gain * emitter.fade_multiplier;
        MixerGroupId current = emitter.desc.mixer_group;
        while (current.IsValid())
        {
            const auto iterator = staged_mixer_groups.find(current);
            if (iterator == staged_mixer_groups.end())
            {
                break;
            }
            gain *= iterator->second.volume;
            current = iterator->second.parent;
        }
        return gain;
    };

    for (const auto& [id, emitter] : staged_emitters)
    {
        (void)emitter;
        emitter_ids.push_back(id);
    }
    std::sort(emitter_ids.begin(), emitter_ids.end(), [](AudioEmitterId left, AudioEmitterId right) { return left.value < right.value; });

    for (AudioEmitterId id : emitter_ids)
    {
        EmitterRecord& candidate = staged_emitters.at(id);
        const EmitterRecord& original = emitters_.at(id);
        if (candidate.state == EmitterState::FadingOut || candidate.state == EmitterState::FadingIn)
        {
            const auto revision = EnsureEmitterRevisionAvailable(original);
            if (!revision)
            {
                return revision;
            }
            candidate.fade_elapsed = AdvanceFadeElapsed(candidate.fade_elapsed, candidate.fade_duration, delta);
            const std::int64_t duration = std::max<std::int64_t>(1, candidate.fade_duration.value.count());
            candidate.fade_progress = std::clamp(static_cast<float>(candidate.fade_elapsed.value.count()) / static_cast<float>(duration), 0.0f, 1.0f);
            candidate.fade_multiplier = candidate.fade_start_multiplier +
                                        ((candidate.fade_target_multiplier - candidate.fade_start_multiplier) * candidate.fade_progress);
        }

        if (candidate.voice.IsValid())
        {
            const auto spatial = ReadSpatialState(candidate.desc.transform, candidate.desc.spatial);
            if (!spatial)
            {
                return foundation::Result<void>::Failure(spatial.GetError());
            }
            EmitterCommand command{};
            command.voice = candidate.voice;
            command.spatial = spatial.Value();
            command.gain = staged_effective_gain(candidate);
            command.stop = candidate.state == EmitterState::FadingOut && candidate.fade_progress >= 1.0f;
            commands.push_back(command);
        }

        if (candidate.state == EmitterState::FadingOut || candidate.state == EmitterState::FadingIn)
        {
            if (candidate.fade_progress >= 1.0f)
            {
                const bool fading_out = candidate.state == EmitterState::FadingOut;
                candidate.state = fading_out ? EmitterState::Stopped : EmitterState::Playing;
                ResetFade(candidate);
            }
            CommitEmitterRevision(candidate);
        }
    }

    // Create one-shot backend objects into temporary ownership records. No queue/event state is consumed yet.
    for (const AudioEvent& event : events_)
    {
        const auto created = CreateOneShotVoice(event);
        if (!created)
        {
            for (auto& owned : new_one_shots)
            {
                RollbackCreatedVoice(*backend, owned.voice, owned.clip_resource);
            }
            return foundation::Result<void>::Failure(created.GetError());
        }
        new_one_shots.push_back(created.Value());
    }

    const auto rollback_new_one_shots = [&]() {
        for (auto& owned : new_one_shots)
        {
            RollbackCreatedVoice(*backend, owned.voice, owned.clip_resource);
        }
    };

    const auto listener = ApplyMainListenerTransform();
    if (!listener)
    {
        rollback_new_one_shots();
        return listener;
    }
    for (const EmitterCommand& command : commands)
    {
        const auto spatial = CallAudioBoundary(
            [&] { return backend->SetSpatialState(command.voice, command.spatial); },
            "audio.backend_exception",
            "audio backend threw while applying tick spatial state");
        if (!spatial)
        {
            rollback_new_one_shots();
            return foundation::Result<void>::Failure(spatial.GetError());
        }
        const auto gain = CallAudioBoundary(
            [&] { return backend->SetGain(command.voice, command.gain); },
            "audio.backend_exception",
            "audio backend threw while applying tick gain");
        if (!gain)
        {
            rollback_new_one_shots();
            return foundation::Result<void>::Failure(gain.GetError());
        }
        if (command.stop)
        {
            const auto stopped = CallAudioBoundary(
                [&] { return backend->Stop(command.voice); },
                "audio.backend_exception",
                "audio backend threw while completing a fade-out in tick");
            if (!stopped)
            {
                rollback_new_one_shots();
                return foundation::Result<void>::Failure(stopped.GetError());
            }
        }
    }
    const auto updated = CallAudioBoundary(
        [&] { return backend->Update(delta); },
        "audio.backend_exception",
        "audio backend threw during tick update");
    if (!updated)
    {
        rollback_new_one_shots();
        return foundation::Result<void>::Failure(updated.GetError());
    }

    // No-fail local publication after every fallible operation succeeded.
    emitters_.swap(staged_emitters);
    mixer_groups_.swap(staged_mixer_groups);
    mixer_fades_.swap(staged_mixer_fades);
    for (auto& owned : new_one_shots)
    {
        one_shot_voices_.push_back(std::move(owned));
    }
    events_.clear();

    // Cleanup is a recoverable ownership reconciliation path and does not invalidate the completed tick.
    const auto pending_cleanup = CleanupPendingVoices();
    if (!pending_cleanup)
    {
        RecordCleanupFailure(pending_cleanup.GetError());
    }
    const auto one_shots = CleanupFinishedOneShots();
    if (!one_shots)
    {
        RecordCleanupFailure(one_shots.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Shutdown()
{
    shutdown_started_ = true;
    if (shutdown_complete_)
    {
        return foundation::Result<void>::Success();
    }
    IAudioBackend* backend = Backend();
    std::optional<foundation::Error> first_error;

    if (backend != nullptr)
    {
        const auto pending_cleanup = CleanupPendingVoices();
        if (!pending_cleanup && !first_error.has_value())
        {
            first_error = pending_cleanup.GetError();
        }
        const auto pending_listeners = CleanupPendingListeners();
        if (!pending_listeners && !first_error.has_value())
        {
            first_error = pending_listeners.GetError();
        }

        for (auto& [id, emitter] : emitters_)
        {
            (void)id;
            if (!emitter.voice.IsValid())
            {
                continue;
            }
            const auto revision = EnsureEmitterRevisionAvailable(emitter);
            if (!revision)
            {
                if (!first_error.has_value())
                {
                    first_error = revision.GetError();
                }
                continue;
            }
            const auto destroyed = CallAudioBoundary(
                [&] { return backend->DestroyVoice(emitter.voice); },
                "audio.backend_exception",
                "audio backend threw while destroying an emitter during shutdown");
            if (!destroyed && !first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            if (destroyed)
            {
                emitter.voice = {};
                emitter.clip_resource.reset();
                emitter.state = EmitterState::Stopped;
                ResetFade(emitter);
                CommitEmitterRevision(emitter);
            }
        }

        for (auto iterator = listeners_.begin(); iterator != listeners_.end();)
        {
            const AudioListenerHandle handle = iterator->second.handle;
            const auto destroyed = CallAudioBoundary(
                [&] { return backend->DestroyBackendListener(handle); },
                "audio.backend_exception",
                "audio backend threw while destroying a listener during shutdown");
            if (!destroyed && !first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            if (destroyed)
            {
                if (main_listener_ == handle)
                {
                    main_listener_.reset();
                }
                iterator = listeners_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    if (backend != nullptr)
    {
        std::vector<VoiceOwnership> survivors;
        try
        {
            survivors.reserve(one_shot_voices_.size());
        }
        catch (const std::exception&)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.allocation_failed", "audio shutdown cleanup allocation failed"));
        }
        for (auto& owned : one_shot_voices_)
        {
            if (!owned.voice.IsValid())
            {
                continue;
            }
            const auto destroyed = CallAudioBoundary(
                [&] { return backend->DestroyVoice(owned.voice); },
                "audio.backend_exception",
                "audio backend threw while destroying a one-shot voice during shutdown");
            if (!destroyed)
            {
                if (!first_error.has_value())
                {
                    first_error = destroyed.GetError();
                }
                survivors.push_back(std::move(owned));
            }
        }
        one_shot_voices_ = std::move(survivors);
    }

    if (first_error.has_value())
    {
        return foundation::Result<void>::Failure(*first_error);
    }

    events_.clear();
    one_shot_voices_.clear();
    pending_voice_cleanups_.clear();
    pending_listener_cleanups_.clear();
    main_listener_.reset();
    listeners_.clear();
    emitters_.clear();
    voices_.clear();
    shutdown_complete_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<AudioListenerHandle> AudioRuntime::CreateListenerHandle(const AudioListenerDesc& desc)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return foundation::Result<AudioListenerHandle>::Failure(accepting.GetError());
    }
    if (!desc.transform.IsValid())
    {
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.invalid_listener_transform", "audio listener must reference a valid scene node"));
    }
    if (next_listener_value_ == 0 || next_listener_generation_ == 0)
    {
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.listener_id_overflow", "audio listener id allocator is exhausted"));
    }

    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = CallAudioBoundary(
            [&] { return Transforms()->ReadTransform(desc.transform); },
            "audio.transform_source_exception",
            "audio transform source threw while creating a listener");
        if (!read)
        {
            return foundation::Result<AudioListenerHandle>::Failure(read.GetError());
        }
        transform = read.Value();
        if (!IsValidTransform(transform))
        {
            return foundation::Result<AudioListenerHandle>::Failure(
                foundation::Error::Create("audio.invalid_listener_transform", "audio listener transform source returned a non-finite transform"));
        }
    }

    const AudioListenerId id{next_listener_value_};
    const AudioListenerHandle handle{id, next_listener_generation_};
    IAudioBackend* backend = Backend();
    if (backend != nullptr)
    {
        try
        {
            pending_listener_cleanups_.reserve(pending_listener_cleanups_.size() + 1);
        }
        catch (const std::exception&)
        {
            return foundation::Result<AudioListenerHandle>::Failure(
                foundation::Error::Create("audio.allocation_failed", "audio listener rollback storage allocation failed"));
        }
        const auto created = CallAudioBoundary(
            [&] { return backend->CreateBackendListener(handle, transform); },
            "audio.backend_exception",
            "audio backend threw while creating a listener");
        if (!created)
        {
            return foundation::Result<AudioListenerHandle>::Failure(created.GetError());
        }
    }

    try
    {
        if (fail_next_listener_publication_for_testing_)
        {
            fail_next_listener_publication_for_testing_ = false;
            throw std::bad_alloc{};
        }
        const auto [_, inserted] = listeners_.emplace(id, ListenerRecord{desc, handle});
        if (!inserted)
        {
            if (backend != nullptr)
            {
                RollbackCreatedListener(*backend, handle);
            }
            return foundation::Result<AudioListenerHandle>::Failure(
                foundation::Error::Create("audio.duplicate_listener_id", "allocated audio listener id already exists"));
        }
    }
    catch (const std::exception&)
    {
        if (backend != nullptr)
        {
            RollbackCreatedListener(*backend, handle);
        }
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.allocation_failed", "audio listener storage allocation failed"));
    }

    next_listener_value_ = NextValue(next_listener_value_);
    next_listener_generation_ = NextValue(next_listener_generation_);
    return foundation::Result<AudioListenerHandle>::Success(handle);
}

foundation::Result<void> AudioRuntime::SetMainListener(AudioListenerHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    const ListenerRecord* listener = FindListener(handle);
    if (listener == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_listener", "audio listener handle is stale"));
    }
    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = CallAudioBoundary(
            [&] { return Transforms()->ReadTransform(listener->desc.transform); },
            "audio.transform_source_exception",
            "audio transform source threw while selecting the main listener");
        if (!read)
        {
            return foundation::Result<void>::Failure(read.GetError());
        }
        transform = read.Value();
        if (!IsValidTransform(transform))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.invalid_listener_transform", "audio listener transform source returned a non-finite transform"));
        }
    }
    IAudioBackend* backend = Backend();
    if (backend != nullptr)
    {
        const auto applied = CallAudioBoundary(
            [&] { return backend->SetBackendListenerTransform(listener->handle, transform); },
            "audio.backend_exception",
            "audio backend threw while updating the main listener");
        if (!applied)
        {
            return foundation::Result<void>::Failure(applied.GetError());
        }
    }
    main_listener_ = handle;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::DestroyListener(AudioListenerHandle handle)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    if (FindListener(handle) == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_listener", "audio listener handle is stale"));
    }
    IAudioBackend* backend = Backend();
    if (backend != nullptr)
    {
        const auto destroyed = CallAudioBoundary(
            [&] { return backend->DestroyBackendListener(handle); },
            "audio.backend_exception",
            "audio backend threw while destroying a listener");
        if (!destroyed)
        {
            return foundation::Result<void>::Failure(destroyed.GetError());
        }
    }
    listeners_.erase(handle.id);
    if (main_listener_ == handle)
    {
        main_listener_.reset();
    }
    return foundation::Result<void>::Success();
}

std::optional<AudioListenerHandle> AudioRuntime::GetMainListener() const
{
    return main_listener_;
}

foundation::Result<void> AudioRuntime::SubmitOneShot(const AudioEvent& event)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    if (!IsValid(event.space))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_event_space", "audio event space is outside the declared enum domain"));
    }
    if (!IsValid(options_.event_overflow_policy))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_overflow_policy", "audio event overflow policy is outside the declared enum domain"));
    }
    if (GetSoundState(event.sound) != SoundState::Ready)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "one-shot sound is not ready"));
    }
    if (!IsFinite(event.volume) || event.volume < 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_volume", "one-shot volume must be finite and non-negative"));
    }
    if (!std::isfinite(event.position.x) || !std::isfinite(event.position.y) || !std::isfinite(event.position.z))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_event_position", "one-shot position must contain finite values"));
    }
    if (event.space == AudioEventSpace::NonSpatial && event.position != Vec3{})
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.contradictory_event_source", "non-spatial audio event must not carry a position"));
    }

    const std::uint32_t capacity = options_.max_queued_events == 0 ? 1u : options_.max_queued_events;
    if (events_.size() >= capacity && options_.event_overflow_policy == AudioEventOverflowPolicy::DropNewest)
    {
        return foundation::Result<void>::Success();
    }
    if (events_.size() >= capacity && options_.event_overflow_policy == AudioEventOverflowPolicy::FailSubmit)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.event_queue_full", "audio event queue is full"));
    }

    try
    {
        std::vector<AudioEvent> staged = events_;
        if (staged.size() >= capacity)
        {
            staged.erase(staged.begin());
        }
        staged.push_back(event);
        events_.swap(staged);
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "audio event queue allocation failed"));
    }
    return foundation::Result<void>::Success();
}

std::span<const AudioEvent> AudioRuntime::Events() const
{
    return events_;
}

void AudioRuntime::Clear()
{
    events_.clear();
}

foundation::Result<void> AudioRuntime::SetMixerGroup(MixerGroupState state)
{
    const auto accepting = EnsureCanStartWork();
    if (!accepting)
    {
        return accepting;
    }
    if (!state.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_group", "mixer group id must be valid"));
    }
    if (!IsValid(state.fade_state))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_fade_state", "mixer fade state is outside the declared enum domain"));
    }
    if (!IsFinite(state.volume) || state.volume < 0.0f || (!options_.allow_mixer_boost && state.volume > 1.0f))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_volume", "mixer group volume is outside the supported range"));
    }
    if (!IsFinite(state.fade_progress) || state.fade_progress < 0.0f || state.fade_progress > 1.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_fade_progress", "mixer fade progress must be finite and within [0, 1]"));
    }
    if (state.fade_duration.value.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_fade_duration", "mixer fade duration must not be negative"));
    }
    if (state.parent.IsValid())
    {
        if (state.parent == state.id)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.mixer_cycle", "mixer group cannot parent itself"));
        }
        if (!mixer_groups_.contains(state.parent))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.mixer_parent_not_found", "mixer group parent must exist"));
        }
        if (MixerWouldCycle(state.id, state.parent))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.mixer_cycle", "mixer group hierarchy must not contain cycles"));
        }
    }

    try
    {
        auto staged_groups = mixer_groups_;
        auto staged_fades = mixer_fades_;
        if (state.fade_state == MixerFadeState::Stable || state.fade_duration.value.count() == 0)
        {
            staged_fades.erase(state.id);
            if (state.fade_state != MixerFadeState::Stable)
            {
                state.fade_state = MixerFadeState::Stable;
                state.fade_progress = 1.0f;
            }
            staged_groups[state.id] = state;
        }
        else
        {
            const float target = state.fade_state == MixerFadeState::FadingOut ? 0.0f : state.volume;
            const float start = state.fade_state == MixerFadeState::FadingIn ? 0.0f : state.volume;
            state.volume = start;
            state.fade_progress = 0.0f;
            staged_groups[state.id] = state;
            staged_fades[state.id] = MixerFadeRecord{start, target, {}};
        }
        mixer_groups_.swap(staged_groups);
        mixer_fades_.swap(staged_fades);
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "mixer state staging allocation failed"));
    }
    return foundation::Result<void>::Success();
}

std::optional<MixerGroupState> AudioRuntime::GetMixerGroup(MixerGroupId id) const
{
    const auto iterator = mixer_groups_.find(id);
    if (iterator == mixer_groups_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

IAudioBackend* AudioRuntime::Backend() const noexcept
{
    return dependencies_.backend != nullptr ? dependencies_.backend.get() : const_cast<AudioRuntime*>(this);
}

IAudioResourceSource* AudioRuntime::Resources() const noexcept
{
    return dependencies_.resources.get();
}

IAudioTransformSource* AudioRuntime::Transforms() const noexcept
{
    return dependencies_.transforms.get();
}

foundation::Result<AudioClipPayload> AudioRuntime::ResolvePayload(SoundId id)
{
    if (Resources() != nullptr)
    {
        const auto loaded = CallAudioBoundary(
            [&] { return Resources()->LoadClip(id); },
            "audio.resource_source_exception",
            "audio resource source threw while loading a clip");
        if (!loaded)
        {
            return loaded;
        }
        if (!IsValid(loaded.Value().state))
        {
            return foundation::Result<AudioClipPayload>::Failure(
                foundation::Error::Create("audio.invalid_sound_state", "audio resource source returned an invalid sound state"));
        }
        if (loaded.Value().state == SoundState::Ready && loaded.Value().resource == nullptr)
        {
            return foundation::Result<AudioClipPayload>::Failure(
                foundation::Error::Create("audio.clip_resource_missing", "ready audio clip payload must provide a resource"));
        }
        return loaded;
    }

    const SoundState state = GetSoundState(id);
    if (state == SoundState::Missing)
    {
        return foundation::Result<AudioClipPayload>::Failure(
            foundation::Error::Create("audio.sound_not_found", "audio clip payload sound was not found"));
    }
    try
    {
        return foundation::Result<AudioClipPayload>::Success(AudioClipPayload{id, state, std::make_shared<MockAudioClipResource>()});
    }
    catch (const std::exception&)
    {
        return foundation::Result<AudioClipPayload>::Failure(
            foundation::Error::Create("audio.allocation_failed", "mock audio clip allocation failed"));
    }
}

foundation::Result<AudioSpatialState> AudioRuntime::ReadSpatialState(AudioTransformId transform_id, bool spatial) const
{
    Transform transform{};
    if (spatial && Transforms() != nullptr)
    {
        const auto read = CallAudioBoundary(
            [&] { return Transforms()->ReadTransform(transform_id); },
            "audio.transform_source_exception",
            "audio transform source threw while reading a spatial transform");
        if (!read)
        {
            return foundation::Result<AudioSpatialState>::Failure(read.GetError());
        }
        transform = read.Value();
        if (!IsValidTransform(transform))
        {
            return foundation::Result<AudioSpatialState>::Failure(
                foundation::Error::Create("audio.invalid_spatial_state", "audio transform source returned a non-finite transform"));
        }
    }
    return foundation::Result<AudioSpatialState>::Success(AudioSpatialState{transform, spatial});
}

foundation::Result<void> AudioRuntime::ApplyEmitterSpatialState(EmitterRecord& emitter)
{
    IAudioBackend* backend = Backend();
    if (backend == nullptr || !emitter.voice.IsValid())
    {
        return foundation::Result<void>::Success();
    }
    const auto spatial = ReadSpatialState(emitter.desc.transform, emitter.desc.spatial);
    if (!spatial)
    {
        return foundation::Result<void>::Failure(spatial.GetError());
    }
    return CallAudioBoundary(
        [&] { return backend->SetSpatialState(emitter.voice, spatial.Value()); },
        "audio.backend_exception",
        "audio backend threw while setting emitter spatial state");
}

foundation::Result<AudioRuntime::VoiceOwnership> AudioRuntime::CreateOneShotVoice(const AudioEvent& event)
{
    const auto payload = ResolvePayload(event.sound);
    if (!payload || payload.Value().state != SoundState::Ready || payload.Value().resource == nullptr)
    {
        return foundation::Result<VoiceOwnership>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "one-shot sound is not ready"));
    }
    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<VoiceOwnership>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required for one-shot playback"));
    }
    AudioSpatialState spatial{};
    spatial.spatial = event.space == AudioEventSpace::WorldPosition;
    spatial.transform.position = event.position;
    if (!IsFiniteSpatial(spatial))
    {
        return foundation::Result<VoiceOwnership>::Failure(
            foundation::Error::Create("audio.invalid_event_position", "one-shot spatial state must be finite"));
    }
    const AudioVoiceDesc voice_desc{payload.Value(), false, {}, event.volume, spatial};
    const auto voice = CallAudioBoundary(
        [&] { return backend->CreateVoice(voice_desc); },
        "audio.backend_exception",
        "audio backend threw while creating a one-shot voice");
    if (!voice)
    {
        return foundation::Result<VoiceOwnership>::Failure(voice.GetError());
    }
    const auto played = CallAudioBoundary(
        [&] { return backend->Play(voice.Value()); },
        "audio.backend_exception",
        "audio backend threw while starting one-shot playback");
    if (!played)
    {
        RollbackCreatedVoice(*backend, voice.Value(), payload.Value().resource);
        return foundation::Result<VoiceOwnership>::Failure(played.GetError());
    }
    return foundation::Result<VoiceOwnership>::Success(VoiceOwnership{voice.Value(), payload.Value().resource});
}

foundation::Result<void> AudioRuntime::CleanupFinishedOneShots()
{
    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Success();
    }

    std::optional<foundation::Error> first_error;
    std::vector<VoiceOwnership> survivors;
    try
    {
        survivors.reserve(one_shot_voices_.size());
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "one-shot cleanup allocation failed"));
    }
    for (auto& owned : one_shot_voices_)
    {
        bool finished = false;
        try
        {
            finished = backend->IsVoiceFinished(owned.voice);
        }
        catch (...)
        {
            if (!first_error.has_value())
            {
                first_error = foundation::Error::Create("audio.backend_exception", "audio backend threw while querying one-shot completion");
            }
            survivors.push_back(std::move(owned));
            continue;
        }
        if (!owned.voice.IsValid() || !finished)
        {
            survivors.push_back(std::move(owned));
            continue;
        }

        const auto destroyed = CallAudioBoundary(
            [&] { return backend->DestroyVoice(owned.voice); },
            "audio.backend_exception",
            "audio backend threw while cleaning a finished one-shot voice");
        if (!destroyed)
        {
            if (!first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            survivors.push_back(std::move(owned));
        }
    }
    one_shot_voices_ = std::move(survivors);
    if (first_error.has_value())
    {
        return foundation::Result<void>::Failure(*first_error);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::CleanupPendingVoices()
{
    IAudioBackend* backend = Backend();
    if (backend == nullptr || pending_voice_cleanups_.empty())
    {
        return foundation::Result<void>::Success();
    }

    std::optional<foundation::Error> first_error;
    std::vector<VoiceOwnership> survivors;
    try
    {
        survivors.reserve(pending_voice_cleanups_.size());
    }
    catch (const std::exception&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.allocation_failed", "pending voice cleanup allocation failed"));
    }
    for (auto& owned : pending_voice_cleanups_)
    {
        const auto destroyed = CallAudioBoundary(
            [&] { return backend->DestroyVoice(owned.voice); },
            "audio.backend_exception",
            "audio backend threw while retrying voice cleanup");
        if (!destroyed)
        {
            if (!first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            survivors.push_back(std::move(owned));
        }
    }
    pending_voice_cleanups_ = std::move(survivors);
    if (first_error.has_value())
    {
        return foundation::Result<void>::Failure(*first_error);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::CleanupPendingListeners()
{
    IAudioBackend* backend = Backend();
    if (backend == nullptr || pending_listener_cleanups_.empty())
    {
        return foundation::Result<void>::Success();
    }

    std::optional<foundation::Error> first_error;
    for (auto iterator = pending_listener_cleanups_.begin(); iterator != pending_listener_cleanups_.end();)
    {
        const AudioListenerHandle handle = *iterator;
        const auto destroyed = CallAudioBoundary(
            [&] { return backend->DestroyBackendListener(handle); },
            "audio.backend_exception",
            "audio backend threw while retrying listener rollback");
        if (destroyed)
        {
            iterator = pending_listener_cleanups_.erase(iterator);
        }
        else
        {
            if (!first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            ++iterator;
        }
    }
    return first_error ? foundation::Result<void>::Failure(*first_error)
                       : foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::EnsureCanStartWork() const
{
    if (shutdown_started_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.shutdown", "audio runtime is shutting down"));
    }
    return foundation::Result<void>::Success();
}

void AudioRuntime::RecordCleanupFailure(const foundation::Error&)
{
    if (cleanup_failures_ != std::numeric_limits<std::uint64_t>::max())
    {
        ++cleanup_failures_;
    }
}

void AudioRuntime::RollbackCreatedVoice(IAudioBackend& backend,
                                        BackendVoiceHandle voice,
                                        std::shared_ptr<const IAudioClipResource> clip_resource)
{
    if (!voice.IsValid())
    {
        return;
    }

    bool can_record_retry = true;
    try
    {
        pending_voice_cleanups_.reserve(pending_voice_cleanups_.size() + 1);
    }
    catch (...)
    {
        can_record_retry = false;
    }

    const auto destroyed = CallAudioBoundary(
        [&] { return backend.DestroyVoice(voice); },
        "audio.backend_exception",
        "audio backend threw while rolling back a created voice");
    if (!destroyed)
    {
        RecordCleanupFailure(destroyed.GetError());
        if (can_record_retry)
        {
            pending_voice_cleanups_.push_back(VoiceOwnership{voice, std::move(clip_resource)});
        }
    }
}

void AudioRuntime::RollbackCreatedListener(IAudioBackend& backend, AudioListenerHandle handle) noexcept
{
    const auto destroyed = CallAudioBoundary(
        [&] { return backend.DestroyBackendListener(handle); },
        "audio.backend_exception",
        "audio backend threw while rolling back a created listener");
    if (!destroyed)
    {
        RecordCleanupFailure(destroyed.GetError());
        pending_listener_cleanups_.push_back(handle);
    }
}

void AudioRuntime::ResetFade(EmitterRecord& emitter)
{
    emitter.fade_duration = {};
    emitter.fade_progress = 0.0f;
    emitter.fade_elapsed = {};
    emitter.fade_multiplier = 1.0f;
    emitter.fade_start_multiplier = 1.0f;
    emitter.fade_target_multiplier = 1.0f;
    emitter.paused_playback_state.reset();
}

foundation::Result<void> AudioRuntime::BeginFade(EmitterRecord& emitter, EmitterState target_state, RuntimeFrameDuration duration)
{
    if (duration.value.count() < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_fade", "fade duration must not be negative"));
    }
    if (target_state == EmitterState::FadingOut && emitter.state != EmitterState::Playing && emitter.state != EmitterState::FadingIn)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_transition", "fade-out requires active playback"));
    }
    if (target_state == EmitterState::FadingIn && emitter.state != EmitterState::Playing && emitter.state != EmitterState::FadingOut)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_transition", "fade-in requires active playback"));
    }
    const auto revision = EnsureEmitterRevisionAvailable(emitter);
    if (!revision)
    {
        return revision;
    }

    EmitterRecord candidate = emitter;
    candidate.state = target_state;
    candidate.fade_duration = duration;
    candidate.fade_elapsed = RuntimeFrameDuration{};
    candidate.fade_start_multiplier = target_state == EmitterState::FadingIn ? 0.0f : candidate.fade_multiplier;
    candidate.fade_target_multiplier = target_state == EmitterState::FadingIn ? 1.0f : 0.0f;
    candidate.fade_multiplier = candidate.fade_start_multiplier;
    candidate.fade_progress = duration.value.count() == 0 ? 1.0f : 0.0f;

    if (candidate.voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        if (backend != nullptr)
        {
            const auto gain = CallAudioBoundary(
                [&] { return backend->SetGain(candidate.voice, EffectiveGain(candidate)); },
                "audio.backend_exception",
                "audio backend threw while applying fade gain");
            if (!gain)
            {
                return foundation::Result<void>::Failure(gain.GetError());
            }
            if (duration.value.count() == 0 && target_state == EmitterState::FadingOut)
            {
                const auto stopped = CallAudioBoundary(
                    [&] { return backend->Stop(candidate.voice); },
                    "audio.backend_exception",
                    "audio backend threw while completing zero-duration fade-out");
                if (!stopped)
                {
                    return foundation::Result<void>::Failure(stopped.GetError());
                }
                candidate.state = EmitterState::Stopped;
                ResetFade(candidate);
            }
            else if (duration.value.count() == 0 && target_state == EmitterState::FadingIn)
            {
                candidate.fade_multiplier = 1.0f;
                const auto full_gain = CallAudioBoundary(
                    [&] { return backend->SetGain(candidate.voice, EffectiveGain(candidate)); },
                    "audio.backend_exception",
                    "audio backend threw while completing zero-duration fade-in");
                if (!full_gain)
                {
                    return foundation::Result<void>::Failure(full_gain.GetError());
                }
                candidate.state = EmitterState::Playing;
                ResetFade(candidate);
            }
        }
    }
    CommitEmitterRevision(candidate);
    emitter = std::move(candidate);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::ApplyMainListenerTransform()
{
    if (!main_listener_.has_value())
    {
        return foundation::Result<void>::Success();
    }
    const ListenerRecord* listener = FindListener(*main_listener_);
    if (listener == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_listener", "main listener handle is stale"));
    }
    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = CallAudioBoundary(
            [&] { return Transforms()->ReadTransform(listener->desc.transform); },
            "audio.transform_source_exception",
            "audio transform source threw while updating the main listener");
        if (!read)
        {
            return foundation::Result<void>::Failure(read.GetError());
        }
        transform = read.Value();
        if (!IsValidTransform(transform))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.invalid_listener_transform", "audio listener transform source returned a non-finite transform"));
        }
    }
    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Success();
    }
    return CallAudioBoundary(
        [&] { return backend->SetBackendListenerTransform(listener->handle, transform); },
        "audio.backend_exception",
        "audio backend threw while applying the main listener transform");
}

float AudioRuntime::EffectiveGain(const EmitterRecord& emitter) const
{
    float gain = emitter.base_gain * emitter.fade_multiplier;
    MixerGroupId current = emitter.desc.mixer_group;
    while (current.IsValid())
    {
        const auto iterator = mixer_groups_.find(current);
        if (iterator == mixer_groups_.end())
        {
            break;
        }
        gain *= iterator->second.volume;
        current = iterator->second.parent;
    }
    return gain;
}

AudioRuntime::EmitterRecord* AudioRuntime::FindEmitter(AudioEmitterId id)
{
    const auto iterator = emitters_.find(id);
    if (iterator == emitters_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const AudioRuntime::EmitterRecord* AudioRuntime::FindEmitter(AudioEmitterId id) const
{
    const auto iterator = emitters_.find(id);
    if (iterator == emitters_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

AudioRuntime::EmitterRecord* AudioRuntime::FindEmitter(AudioEmitterHandle handle)
{
    EmitterRecord* emitter = FindEmitter(handle.id);
    if (emitter == nullptr || emitter->handle.generation != handle.generation)
    {
        return nullptr;
    }

    return emitter;
}

const AudioRuntime::EmitterRecord* AudioRuntime::FindEmitter(AudioEmitterHandle handle) const
{
    const EmitterRecord* emitter = FindEmitter(handle.id);
    if (emitter == nullptr || emitter->handle.generation != handle.generation)
    {
        return nullptr;
    }

    return emitter;
}

bool AudioRuntime::HasListener(AudioListenerId id) const
{
    return listeners_.contains(id);
}

AudioRuntime::ListenerRecord* AudioRuntime::FindListener(AudioListenerId id)
{
    const auto iterator = listeners_.find(id);
    if (iterator == listeners_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

const AudioRuntime::ListenerRecord* AudioRuntime::FindListener(AudioListenerId id) const
{
    const auto iterator = listeners_.find(id);
    if (iterator == listeners_.end())
    {
        return nullptr;
    }
    return &iterator->second;
}

AudioRuntime::ListenerRecord* AudioRuntime::FindListener(AudioListenerHandle handle)
{
    ListenerRecord* listener = FindListener(handle.id);
    if (listener == nullptr || listener->handle.generation != handle.generation)
    {
        return nullptr;
    }
    return listener;
}

const AudioRuntime::ListenerRecord* AudioRuntime::FindListener(AudioListenerHandle handle) const
{
    const ListenerRecord* listener = FindListener(handle.id);
    if (listener == nullptr || listener->handle.generation != handle.generation)
    {
        return nullptr;
    }
    return listener;
}

bool AudioRuntime::MixerWouldCycle(MixerGroupId id, MixerGroupId parent) const
{
    MixerGroupId current = parent;
    while (current.IsValid())
    {
        if (current == id)
        {
            return true;
        }
        const auto iterator = mixer_groups_.find(current);
        if (iterator == mixer_groups_.end())
        {
            return false;
        }
        current = iterator->second.parent;
    }
    return false;
}

bool AudioRuntime::IsFinite(float value) const noexcept
{
    return std::isfinite(value);
}

foundation::Result<void> AudioRuntime::EnsureEmitterRevisionAvailable(const EmitterRecord& emitter) const
{
    if (emitter.revision == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.revision_overflow", "audio emitter revision is exhausted"));
    }
    return foundation::Result<void>::Success();
}

void AudioRuntime::CommitEmitterRevision(EmitterRecord& emitter) noexcept
{
    ++emitter.revision;
}

foundation::Result<void> AudioRuntime::SetEmitterRevisionForTesting(AudioEmitterHandle handle, std::uint64_t revision)
{
    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_emitter", "audio emitter handle is invalid or stale"));
    }
    emitter->revision = revision;
    return foundation::Result<void>::Success();
}

void AudioRuntime::SetAllocatorStateForTesting(std::uint64_t emitter_value,
                                               std::uint32_t emitter_generation,
                                               std::uint64_t listener_value,
                                               std::uint32_t listener_generation,
                                               std::uint64_t voice_value)
{
    next_emitter_value_ = emitter_value;
    next_emitter_generation_ = emitter_generation;
    next_listener_value_ = listener_value;
    next_listener_generation_ = listener_generation;
    next_voice_value_ = voice_value;
}
} // namespace epidemic::runtime::audio
