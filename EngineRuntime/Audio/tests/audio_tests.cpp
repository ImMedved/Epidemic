#include "audio_runtime_impl.h"

#include <iostream>
#include <string_view>

using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::Vec3;
using epidemic::runtime::audio::AudioEmitterDesc;
using epidemic::runtime::audio::AudioEvent;
using epidemic::runtime::audio::AudioListenerDesc;
using epidemic::runtime::audio::AudioRuntime;
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
    return AudioEmitterDesc{RuntimeObjectId{88}, SoundId{1}, SceneNodeId{9}, false};
}

bool TestCreatePlayStopDestroyEmitter()
{
    AudioRuntime runtime{{}};
    bool ok = Expect(SeedSound(runtime), "sound should seed");
    const auto emitter = runtime.CreateEmitter(MakeEmitterDesc());
    ok &= Expect(emitter.HasValue(), "valid emitter should be created");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "new emitter should be stopped");
    ok &= Expect(runtime.Play(emitter.Value()).HasValue(), "ready sound should play");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Playing, "emitter should be playing");
    ok &= Expect(runtime.Stop(emitter.Value()).HasValue(), "playing emitter should stop");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Stopped, "emitter should return stopped");
    ok &= Expect(runtime.DestroyEmitter(emitter.Value()).HasValue(), "emitter should destroy");
    ok &= Expect(runtime.GetEmitterState(emitter.Value()) == EmitterState::Destroyed, "destroyed emitter should read destroyed");
    return ok;
}

bool TestListenerCreationAndMainListener()
{
    AudioRuntime runtime{{}};
    const auto listener = runtime.CreateListener(AudioListenerDesc{SceneNodeId{12}});
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
} // namespace

int main()
{
    bool ok = true;
    ok &= TestCreatePlayStopDestroyEmitter();
    ok &= TestListenerCreationAndMainListener();
    ok &= TestInvalidSoundBehavior();
    ok &= TestFadeVirtualizedMixerAndEvents();
    ok &= TestValidationFailures();
    return ok ? 0 : 1;
}
