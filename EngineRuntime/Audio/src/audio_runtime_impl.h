#pragma once

#include "Epidemic/Runtime/Audio/audio_runtime.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace epidemic::runtime::audio
{
// Internal in-memory Audio runtime. It models service contracts and state transitions without opening an audio device.

class AudioRuntime final : public ISoundRegistry,
                           public IAudioRuntime,
                           public IListenerSystem,
                           public IAudioEventQueue,
                           public IMixerSystem,
                           public IAudioBackend
{
public:
    explicit AudioRuntime(AudioOptions options, AudioDependencies dependencies = {});

    [[nodiscard]] foundation::Result<void> RegisterSound(SoundDesc desc) override;
    [[nodiscard]] SoundState GetSoundState(SoundId id) const override;
    [[nodiscard]] bool IsEnabled() const override;
    [[nodiscard]] foundation::Result<void> Initialize(const AudioBackendOptions& options) override;
    [[nodiscard]] foundation::Result<BackendVoiceHandle> CreateVoice(const AudioClipPayload& payload) override;
    [[nodiscard]] foundation::Result<void> DestroyVoice(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Play(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Pause(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Stop(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> SetGain(BackendVoiceHandle handle, float gain) override;
    [[nodiscard]] foundation::Result<void> SetSpatialState(BackendVoiceHandle handle, const AudioSpatialState& state) override;
    [[nodiscard]] foundation::Result<void> Update(GameDuration delta) override;

    [[nodiscard]] foundation::Result<AudioEmitterId> CreateEmitter(const AudioEmitterDesc& desc) override;
    [[nodiscard]] foundation::Result<AudioEmitterHandle> CreateEmitterHandle(const AudioEmitterDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyEmitter(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> Play(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> Stop(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> FadeOut(AudioEmitterId id) override;
    [[nodiscard]] foundation::Result<void> FadeOut(AudioEmitterHandle handle, GameDuration duration) override;
    [[nodiscard]] foundation::Result<void> Virtualize(AudioEmitterId id) override;
    [[nodiscard]] EmitterState GetEmitterState(AudioEmitterId id) const override;
    [[nodiscard]] AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterId id) const override;
    [[nodiscard]] AudioEmitterSnapshot GetEmitterSnapshot(AudioEmitterHandle handle) const override;
    [[nodiscard]] foundation::Result<void> Tick(GameDuration delta) override;

    [[nodiscard]] foundation::Result<AudioListenerId> CreateListener(const AudioListenerDesc& desc) override;
    [[nodiscard]] foundation::Result<AudioListenerHandle> CreateListenerHandle(const AudioListenerDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyListener(AudioListenerId id) override;
    [[nodiscard]] foundation::Result<void> DestroyListener(AudioListenerHandle handle) override;
    [[nodiscard]] foundation::Result<void> SetMainListener(AudioListenerId id) override;
    [[nodiscard]] foundation::Result<void> SetMainListener(AudioListenerHandle handle) override;
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
        AudioEmitterHandle handle{};
        BackendVoiceHandle voice{};
        EmitterState state = EmitterState::Stopped;
        GameDuration fade_duration{};
        float fade_progress = 0.0f;
        float gain = 1.0f;
        float fade_start_gain = 1.0f;
        float fade_target_gain = 1.0f;
        GameDuration fade_elapsed{};
        std::uint64_t revision = 0;
    };

    struct ListenerRecord
    {
        AudioListenerDesc desc{};
        AudioListenerHandle handle{};
    };

    struct MockVoiceRecord
    {
        AudioClipPayload payload{};
        EmitterState state = EmitterState::Stopped;
        float gain = 1.0f;
        AudioSpatialState spatial{};
    };

    [[nodiscard]] IAudioBackend* Backend() const noexcept;
    [[nodiscard]] IAudioResourceSource* Resources() const noexcept;
    [[nodiscard]] IAudioTransformSource* Transforms() const noexcept;
    [[nodiscard]] foundation::Result<AudioClipPayload> ResolvePayload(SoundId id);
    [[nodiscard]] foundation::Result<void> ApplyEmitterSpatialState(EmitterRecord& emitter);
    [[nodiscard]] EmitterRecord* FindEmitter(AudioEmitterId id);
    [[nodiscard]] const EmitterRecord* FindEmitter(AudioEmitterId id) const;
    [[nodiscard]] EmitterRecord* FindEmitter(AudioEmitterHandle handle);
    [[nodiscard]] const EmitterRecord* FindEmitter(AudioEmitterHandle handle) const;
    [[nodiscard]] ListenerRecord* FindListener(AudioListenerId id);
    [[nodiscard]] const ListenerRecord* FindListener(AudioListenerId id) const;
    [[nodiscard]] ListenerRecord* FindListener(AudioListenerHandle handle);
    [[nodiscard]] const ListenerRecord* FindListener(AudioListenerHandle handle) const;
    [[nodiscard]] bool HasListener(AudioListenerId id) const;
    [[nodiscard]] bool MixerWouldCycle(MixerGroupId id, MixerGroupId parent) const;
    [[nodiscard]] bool IsFinite(float value) const noexcept;

    AudioOptions options_{};
    AudioDependencies dependencies_{};
    std::uint64_t next_emitter_value_ = 1;
    std::uint32_t next_emitter_generation_ = 1;
    std::uint64_t next_listener_value_ = 1;
    std::uint32_t next_listener_generation_ = 1;
    std::uint64_t next_voice_value_ = 1;
    std::unordered_map<SoundId, SoundDesc> sounds_;
    std::unordered_map<AudioEmitterId, EmitterRecord> emitters_;
    std::unordered_map<AudioListenerId, ListenerRecord> listeners_;
    std::unordered_map<BackendVoiceHandle, MockVoiceRecord> voices_;
    std::unordered_map<MixerGroupId, MixerGroupState> mixer_groups_;
    std::optional<AudioListenerId> main_listener_;
    std::vector<AudioEvent> events_;
};
} // namespace epidemic::runtime::audio
