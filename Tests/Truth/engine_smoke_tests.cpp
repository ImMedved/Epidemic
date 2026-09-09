#include "truth_test.h"

#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Progression/progression.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include "Epidemic/Runtime/Support/runtime_support.h"
#include "Epidemic/Runtime/Time/time_runtime.h"

#include <Epidemic/Core/application.h>
#include <Epidemic/EngineBase/engine_base_support.h>

#include <chrono>

using namespace epidemic::gameplay;

namespace
{
void HeadlessApplicationBootsRunsAndStops()
{
    epidemic::core::Application application({"TruthSmokeBase"});
    const auto logger = epidemic::enginebase::RegisterEngineBase(
        application, {.runtime_name = "TruthSmokeBase", .log_module = "TruthSmoke", .worker_count = 2});
    TRUTH_REQUIRE(logger != nullptr);
    application.SetFrameLimit(3);
    TRUTH_REQUIRE(application.Bootstrap() == 0);
    TRUTH_REQUIRE(application.Initialize() == 0);
    TRUTH_REQUIRE(application.Run() == 0);
    TRUTH_REQUIRE(application.Shutdown() == 0);
}

void RuntimeClockProjectsIntoGameplayClock()
{
    epidemic::runtime::TimeOptions runtime_options;
    runtime_options.game_ticks_per_real_second = 60;
    const auto runtime_time = epidemic::runtime::CreateTimeServices(runtime_options);
    TRUTH_REQUIRE(runtime_time);

    time::GameplayTimeService gameplay_time;
    const auto clock = gameplay_time.RegisterClock("truth.smoke.projected_clock");
    const auto domain = GameplayDomainId::FromString("truth.smoke");
    const auto action = gameplay_time.RegisterAction("truth.smoke.midnight", domain);
    TRUTH_REQUIRE(clock && action);
    gameplay_time.Freeze();

    const GameplayObjectRef owner{domain, GameplayObjectId::FromString("world")};
    TRUTH_REQUIRE(gameplay_time.Schedule(clock.Value(), GameplayTimePoint{120}, owner, action.Value()));
    TRUTH_REQUIRE(runtime_time.Value().runtime->Advance(std::chrono::seconds{2}));
    const auto runtime_now = runtime_time.Value().clock->Now();
    TRUTH_REQUIRE(runtime_now.ticks == 120);
    TRUTH_REQUIRE(gameplay_time.SynchronizeClock(clock.Value(), GameplayTimePoint{runtime_now.ticks}, Revision{1}));
    const auto triggers = gameplay_time.CollectDue(clock.Value());
    TRUTH_REQUIRE(triggers && triggers.Value().size() == 1);
}

void SavedGameplaySessionContinuesAfterRestore()
{
    using namespace entities;
    using namespace progression;

    EntityService entities;
    EntityArchetypeDefinition actor_definition;
    actor_definition.canonical_name = "truth.smoke.actor";
    const auto archetype = entities.RegisterArchetype(actor_definition);
    TRUTH_REQUIRE(archetype);
    entities.Freeze();
    CreateEntityRequest create;
    create.archetype = archetype.Value();
    const auto actor = entities.Create(create);
    TRUTH_REQUIRE(actor);

    ProgressionService progression;
    ProgressionTrackDefinition experience;
    experience.canonical_name = "truth.smoke.experience";
    experience.rank_thresholds_micro = {100, 250};
    const auto track = progression.RegisterTrack(experience);
    TRUTH_REQUIRE(track && progression.Freeze());
    const auto actor_ref = EntityService::ToGameplayObjectRef(actor.Value().id);
    TRUTH_REQUIRE(progression.EnsureProfile(actor_ref));
    TRUTH_REQUIRE(progression.GrantProgress(actor_ref, track.Value(), 90));

    auto entity_snapshot = entities.CaptureSnapshot();
    auto progression_snapshot = progression.CaptureSnapshot();

    EntityService restored_entities;
    TRUTH_REQUIRE(restored_entities.RegisterArchetype(actor_definition));
    restored_entities.Freeze();
    ProgressionService restored_progression;
    TRUTH_REQUIRE(restored_progression.RegisterTrack(experience));
    TRUTH_REQUIRE(restored_progression.Freeze());
    TRUTH_REQUIRE(restored_entities.RestoreSnapshot(std::move(entity_snapshot)));
    TRUTH_REQUIRE(restored_progression.RestoreSnapshot(std::move(progression_snapshot)));

    TRUTH_REQUIRE(restored_entities.Exists(actor.Value().id));
    TRUTH_REQUIRE(restored_progression.GrantProgress(actor_ref, track.Value(), 20));
    const auto state = restored_progression.GetTrack(actor_ref, track.Value());
    TRUTH_REQUIRE(state && state.Value().progress_micro == 110);
    TRUTH_REQUIRE(state.Value().rank == 1);
}

void CompleteRuntimeCompositionTicksAndShutsDown()
{
    epidemic::core::Application application({"TruthSmokeRuntime"});
    const auto runtime = epidemic::runtime::RegisterDefaultEngineRuntime(application);
    TRUTH_REQUIRE(runtime);
    TRUTH_REQUIRE(runtime.Value().registered_majors.size() == 16);
    epidemic::runtime::RuntimeFrameInput frame;
    frame.real_delta = epidemic::runtime::RuntimeFrameDuration{std::chrono::microseconds{16'667}};
    for (int index = 0; index < 8; ++index)
    {
        const auto result = runtime.Value().coordinator->Tick(frame);
        TRUTH_REQUIRE(result);
        TRUTH_REQUIRE(!result.Value().executed_steps.empty());
    }
    TRUTH_REQUIRE(runtime.Value().coordinator->Shutdown());
}
} // namespace

int main()
{
    return epidemic::truth_tests::Run("engine-smoke", {
        {"headless application boots runs and stops", &HeadlessApplicationBootsRunsAndStops},
        {"runtime clock projects into gameplay clock", &RuntimeClockProjectsIntoGameplayClock},
        {"saved gameplay session continues after restore", &SavedGameplaySessionContinuesAfterRestore},
        {"complete runtime composition ticks and shuts down", &CompleteRuntimeCompositionTicksAndShutsDown},
    });
}