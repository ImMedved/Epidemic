#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime::audio
{
AudioRuntime::AudioRuntime(AudioOptions options) : options_(options)
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
    emitters_.emplace(id, EmitterRecord{desc, handle, EmitterState::Stopped, {}, 0.0f, 1});
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

    if (GetSoundState(emitter->desc.sound) != SoundState::Ready)
    {
        if (emitter->state != EmitterState::Stopped)
        {
            emitter->state = EmitterState::Stopped;
            ++emitter->revision;
        }
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio emitter sound is not ready"));
    }

    if (!options_.enable_mock_backend)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_disabled", "mock audio backend is disabled"));
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

foundation::Result<AudioListenerId> AudioRuntime::CreateListener(const AudioListenerDesc& desc)
{
    if (!desc.transform.IsValid())
    {
        return foundation::Result<AudioListenerId>::Failure(
            foundation::Error::Create("audio.invalid_listener_transform", "audio listener must reference a valid scene node"));
    }

    const AudioListenerId id{next_listener_value_++};
    listeners_.emplace(id, ListenerRecord{desc});
    return foundation::Result<AudioListenerId>::Success(id);
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

    if (event.volume < 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_volume", "one-shot volume must not be negative"));
    }
    if (options_.max_queued_events != 0 && events_.size() >= options_.max_queued_events)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.event_queue_full", "audio event queue is full"));
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

    if (state.volume < 0.0f)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.invalid_mixer_volume", "mixer group volume must not be negative"));
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
} // namespace epidemic::runtime::audio
