#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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
    if (sounds_.contains(desc.id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_already_registered", "sound id is already registered"));
    }

    sounds_[desc.id] = desc;
    return foundation::Result<void>::Success();
}

SoundState AudioRuntime::GetSoundState(SoundId id) const
{
    if (dependencies_.resources != nullptr)
    {
        const SoundState state = dependencies_.resources->GetSoundState(id);
        if (state != SoundState::Missing)
        {
            return state;
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

    const auto voice_value = AllocateMonotonicId(
        next_voice_value_,
        "audio.voice_id_overflow",
        "mock backend voice id allocator is exhausted");
    if (!voice_value)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(voice_value.GetError());
    }
    const BackendVoiceHandle handle{voice_value.Value()};
    const auto [voice_iterator, inserted] = voices_.emplace(handle, MockVoiceRecord{desc, EmitterState::Stopped, desc.initial_gain, desc.spatial});
    if (!inserted)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.duplicate_voice_id", "allocated backend voice id already exists"));
    }
    (void)voice_iterator;
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
    iterator->second.spatial = state;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::CreateBackendListener(AudioListenerHandle handle, const Transform&)
{
    if (!handle.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener", "backend listener handle must be valid"));
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

foundation::Result<void> AudioRuntime::SetBackendListenerTransform(AudioListenerHandle handle, const Transform&)
{
    if (!handle.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_listener", "backend listener handle must be valid"));
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

    if (!CanAllocateMonotonicId(next_emitter_value_) || !CanAllocateMonotonicId(next_emitter_generation_))
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.emitter_id_overflow", "audio emitter id allocator is exhausted"));
    }
    const auto emitter_value = AllocateMonotonicId(next_emitter_value_, "audio.emitter_id_overflow", "audio emitter id allocator is exhausted");
    const auto emitter_generation = AllocateMonotonicId(next_emitter_generation_, "audio.emitter_id_overflow", "audio emitter generation allocator is exhausted");
    if (!emitter_value || !emitter_generation)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.emitter_id_overflow", "audio emitter id allocator is exhausted"));
    }
    const AudioEmitterId id{emitter_value.Value()};
    const AudioEmitterHandle handle{id, emitter_generation.Value()};
    EmitterRecord record{};
    record.desc = desc;
    record.handle = handle;
    record.state = EmitterState::Stopped;
    record.base_gain = desc.gain;
    record.fade_multiplier = 1.0f;
    record.fade_start_multiplier = 1.0f;
    record.fade_target_multiplier = 1.0f;
    record.revision = 1;
    const auto [emitter_iterator, inserted] = emitters_.emplace(id, record);
    if (!inserted)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(
            foundation::Error::Create("audio.duplicate_emitter_id", "allocated audio emitter id already exists"));
    }
    (void)emitter_iterator;
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
            const auto destroyed = backend->DestroyVoice(emitter->voice);
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

    const auto payload = ResolvePayload(emitter->desc.sound);
    if (!payload || payload.Value().state != SoundState::Ready || payload.Value().resource == nullptr)
    {
        if (emitter->state != EmitterState::Stopped)
        {
            emitter->state = EmitterState::Stopped;
            ++emitter->revision;
        }
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio emitter sound is not ready"));
    }

    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required for playback"));
    }

    ResetFade(*emitter);
    emitter->fade_multiplier = 1.0f;

    const auto spatial_state = ReadSpatialState(emitter->desc.transform, emitter->desc.spatial);
    if (!spatial_state)
    {
        return foundation::Result<void>::Failure(spatial_state.GetError());
    }

    bool created_voice = false;
    if (!emitter->voice.IsValid())
    {
        const AudioVoiceDesc voice_desc{
            payload.Value(),
            emitter->desc.loop,
            emitter->desc.mixer_group,
            EffectiveGain(*emitter),
            spatial_state.Value(),
        };
        const auto voice = backend->CreateVoice(voice_desc);
        if (!voice)
        {
            return foundation::Result<void>::Failure(voice.GetError());
        }
        emitter->voice = voice.Value();
        emitter->clip_resource = payload.Value().resource;
        created_voice = true;
    }

    const auto spatial = backend->SetSpatialState(emitter->voice, spatial_state.Value());
    if (!spatial)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, emitter->voice);
            emitter->voice = {};
        }
        return spatial;
    }

    const auto gain = backend->SetGain(emitter->voice, EffectiveGain(*emitter));
    if (!gain)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, emitter->voice);
            emitter->voice = {};
        }
        return foundation::Result<void>::Failure(gain.GetError());
    }

    const auto played = backend->Play(emitter->voice);
    if (!played)
    {
        if (created_voice)
        {
            RollbackCreatedVoice(*backend, emitter->voice);
            emitter->voice = {};
        }
        return foundation::Result<void>::Failure(played.GetError());
    }

    if (emitter->state != EmitterState::Playing)
    {
        emitter->state = EmitterState::Playing;
        ++emitter->revision;
    }
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

    if (emitter->state != EmitterState::Stopped)
    {
        if (emitter->voice.IsValid())
        {
            const auto stopped = Backend()->Stop(emitter->voice);
            if (!stopped)
            {
                return foundation::Result<void>::Failure(stopped.GetError());
            }
        }
        emitter->state = EmitterState::Stopped;
        ResetFade(*emitter);
        ++emitter->revision;
    }
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
    if (emitter->voice.IsValid())
    {
        const auto paused = Backend()->Pause(emitter->voice);
        if (!paused)
        {
            return foundation::Result<void>::Failure(paused.GetError());
        }
    }
    const EmitterState paused_state = emitter->state;
    emitter->state = EmitterState::Paused;
    emitter->paused_playback_state = paused_state;
    ++emitter->revision;
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
    if (emitter->voice.IsValid())
    {
        const auto played = Backend()->Play(emitter->voice);
        if (!played)
        {
            return foundation::Result<void>::Failure(played.GetError());
        }
    }
    emitter->state = emitter->paused_playback_state.value_or(EmitterState::Playing);
    emitter->paused_playback_state.reset();
    ++emitter->revision;
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

    return BeginFade(*emitter, EmitterState::FadingOut, duration);
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

    if (emitter->state == EmitterState::Stopped || emitter->state == EmitterState::Virtualized)
    {
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

        bool created_voice = false;
        if (!emitter->voice.IsValid())
        {
            const AudioVoiceDesc voice_desc{payload.Value(), emitter->desc.loop, emitter->desc.mixer_group, 0.0f, spatial_state.Value()};
            const auto voice = backend->CreateVoice(voice_desc);
            if (!voice)
            {
                return foundation::Result<void>::Failure(voice.GetError());
            }
            emitter->voice = voice.Value();
            emitter->clip_resource = payload.Value().resource;
            created_voice = true;
        }
        emitter->fade_multiplier = 0.0f;
        emitter->fade_start_multiplier = 0.0f;
        emitter->fade_target_multiplier = 1.0f;
        const auto spatial = backend->SetSpatialState(emitter->voice, spatial_state.Value());
        if (!spatial)
        {
            if (created_voice)
            {
                RollbackCreatedVoice(*backend, emitter->voice, emitter->clip_resource);
                emitter->voice = {};
                emitter->clip_resource.reset();
            }
            return spatial;
        }
        const auto gain = backend->SetGain(emitter->voice, 0.0f);
        if (!gain)
        {
            if (created_voice)
            {
                RollbackCreatedVoice(*backend, emitter->voice, emitter->clip_resource);
                emitter->voice = {};
                emitter->clip_resource.reset();
            }
            return foundation::Result<void>::Failure(gain.GetError());
        }
        const auto played = backend->Play(emitter->voice);
        if (!played)
        {
            if (created_voice)
            {
                RollbackCreatedVoice(*backend, emitter->voice, emitter->clip_resource);
                emitter->voice = {};
                emitter->clip_resource.reset();
            }
            return foundation::Result<void>::Failure(played.GetError());
        }
        emitter->state = EmitterState::Playing;
    }
    emitter = FindEmitter(handle);
    return BeginFade(*emitter, EmitterState::FadingIn, duration);
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

    if (emitter->state != EmitterState::Virtualized)
    {
        if (emitter->voice.IsValid())
        {
            IAudioBackend* backend = Backend();
            if (backend != nullptr)
            {
                const auto destroyed = backend->DestroyVoice(emitter->voice);
                if (!destroyed)
                {
                    return foundation::Result<void>::Failure(destroyed.GetError());
                }
            }
            emitter->voice = {};
            emitter->clip_resource.reset();
        }
        emitter->state = EmitterState::Virtualized;
        ResetFade(*emitter);
        ++emitter->revision;
    }
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

    const auto pending_cleanup = CleanupPendingVoices();
    if (!pending_cleanup)
    {
        return pending_cleanup;
    }

    std::size_t processed_events = 0;
    for (const AudioEvent& event : events_)
    {
        const auto processed = ProcessOneShot(event);
        if (!processed)
        {
            events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(processed_events));
            return processed;
        }
        ++processed_events;
    }
    events_.clear();

    const auto one_shots = CleanupFinishedOneShots();
    if (!one_shots)
    {
        return one_shots;
    }

    const auto listener = ApplyMainListenerTransform();
    if (!listener)
    {
        return listener;
    }

    AdvanceMixerFades(delta);

    std::vector<AudioEmitterId> emitter_ids;
    emitter_ids.reserve(emitters_.size());
    for (const auto& [id, emitter] : emitters_)
    {
        (void)emitter;
        emitter_ids.push_back(id);
    }
    std::sort(emitter_ids.begin(), emitter_ids.end(), [](AudioEmitterId left, AudioEmitterId right) {
        return left.value < right.value;
    });

    for (AudioEmitterId id : emitter_ids)
    {
        EmitterRecord* emitter_record = FindEmitter(id);
        if (emitter_record == nullptr)
        {
            continue;
        }
        EmitterRecord& emitter = *emitter_record;
        if (emitter.voice.IsValid())
        {
            const auto spatial = ApplyEmitterSpatialState(emitter);
            if (!spatial)
            {
                return spatial;
            }
        }

        if (emitter.state == EmitterState::FadingOut || emitter.state == EmitterState::FadingIn)
        {
            emitter.fade_elapsed.value += delta.value;
            const auto duration = std::max<std::int64_t>(1, emitter.fade_duration.value.count());
            emitter.fade_progress = std::clamp(
                static_cast<float>(emitter.fade_elapsed.value.count()) / static_cast<float>(duration),
                0.0f,
                1.0f);
            emitter.fade_multiplier = emitter.fade_start_multiplier + ((emitter.fade_target_multiplier - emitter.fade_start_multiplier) * emitter.fade_progress);
            if (emitter.voice.IsValid())
            {
                const auto gain = backend->SetGain(emitter.voice, EffectiveGain(emitter));
                if (!gain)
                {
                    return foundation::Result<void>::Failure(gain.GetError());
                }
            }
            if (emitter.fade_progress >= 1.0f)
            {
                if (emitter.state == EmitterState::FadingOut && emitter.voice.IsValid())
                {
                    const auto stopped = backend->Stop(emitter.voice);
                    if (!stopped)
                    {
                        return foundation::Result<void>::Failure(stopped.GetError());
                    }
                }
                emitter.state = emitter.state == EmitterState::FadingOut ? EmitterState::Stopped : EmitterState::Playing;
                ResetFade(emitter);
            }
            ++emitter.revision;
        }
        else if (emitter.voice.IsValid())
        {
            const auto gain = backend->SetGain(emitter.voice, EffectiveGain(emitter));
            if (!gain)
            {
                return foundation::Result<void>::Failure(gain.GetError());
            }
        }
    }

    const auto updated = backend->Update(delta);
    if (!updated)
    {
        return foundation::Result<void>::Failure(updated.GetError());
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

        for (auto& [id, emitter] : emitters_)
        {
            (void)id;
            if (!emitter.voice.IsValid())
            {
                continue;
            }
            const auto destroyed = backend->DestroyVoice(emitter.voice);
            if (!destroyed && !first_error.has_value())
            {
                first_error = destroyed.GetError();
            }
            if (destroyed)
            {
                emitter.voice = {};
                emitter.clip_resource.reset();
                emitter.state = EmitterState::Stopped;
                ++emitter.revision;
            }
        }

        for (auto iterator = listeners_.begin(); iterator != listeners_.end();)
        {
            const AudioListenerHandle handle = iterator->second.handle;
            const auto destroyed = backend->DestroyBackendListener(handle);
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
        survivors.reserve(one_shot_voices_.size());
        for (auto& owned : one_shot_voices_)
        {
            if (!owned.voice.IsValid())
            {
                continue;
            }
            const auto destroyed = backend->DestroyVoice(owned.voice);
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

    if (!CanAllocateMonotonicId(next_listener_value_) || !CanAllocateMonotonicId(next_listener_generation_))
    {
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.listener_id_overflow", "audio listener id allocator is exhausted"));
    }
    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = Transforms()->ReadTransform(desc.transform);
        if (!read)
        {
            return foundation::Result<AudioListenerHandle>::Failure(read.GetError());
        }
        transform = read.Value();
    }

    const auto listener_value = AllocateMonotonicId(next_listener_value_, "audio.listener_id_overflow", "audio listener id allocator is exhausted");
    const auto listener_generation = AllocateMonotonicId(next_listener_generation_, "audio.listener_id_overflow", "audio listener generation allocator is exhausted");
    if (!listener_value || !listener_generation)
    {
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.listener_id_overflow", "audio listener id allocator is exhausted"));
    }
    const AudioListenerId id{listener_value.Value()};
    const AudioListenerHandle handle{id, listener_generation.Value()};

    IAudioBackend* backend = Backend();
    if (backend != nullptr)
    {
        const auto created = backend->CreateBackendListener(handle, transform);
        if (!created)
        {
            return foundation::Result<AudioListenerHandle>::Failure(created.GetError());
        }
    }

    const auto [listener_iterator, inserted] = listeners_.emplace(id, ListenerRecord{desc, handle});
    if (!inserted)
    {
        if (backend != nullptr)
        {
            (void)backend->DestroyBackendListener(handle);
        }
        return foundation::Result<AudioListenerHandle>::Failure(
            foundation::Error::Create("audio.duplicate_listener_id", "allocated audio listener id already exists"));
    }
    (void)listener_iterator;
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
        const auto read = Transforms()->ReadTransform(listener->desc.transform);
        if (!read)
        {
            return foundation::Result<void>::Failure(read.GetError());
        }
        transform = read.Value();
    }
    IAudioBackend* backend = Backend();
    if (backend != nullptr)
    {
        const auto applied = backend->SetBackendListenerTransform(listener->handle, transform);
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
        const auto destroyed = backend->DestroyBackendListener(handle);
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
    if (GetSoundState(event.sound) != SoundState::Ready)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "one-shot sound is not ready"));
    }

    if (!IsFinite(event.volume) || event.volume < 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_volume", "one-shot volume must not be negative"));
    }
    if (event.space == AudioEventSpace::NonSpatial && event.position != Vec3{})
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.contradictory_event_source", "non-spatial audio event must not carry a position"));
    }

    const std::uint32_t capacity = options_.max_queued_events == 0 ? 1u : options_.max_queued_events;
    if (events_.size() >= capacity)
    {
        switch (options_.event_overflow_policy)
        {
        case AudioEventOverflowPolicy::DropNewest:
            return foundation::Result<void>::Success();
        case AudioEventOverflowPolicy::DropOldest:
            events_.erase(events_.begin());
            break;
        case AudioEventOverflowPolicy::FailSubmit:
            return foundation::Result<void>::Failure(
                foundation::Error::Create("audio.event_queue_full", "audio event queue is full"));
        }
    }

    events_.push_back(event);
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

    if (!IsFinite(state.volume) || state.volume < 0.0f || (!options_.allow_mixer_boost && state.volume > 1.0f))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_volume", "mixer group volume is outside the supported range"));
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

    mixer_groups_[state.id] = state;
    if (state.fade_state == MixerFadeState::Stable || state.fade_duration.value.count() <= 0)
    {
        mixer_fades_.erase(state.id);
        mixer_groups_[state.id].fade_progress = state.fade_state == MixerFadeState::Stable ? state.fade_progress : 1.0f;
        if (state.fade_state != MixerFadeState::Stable)
        {
            mixer_groups_[state.id].fade_state = MixerFadeState::Stable;
        }
    }
    else
    {
        const float target = state.fade_state == MixerFadeState::FadingOut ? 0.0f : state.volume;
        const float start = state.fade_state == MixerFadeState::FadingIn ? 0.0f : state.volume;
        mixer_groups_[state.id].volume = start;
        mixer_groups_[state.id].fade_progress = 0.0f;
        mixer_fades_[state.id] = MixerFadeRecord{start, target, {}};
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
        const auto loaded = Resources()->LoadClip(id);
        if (!loaded)
        {
            return loaded;
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
    return foundation::Result<AudioClipPayload>::Success(AudioClipPayload{id, state, std::make_shared<MockAudioClipResource>()});
}

foundation::Result<AudioSpatialState> AudioRuntime::ReadSpatialState(AudioTransformId transform_id, bool spatial) const
{
    Transform transform{};
    if (spatial && Transforms() != nullptr)
    {
        const auto read = Transforms()->ReadTransform(transform_id);
        if (!read)
        {
            return foundation::Result<AudioSpatialState>::Failure(read.GetError());
        }
        transform = read.Value();
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
    return backend->SetSpatialState(emitter.voice, spatial.Value());
}

foundation::Result<void> AudioRuntime::ProcessOneShot(const AudioEvent& event)
{
    const auto payload = ResolvePayload(event.sound);
    if (!payload || payload.Value().state != SoundState::Ready || payload.Value().resource == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "one-shot sound is not ready"));
    }

    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required for one-shot playback"));
    }

    AudioSpatialState spatial{};
    spatial.spatial = event.space == AudioEventSpace::WorldPosition;
    spatial.transform.position = event.position;
    const AudioVoiceDesc voice_desc{payload.Value(), false, {}, event.volume, spatial};
    const auto voice = backend->CreateVoice(voice_desc);
    if (!voice)
    {
        return foundation::Result<void>::Failure(voice.GetError());
    }

    const auto played = backend->Play(voice.Value());
    if (!played)
    {
        RollbackCreatedVoice(*backend, voice.Value(), payload.Value().resource);
        return foundation::Result<void>::Failure(played.GetError());
    }
    one_shot_voices_.push_back(VoiceOwnership{voice.Value(), payload.Value().resource});
    return foundation::Result<void>::Success();
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
    survivors.reserve(one_shot_voices_.size());
    for (auto& owned : one_shot_voices_)
    {
        if (!owned.voice.IsValid() || !backend->IsVoiceFinished(owned.voice))
        {
            survivors.push_back(std::move(owned));
            continue;
        }

        const auto destroyed = backend->DestroyVoice(owned.voice);
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
    survivors.reserve(pending_voice_cleanups_.size());
    for (auto& owned : pending_voice_cleanups_)
    {
        const auto destroyed = backend->DestroyVoice(owned.voice);
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
    ++cleanup_failures_;
}

void AudioRuntime::RollbackCreatedVoice(IAudioBackend& backend,
                                        BackendVoiceHandle voice,
                                        std::shared_ptr<const IAudioClipResource> clip_resource)
{
    if (!voice.IsValid())
    {
        return;
    }

    const auto destroyed = backend.DestroyVoice(voice);
    if (!destroyed)
    {
        RecordCleanupFailure(destroyed.GetError());
        pending_voice_cleanups_.push_back(VoiceOwnership{voice, std::move(clip_resource)});
    }
}

void AudioRuntime::AdvanceMixerFades(RuntimeFrameDuration delta)
{
    if (mixer_fades_.empty())
    {
        return;
    }

    std::vector<MixerGroupId> ids;
    ids.reserve(mixer_fades_.size());
    for (const auto& [id, fade] : mixer_fades_)
    {
        (void)fade;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](MixerGroupId left, MixerGroupId right) {
        return left.value < right.value;
    });

    for (MixerGroupId id : ids)
    {
        auto state_iterator = mixer_groups_.find(id);
        auto fade_iterator = mixer_fades_.find(id);
        if (state_iterator == mixer_groups_.end() || fade_iterator == mixer_fades_.end())
        {
            continue;
        }

        MixerGroupState& state = state_iterator->second;
        MixerFadeRecord& fade = fade_iterator->second;
        fade.elapsed.value += delta.value;
        const auto duration = std::max<std::int64_t>(1, state.fade_duration.value.count());
        state.fade_progress = std::clamp(
            static_cast<float>(fade.elapsed.value.count()) / static_cast<float>(duration),
            0.0f,
            1.0f);
        state.volume = fade.start_volume + ((fade.target_volume - fade.start_volume) * state.fade_progress);
        if (state.fade_progress >= 1.0f)
        {
            state.volume = fade.target_volume;
            state.fade_state = MixerFadeState::Stable;
            mixer_fades_.erase(id);
        }
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

    emitter.state = target_state;
    emitter.fade_duration = duration;
    emitter.fade_elapsed = RuntimeFrameDuration{};
    emitter.fade_start_multiplier = target_state == EmitterState::FadingIn ? 0.0f : emitter.fade_multiplier;
    emitter.fade_target_multiplier = target_state == EmitterState::FadingIn ? 1.0f : 0.0f;
    emitter.fade_multiplier = emitter.fade_start_multiplier;
    emitter.fade_progress = duration.value.count() == 0 ? 1.0f : 0.0f;
    if (emitter.voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        if (backend != nullptr)
        {
            const auto gain = backend->SetGain(emitter.voice, EffectiveGain(emitter));
            if (!gain)
            {
                return foundation::Result<void>::Failure(gain.GetError());
            }
            if (duration.value.count() == 0 && target_state == EmitterState::FadingOut)
            {
                const auto stopped = backend->Stop(emitter.voice);
                if (!stopped)
                {
                    return foundation::Result<void>::Failure(stopped.GetError());
                }
                emitter.state = EmitterState::Stopped;
                ResetFade(emitter);
            }
            else if (duration.value.count() == 0 && target_state == EmitterState::FadingIn)
            {
                emitter.fade_multiplier = 1.0f;
                const auto full_gain = backend->SetGain(emitter.voice, EffectiveGain(emitter));
                if (!full_gain)
                {
                    return foundation::Result<void>::Failure(full_gain.GetError());
                }
                emitter.state = EmitterState::Playing;
                ResetFade(emitter);
            }
        }
    }
    ++emitter.revision;
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
        main_listener_.reset();
        return foundation::Result<void>::Success();
    }

    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = Transforms()->ReadTransform(listener->desc.transform);
        if (!read)
        {
            return foundation::Result<void>::Failure(read.GetError());
        }
        transform = read.Value();
    }

    IAudioBackend* backend = Backend();
    if (backend == nullptr)
    {
        return foundation::Result<void>::Success();
    }
    return backend->SetBackendListenerTransform(listener->handle, transform);
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
