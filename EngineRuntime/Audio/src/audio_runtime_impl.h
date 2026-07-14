#pragma once

#include "Epidemic/Runtime/Audio/audio_runtime.h"

#include <unordered_map>
#include <vector>

namespace epidemic::runtime::audio
{
// File note:
// Internal in-memory Audio runtime. It models service contracts and state transitions without opening an audio device.

class AudioRuntime final : public ISoundRegistry,
                           public IAudioRuntime,
                           public IListenerSystem,
                           public IAudioEventQueue,
                           public IMixerSystem
{
public:
    explicit AudioRuntime(AudioOptions options);

    [[nodiscard]] foundation::Result<void> RegisterSound(SoundDesc desc) override;
    [[nodiscard]] SoundState GetSoundState(SoundId id) const override;

    [[nodiscard]] foundation::Result<AudioEmitterId> CreateEmitter(const AudioEmitterDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyEmitter(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> Play(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> Stop(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> FadeOut(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> Virtualize(AudioEmitterId id) override;
    [[nodiscard]] EmitterState GetEmitterState(AudioEmitterId id) const override;

    [[nodiscard]] foundation::Result<AudioListenerId> CreateListener(const AudioListenerDesc& desc) override;
    [[nodiscard]] foundation::Result<void> SetMainListener(AudioListenerId id) override;
    [[nodiscard]] std::optional<AudioListenerId> GetMainListener() const override;

    [[nodiscard]] foundation::Result<void> SubmitOneShot(const AudioEvent& event) override;
    [[nodiscard]] std::span<const AudioEvent> Events() const override;
    void Clear() override;

    [[nodiscard]] foundation::Result<void> SetMixerGroup(MixerGroupState state) override;
    [[nodiscard]] std::optional<MixerGroupState> GetMixerGroup(MixerGroupId id) const override;

private:
    struct EmitterRecord
    {
        AudioEmitterDesc desc{};
        EmitterState state = EmitterState::Stopped;
    };

    struct ListenerRecord
    {
        AudioListenerDesc desc{};
    };

    [[nodiscard]] EmitterRecord* FindEmitter(AudioEmitterId id);
    [[nodiscard]] const EmitterRecord* FindEmitter(AudioEmitterId id) const;
    [[nodiscard]] bool HasListener(AudioListenerId id) const;

    AudioOptions options_{};
    std::uint64_t next_emitter_value_ = 1;
    std::uint64_t next_listener_value_ = 1;
    std::unordered_map<SoundId, SoundDesc> sounds_;
    std::unordered_map<AudioEmitterId, EmitterRecord> emitters_;
    std::unordered_map<AudioListenerId, ListenerRecord> listeners_;
    std::unordered_map<MixerGroupId, MixerGroupState> mixer_groups_;
    std::optional<AudioListenerId> main_listener_;
    std::vector<AudioEvent> events_;
};
} // namespace epidemic::runtime::audio
