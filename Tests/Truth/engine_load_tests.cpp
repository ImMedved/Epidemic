#include "truth_test.h"

#include "Epidemic/GameFramework/AI/ai.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Perception/perception.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"
#include "Epidemic/Runtime/Support/runtime_support.h"

#include <Epidemic/Core/application.h>

#include <chrono>
#include <cstddef>
#include <vector>

using namespace epidemic::gameplay;

namespace
{
GameplayObjectRef LoadActor(std::uint64_t id)
{
    return {GameplayDomainId::FromString("truth.load.actor"), GameplayObjectId::FromRaw(0xAC70, id)};
}

void EntityOwnerHandlesOneHundredThousandLiveRecords()
{
    using namespace entities;
    EntityService service;
    EntityArchetypeDefinition definition;
    definition.canonical_name = "truth.load.entity";
    const auto archetype = service.RegisterArchetype(definition);
    TRUTH_REQUIRE(archetype);
    service.Freeze();

    std::vector<EntityHandle> samples;
    samples.reserve(100);
    for (std::size_t index = 0; index < 100'000; ++index)
    {
        CreateEntityRequest request;
        request.archetype = archetype.Value();
        const auto created = service.Create(request);
        TRUTH_REQUIRE(created);
        if (index % 1000 == 0)
        {
            samples.push_back(created.Value().handle);
        }
    }
    TRUTH_REQUIRE(service.GetDiagnostics().entity_count == 100'000);
    TRUTH_REQUIRE(service.AllEntities().size() == 100'000);
    for (const auto handle : samples)
    {
        TRUTH_REQUIRE(service.Resolve(handle).has_value());
    }
}

void SchedulerBudgetBoundsTwentyThousandDeadlines()
{
    using namespace time;
    GameplayTimeService service;
    const auto clock = service.RegisterClock("truth.load.clock");
    const auto domain = GameplayDomainId::FromString("truth.load.scheduler");
    const auto action = service.RegisterAction("truth.load.deadline", domain);
    TRUTH_REQUIRE(clock && action);
    service.Freeze();

    for (std::uint64_t index = 1; index <= 20'000; ++index)
    {
        const GameplayObjectRef owner{domain, GameplayObjectId::FromRaw(0x5CED, index)};
        TRUTH_REQUIRE(service.Schedule(clock.Value(), GameplayTimePoint{1}, owner, action.Value()));
    }
    TRUTH_REQUIRE(service.AdvanceTo(clock.Value(), GameplayTimePoint{1}));

    std::size_t emitted = 0;
    for (;;)
    {
        const auto batch = service.CollectDue(clock.Value(), SchedulerBudget{257, 257});
        TRUTH_REQUIRE(batch);
        TRUTH_REQUIRE(batch.Value().size() <= 257);
        emitted += batch.Value().size();
        if (batch.Value().empty())
        {
            break;
        }
    }
    TRUTH_REQUIRE(emitted == 20'000);
    TRUTH_REQUIRE(service.GetDiagnostics().active_schedules == 0);
    TRUTH_REQUIRE(service.GetDiagnostics().budget_exhaustions > 0);
}

void PerceptionAndAIBudgetsBoundNpcWork()
{
    using namespace perception;
    PerceptionService perception;
    SenseDefinition hearing;
    hearing.id = SenseTypeId::FromString("truth.load.hearing");
    hearing.canonical_name = "truth.load.hearing";
    hearing.evaluation_model = SenseEvaluationModel::Hearing;
    hearing.base_range_mm = 100'000;
    TRUTH_REQUIRE(perception.RegisterSense(hearing));
    PerceiverProfileDefinition perceiver_profile;
    perceiver_profile.id = PerceiverProfileId::FromString("truth.load.perceiver");
    perceiver_profile.canonical_name = "truth.load.perceiver";
    perceiver_profile.senses = {hearing.id};
    TRUTH_REQUIRE(perception.RegisterProfileDefinition(perceiver_profile));
    perception.SetBudget(PerceptionBudget{1, 128, 128});
    perception.Freeze();

    std::vector<PerceiverEvaluationSample> samples;
    samples.reserve(5'000);
    for (std::uint64_t index = 1; index <= 5'000; ++index)
    {
        const auto actor = LoadActor(index);
        TRUTH_REQUIRE(perception.RegisterPerceiver(actor, perceiver_profile.id));
        PerceiverEvaluationSample sample;
        sample.subject = actor;
        sample.position = {static_cast<Fixed>(index), 0, 0};
        sample.materialized = true;
        sample.runtime_projection_available = true;
        samples.push_back(sample);
    }
    PerceptionStimulus stimulus;
    stimulus.sense = hearing.id;
    stimulus.source = LoadActor(9'000);
    stimulus.position = {0, 0, 0};
    const auto stimulus_id = perception.CreateStimulus(stimulus);
    TRUTH_REQUIRE(stimulus_id);
    const auto observations = perception.ProcessStimulus(
        stimulus_id.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, samples});
    TRUTH_REQUIRE(observations);
    TRUTH_REQUIRE(observations.Value().size() <= 128);
    TRUTH_REQUIRE(perception.GetDiagnostics().detection_tests <= 128);
    TRUTH_REQUIRE(perception.GetDiagnostics().budget_exhaustions > 0);

    using namespace ai;
    AIService ai;
    AIGoalDefinition goal;
    goal.id = AIGoalId::FromString("truth.load.goal");
    goal.canonical_name = "truth.load.goal";
    goal.intent_type = AIIntentTypeId::FromString("truth.load.intent");
    goal.target_policy = AITargetPolicy::Targetless;
    goal.base_priority_micro = 1;
    TRUTH_REQUIRE(ai.RegisterGoal(goal));
    AIProfile ai_profile;
    ai_profile.id = AIProfileId::FromString("truth.load.ai_profile");
    ai_profile.canonical_name = "truth.load.ai_profile";
    ai_profile.default_goals = {goal.id};
    TRUTH_REQUIRE(ai.RegisterProfile(ai_profile));
    TRUTH_REQUIRE(ai.Freeze());
    for (std::uint64_t index = 1; index <= 10'000; ++index)
    {
        TRUTH_REQUIRE(ai.RegisterAgent(LoadActor(index), ai_profile.id, GameplayTimePoint{5}));
    }
    const auto due = ai.FindDueAgents(GameplayTimePoint{5}, 256);
    TRUTH_REQUIRE(due.size() == 256);
    TRUTH_REQUIRE(ai.GetDiagnostics().agents == 10'000);
}

void DefaultRuntimeSurvivesOneThousandHeadlessFrames()
{
    epidemic::core::Application application({"TruthLoadRuntime"});
    const auto runtime = epidemic::runtime::RegisterDefaultEngineRuntime(application);
    TRUTH_REQUIRE(runtime);
    epidemic::runtime::RuntimeFrameInput frame;
    frame.real_delta = epidemic::runtime::RuntimeFrameDuration{std::chrono::microseconds{16'667}};
    for (int index = 0; index < 1'000; ++index)
    {
        TRUTH_REQUIRE(runtime.Value().coordinator->Tick(frame));
    }
    TRUTH_REQUIRE(runtime.Value().coordinator->Shutdown());
}
} // namespace

int main()
{
    return epidemic::truth_tests::Run("engine-load", {
        {"entity owner handles 100000 live records", &EntityOwnerHandlesOneHundredThousandLiveRecords},
        {"scheduler budget bounds 20000 deadlines", &SchedulerBudgetBoundsTwentyThousandDeadlines},
        {"perception and AI budgets bound NPC work", &PerceptionAndAIBudgetsBoundNpcWork},
        {"default runtime survives 1000 headless frames", &DefaultRuntimeSurvivesOneThousandHeadlessFrames},
    });
}