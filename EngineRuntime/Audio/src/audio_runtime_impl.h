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
    [[nodiscard]] foundation::Result<BackendVoiceHandle> CreateVoice(const AudioVoiceDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyVoice(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Play(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Pause(BackendVoiceHandle handle) override;
    [[nodiscard]] foundation::Result<void> Stop(BackendVoiceHandle handle) override;
    [[nodiscard]] bool IsVoiceFinished(BackendVoiceHandle handle) const override;
    [[nodiscard]] foundation::Result<void> SetGain(BackendVoiceHandle handle, float gain) override;
    [[nodiscard]] foundation::Result<void> SetSpatialState(BackendVoiceHandle handle, const AudioSpatialState& state) override;
    [[nodiscard]] foundation::Result<void> CreateBackendListener(AudioListenerHandle handle, const Transform& transform) override;
    [[nodiscard]] foundation::Result<void> DestroyBackendListener(AudioListenerHandle handle) override;
    [[nodiscard]] foundation::Result<void> SetBackendListenerTransform(AudioListenerHandle handle, const Transform& transform) override;
    [[nodiscard]] foundation::Result<void> Update(RuntimeFrameDuration delta) override;

    [[nodiscard]] foundation::Result<AudioEmitterHandle> CreateEmitterHandle(const AudioEmitterDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyEmitter(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<void> Play(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<void> Pause(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<void> Resume(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<void> Stop(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<void> FadeOut(AudioEmitterHandle handle, RuntimeFrameDuration duration) override;
    [[nodiscard]] foundation::Result<void> FadeIn(AudioEmitterHandle handle, RuntimeFrameDuration duration) override;
    [[nodiscard]] foundation::Result<void> Virtualize(AudioEmitterHandle handle) override;
    [[nodiscard]] foundation::Result<EmitterState> GetEmitterState(AudioEmitterHandle handle) const override;
    [[nodiscard]] foundation::Result<AudioEmitterSnapshot> GetEmitterSnapshot(AudioEmitterHandle handle) const override;
    [[nodiscard]] foundation::Result<void> Tick(RuntimeFrameDuration delta) override;
    [[nodiscard]] foundation::Result<void> Shutdown() override;

    [[nodiscard]] foundation::Result<AudioListenerHandle> CreateListenerHandle(const AudioListenerDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyListener(AudioListenerHandle handle) override;
    [[nodiscard]] foundation::Result<void> SetMainListener(AudioListenerHandle handle) override;
    [[nodiscard]] std::optional<AudioListenerHandle> GetMainListener() const override;

    [[nodiscard]] foundation::Result<void> SubmitOneShot(const AudioEvent& event) override;
    [[nodiscard]] std::span<const AudioEvent> Events() const override;
    void Clear() override;

    [[nodiscard]] foundation::Result<void> SetMixerGroup(MixerGroupState state) override;
    [[nodiscard]] std::optional<MixerGroupState> GetMixerGroup(MixerGroupId id) const override;

    void SetAllocatorStateForTesting(std::uint64_t emitter_value,
                                     std::uint32_t emitter_generation,
                                     std::uint64_t listener_value,
                                     std::uint32_t listener_generation,
                                     std::uint64_t voice_value);
    [[nodiscard]] foundation::Result<void> SetEmitterRevisionForTesting(AudioEmitterHandle handle, std::uint64_t revision);
    void FailNextListenerPublicationForTesting() noexcept { fail_next_listener_publication_for_testing_ = true; }
    [[nodiscard]] std::size_t PendingListenerCleanupCountForTesting() const noexcept
    {
        return pending_listener_cleanups_.size();
    }

private:
    struct EmitterRecord
    {
        AudioEmitterDesc desc{};
        AudioEmitterHandle handle{};
        BackendVoiceHandle voice{};
        std::shared_ptr<const IAudioClipResource> clip_resource{};
        EmitterState state = EmitterState::Stopped;
        RuntimeFrameDuration fade_duration{};
        float fade_progress = 0.0f;
        float base_gain = 1.0f;
        float fade_multiplier = 1.0f;
        float fade_start_multiplier = 1.0f;
        float fade_target_multiplier = 1.0f;
        RuntimeFrameDuration fade_elapsed{};
        std::optional<EmitterState> paused_playback_state;
        std::uint64_t revision = 0;
    };

    struct ListenerRecord
    {
        AudioListenerDesc desc{};
        AudioListenerHandle handle{};
    };

    struct MockVoiceRecord
    {
        AudioVoiceDesc desc{};
        EmitterState state = EmitterState::Stopped;
        float gain = 1.0f;
        AudioSpatialState spatial{};
    };

    struct MixerFadeRecord
    {
        float start_volume = 1.0f;
        float target_volume = 1.0f;
        RuntimeFrameDuration elapsed{};
    };

    struct VoiceOwnership
    {
        BackendVoiceHandle voice{};
        std::shared_ptr<const IAudioClipResource> clip_resource{};
    };

    [[nodiscard]] IAudioBackend* Backend() const noexcept;
    [[nodiscard]] IAudioResourceSource* Resources() const noexcept;
    [[nodiscard]] IAudioTransformSource* Transforms() const noexcept;
    [[nodiscard]] foundation::Result<AudioClipPayload> ResolvePayload(SoundId id);
    [[nodiscard]] foundation::Result<AudioSpatialState> ReadSpatialState(AudioTransformId transform, bool spatial) const;
    [[nodiscard]] foundation::Result<void> ApplyEmitterSpatialState(EmitterRecord& emitter);
    [[nodiscard]] foundation::Result<VoiceOwnership> CreateOneShotVoice(const AudioEvent& event);
    [[nodiscard]] foundation::Result<void> CleanupFinishedOneShots();
    [[nodiscard]] foundation::Result<void> CleanupPendingVoices();
    [[nodiscard]] foundation::Result<void> CleanupPendingListeners();
    [[nodiscard]] foundation::Result<void> EnsureCanStartWork() const;
    void RecordCleanupFailure(const foundation::Error& error);
    void RollbackCreatedVoice(IAudioBackend& backend,
                              BackendVoiceHandle voice,
                              std::shared_ptr<const IAudioClipResource> clip_resource = {});
    void RollbackCreatedListener(IAudioBackend& backend, AudioListenerHandle handle) noexcept;
    void ResetFade(EmitterRecord& emitter);
    [[nodiscard]] foundation::Result<void> BeginFade(EmitterRecord& emitter, EmitterState target_state, RuntimeFrameDuration duration);
    [[nodiscard]] foundation::Result<void> EnsureEmitterRevisionAvailable(const EmitterRecord& emitter) const;
    void CommitEmitterRevision(EmitterRecord& emitter) noexcept;
    [[nodiscard]] foundation::Result<void> ApplyMainListenerTransform();
    [[nodiscard]] float EffectiveGain(const EmitterRecord& emitter) const;
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
    std::unordered_map<MixerGroupId, MixerFadeRecord> mixer_fades_;
    std::optional<AudioListenerHandle> main_listener_;
    std::vector<AudioEvent> events_;
    std::vector<VoiceOwnership> one_shot_voices_;
    std::vector<VoiceOwnership> pending_voice_cleanups_;
    std::vector<AudioListenerHandle> pending_listener_cleanups_;
    std::uint64_t cleanup_failures_ = 0;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
    bool fail_next_listener_publication_for_testing_ = false;
};
} // namespace epidemic::runtime::audio
