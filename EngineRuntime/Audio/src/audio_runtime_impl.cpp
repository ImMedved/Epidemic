#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace epidemic::runtime::audio
{
AudioRuntime::AudioRuntime(AudioOptions options, AudioDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
}

foundation::Result<void> AudioRuntime::RegisterSound(SoundDesc desc)
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_sound", "sound id must be valid before registration"));
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
    options_.enable_mock_backend = options.enabled;
    return foundation::Result<void>::Success();
}

foundation::Result<BackendVoiceHandle> AudioRuntime::CreateVoice(const AudioClipPayload& payload)
{
    if (!options_.enable_mock_backend)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.backend_disabled", "mock audio backend is disabled"));
    }
    if (!payload.sound.IsValid() || payload.state != SoundState::Ready)
    {
        return foundation::Result<BackendVoiceHandle>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio clip payload is not ready"));
    }

    const BackendVoiceHandle handle{next_voice_value_++};
    voices_.emplace(handle, MockVoiceRecord{payload});
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

foundation::Result<void> AudioRuntime::Update(GameDuration)
{
    return foundation::Result<void>::Success();
}

foundation::Result<AudioEmitterId> AudioRuntime::CreateEmitter(const AudioEmitterDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<AudioEmitterId>::Failure(
            foundation::Error::Create("audio.invalid_owner", "audio emitter owner must be valid before creation"));
    }

    if (!desc.sound.IsValid() || GetSoundState(desc.sound) == SoundState::Missing)
    {
        return foundation::Result<AudioEmitterId>::Failure(
            foundation::Error::Create("audio.sound_not_found", "audio emitter must reference a registered sound"));
    }

    if (!desc.transform.IsValid())
    {
        return foundation::Result<AudioEmitterId>::Failure(
            foundation::Error::Create("audio.invalid_transform", "audio emitter must reference a valid scene node"));
    }

    const AudioEmitterId id{next_emitter_value_++};
    const AudioEmitterHandle handle{id, next_emitter_generation_++};
    emitters_.emplace(id, EmitterRecord{desc, handle, {}, EmitterState::Stopped, {}, 0.0f, 1.0f, 1.0f, 1.0f, {}, 1});
    return foundation::Result<AudioEmitterId>::Success(id);
}

foundation::Result<AudioEmitterHandle> AudioRuntime::CreateEmitterHandle(const AudioEmitterDesc& desc)
{
    const auto id = CreateEmitter(desc);
    if (!id)
    {
        return foundation::Result<AudioEmitterHandle>::Failure(id.GetError());
    }

    const EmitterRecord* emitter = FindEmitter(id.Value());
    return foundation::Result<AudioEmitterHandle>::Success(emitter->handle);
}

foundation::Result<void> AudioRuntime::DestroyEmitter(AudioEmitterId id)
{
    EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.emitter_not_found", "audio emitter was not found for destruction"));
    }

    if (emitter->voice.IsValid())
    {
        IAudioBackend* backend = Backend();
        if (backend != nullptr)
        {
            (void)backend->DestroyVoice(emitter->voice);
        }
    }
    emitters_.erase(id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Play(AudioEmitterId id)
{
    EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.emitter_not_found", "audio emitter was not found for playback"));
    }

    const auto payload = ResolvePayload(emitter->desc.sound);
    if (!payload || payload.Value().state != SoundState::Ready)
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

    if (!emitter->voice.IsValid())
    {
        const auto voice = backend->CreateVoice(payload.Value());
        if (!voice)
        {
            return foundation::Result<void>::Failure(voice.GetError());
        }
        emitter->voice = voice.Value();
    }

    const auto spatial = ApplyEmitterSpatialState(*emitter);
    if (!spatial)
    {
        return spatial;
    }

    const auto played = backend->Play(emitter->voice);
    if (!played)
    {
        return foundation::Result<void>::Failure(played.GetError());
    }

    if (emitter->state != EmitterState::Playing)
    {
        emitter->state = EmitterState::Playing;
        ++emitter->revision;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Stop(AudioEmitterId id)
{
    EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.emitter_not_found", "audio emitter was not found for stop"));
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
        ++emitter->revision;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::FadeOut(AudioEmitterId id)
{
    EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.emitter_not_found", "audio emitter was not found for fade"));
    }

    if (emitter->state != EmitterState::FadingOut)
    {
        emitter->state = EmitterState::FadingOut;
        emitter->fade_duration = GameDuration{};
        emitter->fade_elapsed = GameDuration{};
        emitter->fade_start_gain = emitter->gain;
        emitter->fade_target_gain = 0.0f;
        emitter->fade_progress = 0.0f;
        ++emitter->revision;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::FadeOut(AudioEmitterHandle handle, GameDuration duration)
{
    if (duration.ticks < 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_fade", "fade duration must not be negative"));
    }

    EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_emitter", "audio emitter handle is stale"));
    }

    emitter->state = EmitterState::FadingOut;
    emitter->fade_duration = duration;
    emitter->fade_elapsed = GameDuration{};
    emitter->fade_start_gain = emitter->gain;
    emitter->fade_target_gain = 0.0f;
    emitter->fade_progress = duration.ticks == 0 ? 1.0f : 0.0f;
    ++emitter->revision;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::Virtualize(AudioEmitterId id)
{
    EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.emitter_not_found", "audio emitter was not found for virtualization"));
    }

    if (emitter->state != EmitterState::Virtualized)
    {
        emitter->state = EmitterState::Virtualized;
        ++emitter->revision;
    }
    return foundation::Result<void>::Success();
}

EmitterState AudioRuntime::GetEmitterState(AudioEmitterId id) const
{
    const EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return EmitterState::Destroyed;
    }

    return emitter->state;
}

AudioEmitterSnapshot AudioRuntime::GetEmitterSnapshot(AudioEmitterId id) const
{
    const EmitterRecord* emitter = FindEmitter(id);
    if (emitter == nullptr)
    {
        return AudioEmitterSnapshot{id, {}, {}, {}, EmitterState::Destroyed, {}, 0.0f, 0};
    }

    return AudioEmitterSnapshot{
        id,
        emitter->handle,
        emitter->desc.sound,
        emitter->desc.transform,
        emitter->state,
        emitter->fade_duration,
        emitter->fade_progress,
        emitter->revision,
    };
}

AudioEmitterSnapshot AudioRuntime::GetEmitterSnapshot(AudioEmitterHandle handle) const
{
    const EmitterRecord* emitter = FindEmitter(handle);
    if (emitter == nullptr)
    {
        return AudioEmitterSnapshot{handle.id, handle, {}, {}, EmitterState::Destroyed, {}, 0.0f, 0};
    }

    return GetEmitterSnapshot(handle.id);
}

foundation::Result<void> AudioRuntime::Tick(GameDuration delta)
{
    if (delta.ticks < 0)
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

    for (auto& [id, emitter] : emitters_)
    {
        (void)id;
        if (emitter.voice.IsValid())
        {
            const auto spatial = ApplyEmitterSpatialState(emitter);
            if (!spatial)
            {
                return spatial;
            }
        }

        if (emitter.state == EmitterState::FadingOut)
        {
            emitter.fade_elapsed.ticks += delta.ticks;
            const auto duration = std::max<std::int64_t>(1, emitter.fade_duration.ticks);
            emitter.fade_progress = std::clamp(
                static_cast<float>(emitter.fade_elapsed.ticks) / static_cast<float>(duration),
                0.0f,
                1.0f);
            emitter.gain = emitter.fade_start_gain + ((emitter.fade_target_gain - emitter.fade_start_gain) * emitter.fade_progress);
            if (emitter.voice.IsValid())
            {
                const auto gain = backend->SetGain(emitter.voice, emitter.gain);
                if (!gain)
                {
                    return foundation::Result<void>::Failure(gain.GetError());
                }
            }
            if (emitter.fade_progress >= 1.0f)
            {
                emitter.state = EmitterState::Stopped;
            }
            ++emitter.revision;
        }
    }

    const auto updated = backend->Update(delta);
    if (!updated)
    {
        return foundation::Result<void>::Failure(updated.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<AudioListenerId> AudioRuntime::CreateListener(const AudioListenerDesc& desc)
{
    if (!desc.transform.IsValid())
    {
        return foundation::Result<AudioListenerId>::Failure(
            foundation::Error::Create("audio.invalid_listener_transform", "audio listener must reference a valid scene node"));
    }

    const AudioListenerId id{next_listener_value_++};
    const AudioListenerHandle handle{id, next_listener_generation_++};
    listeners_.emplace(id, ListenerRecord{desc, handle});
    return foundation::Result<AudioListenerId>::Success(id);
}

foundation::Result<AudioListenerHandle> AudioRuntime::CreateListenerHandle(const AudioListenerDesc& desc)
{
    const auto id = CreateListener(desc);
    if (!id)
    {
        return foundation::Result<AudioListenerHandle>::Failure(id.GetError());
    }

    const ListenerRecord* listener = FindListener(id.Value());
    return foundation::Result<AudioListenerHandle>::Success(listener->handle);
}

foundation::Result<void> AudioRuntime::SetMainListener(AudioListenerId id)
{
    if (!HasListener(id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.listener_not_found", "audio listener was not found for main listener selection"));
    }

    main_listener_ = id;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::SetMainListener(AudioListenerHandle handle)
{
    if (FindListener(handle) == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_listener", "audio listener handle is stale"));
    }
    main_listener_ = handle.id;
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::DestroyListener(AudioListenerId id)
{
    const auto erased = listeners_.erase(id);
    if (erased == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.listener_not_found", "audio listener was not found for destruction"));
    }

    if (main_listener_ == id)
    {
        main_listener_.reset();
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AudioRuntime::DestroyListener(AudioListenerHandle handle)
{
    if (FindListener(handle) == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.stale_listener", "audio listener handle is stale"));
    }
    return DestroyListener(handle.id);
}

std::optional<AudioListenerId> AudioRuntime::GetMainListener() const
{
    return main_listener_;
}

foundation::Result<void> AudioRuntime::SubmitOneShot(const AudioEvent& event)
{
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
        return Resources()->LoadClip(id);
    }

    const SoundState state = GetSoundState(id);
    if (state == SoundState::Missing)
    {
        return foundation::Result<AudioClipPayload>::Failure(
            foundation::Error::Create("audio.sound_not_found", "audio clip payload sound was not found"));
    }
    return foundation::Result<AudioClipPayload>::Success(AudioClipPayload{id, state});
}

foundation::Result<void> AudioRuntime::ApplyEmitterSpatialState(EmitterRecord& emitter)
{
    IAudioBackend* backend = Backend();
    if (backend == nullptr || !emitter.voice.IsValid())
    {
        return foundation::Result<void>::Success();
    }

    Transform transform{};
    if (Transforms() != nullptr)
    {
        const auto read = Transforms()->ReadTransform(emitter.desc.transform);
        if (!read)
        {
            return foundation::Result<void>::Failure(read.GetError());
        }
        transform = read.Value();
    }
    return backend->SetSpatialState(emitter.voice, AudioSpatialState{transform, true});
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
} // namespace epidemic::runtime::audio
