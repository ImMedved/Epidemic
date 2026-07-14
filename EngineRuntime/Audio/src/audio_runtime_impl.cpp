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
    emitters_.emplace(id, EmitterRecord{desc, EmitterState::Stopped});
    return foundation::Result<AudioEmitterId>::Success(id);
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
        emitter->state = EmitterState::Stopped;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.sound_not_ready", "audio emitter sound is not ready"));
    }

    if (!options_.enable_mock_backend)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("audio.backend_disabled", "mock audio backend is disabled"));
    }

    emitter->state = EmitterState::Playing;
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

    emitter->state = EmitterState::Stopped;
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

    emitter->state = EmitterState::FadingOut;
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

    emitter->state = EmitterState::Virtualized;
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

bool AudioRuntime::HasListener(AudioListenerId id) const
{
    return listeners_.contains(id);
}
} // namespace epidemic::runtime::audio
