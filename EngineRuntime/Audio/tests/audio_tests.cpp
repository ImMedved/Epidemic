#include "audio_runtime_impl.h"

#include <iostream>
#include <string_view>
#include <type_traits>

using epidemic::runtime::GameDuration;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::Vec3;
using epidemic::runtime::audio::CreateMockAudioServices;
using epidemic::runtime::audio::IAudioBackend;
using epidemic::runtime::audio::IAudioResourceSource;
using epidemic::runtime::audio::IAudioTransformSource;
using epidemic::runtime::audio::AudioEmitterDesc;
using epidemic::runtime::audio::AudioEvent;
using epidemic::runtime::audio::AudioListenerDesc;
using epidemic::runtime::audio::AudioOptions;
using epidemic::runtime::audio::AudioRuntime;
using epidemic::runtime::audio::AudioTransformId;
using epidemic::runtime::audio::EmitterState;
using epidemic::runtime::audio::MixerFadeState;
using epidemic::runtime::audio::MixerGroupId;
using epidemic::runtime::audio::MixerGroupState;
using epidemic::runtime::audio::SoundDesc;
using epidemic::runtime::audio::SoundId;
using epidemic::runtime::audio::SoundState;

namespace
{
bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }

    return condition;
}

bool SeedSound(AudioRuntime& runtime, SoundState state = SoundState::Ready)
{
    return runtime.RegisterSound(SoundDesc{SoundId{1}, state}).HasValue();
}

AudioEmitterDesc MakeEmitterDesc()
{
    return AudioEmitterDesc{RuntimeObjectId{88}, SoundId{1}, AudioTransformId{9}, false};
}

bool TestCreatePlayStopDestroyEmitter()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "new emitter should be stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 1, "new emitter snapshot should start at revision one");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "ready sound should play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Playing, "emitter should be playing");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 2, "play should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "playing emitter should stop");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "emitter should return stopped");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 3, "stop should increment emitter revision");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "idempotent stop should succeed");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 3, "idempotent stop should not increment revision");
    ok &= Expect(runtime.DestroyEmitter(emitter.Value()).HasValue(), "emitter should destroy");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Destroyed, "destroyed emitter should read destroyed");
    ok &= Expect(runtime.GetEmitterSnapshot(emitter.Value()).revision == 0, "destroyed emitter snapshot should be revision zero");
    return ok;
}

bool TestListenerCreationAndMainListener()
{
    AudioRuntime runtime{{}};
    const auto listener = runtime.CreateListener(AudioListenerDesc{AudioTransformId{12}});
    bool ok = Expect(listener.HasValue(), "valid listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "main listener should be set");
    ok &= Expect(runtime.GetMainListener().has_value(), "main listener should be readable");
    ok &= Expect(runtime.GetMainListener().has_value() && runtime.GetMainListener()->value == listener.Value().value, "main listener id should match");
    ok &= Expect(!runtime.CreateListener(AudioListenerDesc{}).HasValue(), "invalid listener transform should fail");
    return ok;
}

bool TestInvalidSoundBehavior()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime, SoundState::Loading), "loading sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "emitter can reference registered loading sound");
    ok &= Expect(!runtime.Play(emitter.Value()).HasValue(), "loading sound should not play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "failed play should leave stopped state");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{99}, Vec3{}, 1.0f}).HasValue(), "missing one-shot sound should fail");
    return ok;
}

bool TestFadeVirtualizedMixerAndEvents()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.FadeOut(emitter.Value()).HasValue(), "emitter should fade out");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::FadingOut, "emitter should report fade-out state");
    ok &= Expect(runtime.Virtualize(emitter.Value()).HasValue(), "emitter should virtualize");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Virtualized, "emitter should report virtualized state");
    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{1.0f, 2.0f, 3.0f}, 0.75f}).HasValue(), "one-shot should queue");
    ok &= Expect(runtime.Events().size() == 1, "event queue should contain one event");
    runtime.Clear();
    ok &= Expect(runtime.Events().empty(), "event queue should clear");
    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{3}, 0.5f, MixerFadeState::FadingOut}).HasValue(), "mixer group should update");
    const auto group = runtime.GetMixerGroup(MixerGroupId{3});
    ok &= Expect(group.has_value() && group->fade_state == MixerFadeState::FadingOut, "mixer fade state should be stored");
    return ok;
}

bool TestValidationFailures()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(!runtime.RegisterSound(SoundDesc{}).HasValue(), "invalid sound id should fail");
    ok &= Expect(!runtime.CreateEmitter(AudioEmitterDesc{}).HasValue(), "empty emitter desc should fail");
    ok &= Expect(!runtime.SetMainListener({99}).HasValue(), "unknown listener should fail main selection");
    ok &= Expect(!runtime.SetMixerGroup(MixerGroupState{MixerGroupId{}, 1.0f, MixerFadeState::Stable}).HasValue(), "invalid mixer group should fail");
    return ok;
}

bool TestBackendContractsHandlesBoundsAndHierarchy()
{
    AudioRuntime runtime{AudioOptions{true, 1}};
    bool ok = Expect(runtime.IsEnabled(), "mock backend should be enabled");
    ok &= Expect(SeedSound(runtime), "sound should seed");
    const auto handle = runtime.CreateEmitterHandle(MakeEmitterDesc());
    ok &= Expect(handle.HasValue(), "emitter handle should be created");
    ok &= Expect(runtime.FadeOut(handle.Value(), GameDuration{5}).HasValue(), "handle fade should succeed");
    const auto snapshot = runtime.GetEmitterSnapshot(handle.Value());
    ok &= Expect(snapshot.handle == handle.Value(), "snapshot should preserve emitter handle");
    ok &= Expect(snapshot.fade_duration.ticks == 5, "snapshot should expose fade duration");

    ok &= Expect(runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "first event should queue");
    ok &= Expect(!runtime.SubmitOneShot(AudioEvent{SoundId{1}, Vec3{}, 1.0f}).HasValue(), "bounded queue should reject overflow");

    const auto listener = runtime.CreateListener(AudioListenerDesc{AudioTransformId{44}});
    ok &= Expect(listener.HasValue(), "listener should be created");
    ok &= Expect(runtime.SetMainListener(listener.Value()).HasValue(), "listener should become main");
    ok &= Expect(runtime.DestroyListener(listener.Value()).HasValue(), "listener should destroy");
    ok &= Expect(!runtime.GetMainListener().has_value(), "destroyed main listener should clear selection");

    ok &= Expect(runtime.SetMixerGroup(MixerGroupState{MixerGroupId{4}, 1.0f, MixerFadeState::FadingIn, MixerGroupId{3}, GameDuration{2}, 0.5f}).HasValue(),
                 "hierarchical mixer group should update");
    const auto mixer = runtime.GetMixerGroup(MixerGroupId{4});
    ok &= Expect(mixer.has_value() && mixer->parent == MixerGroupId{3} && mixer->fade_progress == 0.5f,
                 "mixer hierarchy and fade progress should persist");

    const auto services = CreateMockAudioServices();
    ok &= Expect(services.sounds != nullptr && services.runtime != nullptr && services.listeners != nullptr &&
                     services.events != nullptr && services.mixer != nullptr && services.backend != nullptr,
                 "audio services should be populated");
    return ok;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IAudioBackend>);
    static_assert(std::is_abstract_v<IAudioResourceSource>);
    static_assert(std::is_abstract_v<IAudioTransformSource>);

    bool ok = true;
    ok &= TestCreatePlayStopDestroyEmitter();
    ok &= TestListenerCreationAndMainListener();
    ok &= TestInvalidSoundBehavior();
    ok &= TestFadeVirtualizedMixerAndEvents();
    ok &= TestValidationFailures();
    ok &= TestBackendContractsHandlesBoundsAndHierarchy();
    return ok ? 0 : 1;
}
