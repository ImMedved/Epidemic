#include "truth_test.h"

#include "Epidemic/GameFramework/PerceptionKnowledgeAIIntegration/perception_knowledge_ai_adapters.h"

#include <algorithm>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ai;
using namespace epidemic::gameplay::integration;
using namespace epidemic::gameplay::knowledge;
using namespace epidemic::gameplay::perception;

namespace
{
GameplayObjectRef Actor(std::string_view name)
{
    return {GameplayDomainId::FromString("truth.integration.actor"), GameplayObjectId::FromString(name)};
}

struct Scenario
{
    GameplayObjectRef observer = Actor("observer");
    GameplayObjectRef target = Actor("target");
    SenseTypeId hearing = SenseTypeId::FromString("truth.sense.hearing");
    PerceiverProfileId perception_profile = PerceiverProfileId::FromString("truth.profile.guard_senses");
    PerceptionService perception;
    KnowledgeService knowledge;

    Scenario()
    {
        SenseDefinition sense;
        sense.id = hearing;
        sense.canonical_name = "truth.sense.hearing";
        sense.evaluation_model = SenseEvaluationModel::Hearing;
        sense.base_range_mm = 30'000;
        TRUTH_REQUIRE(perception.RegisterSense(sense));

        PerceiverProfileDefinition profile;
        profile.id = perception_profile;
        profile.canonical_name = "truth.profile.guard_senses";
        profile.senses = {hearing};
        TRUTH_REQUIRE(perception.RegisterProfileDefinition(profile));
        perception.Freeze();
        TRUTH_REQUIRE(perception.RegisterPerceiver(observer, perception_profile));

        KnowledgeProfile knowledge_profile;
        knowledge_profile.subject = observer;
        TRUTH_REQUIRE(knowledge.CreateProfile(knowledge_profile));
    }

    PerceptionObservation Observe()
    {
        PerceptionStimulus stimulus;
        stimulus.sense = hearing;
        stimulus.source = target;
        stimulus.position = {2'000, 0, 0};
        stimulus.strength_micro = 1'000'000;
        stimulus.created_at = GameplayTimePoint{10};
        stimulus.lifetime = GameplayDuration{20};
        const auto id = perception.CreateStimulus(stimulus);
        TRUTH_REQUIRE(id);

        PerceiverEvaluationSample sample;
        sample.subject = observer;
        sample.position = {0, 0, 0};
        sample.materialized = true;
        sample.runtime_projection_available = true;
        GameplayContext causal_context;
        causal_context.tick = GameplayTickId{77};
        causal_context.time = GameplayTimePoint{10};
        const auto observations = perception.ProcessStimulus(
            id.Value(), PerceptionProcessingContext{GameplayTickId{77}, GameplayTimePoint{10}, causal_context, {sample}});
        TRUTH_REQUIRE(observations && observations.Value().size() == 1);
        return observations.Value().front();
    }
};

void ObservationBecomesSubjectiveKnowledge()
{
    Scenario scenario;
    const auto observation = scenario.Observe();
    PerceptionKnowledgeMapping mapping;
    mapping.observed_topic = KnowledgeTopicId::FromString("truth.topic.detected_actor");
    PerceptionKnowledgeAdapter adapter{scenario.knowledge, mapping};
    const auto learned = adapter.LearnFromObservation(observation);
    TRUTH_REQUIRE(learned && learned.Value().knowledge.IsValid());
    const auto* record = scenario.knowledge.FindKnowledge(learned.Value().knowledge);
    TRUTH_REQUIRE(record != nullptr);
    TRUTH_REQUIRE(record->owner == scenario.observer);
    TRUTH_REQUIRE(record->subject == scenario.target);
    TRUTH_REQUIRE(record->learned_at == GameplayTimePoint{10});
    const auto changes = scenario.knowledge.ReadChangesSince(ChangeCursor{});
    TRUTH_REQUIRE(!changes.changes.empty());
    TRUTH_REQUIRE(changes.changes.back().context.tick == GameplayTickId{77});
    TRUTH_REQUIRE(changes.changes.back().context.time == GameplayTimePoint{10});
}

void KnowledgeDrivesARealTargetedIntent()
{
    Scenario scenario;
    const auto observation = scenario.Observe();
    PerceptionKnowledgeMapping mapping;
    mapping.observed_topic = KnowledgeTopicId::FromString("truth.topic.hostile");
    PerceptionKnowledgeAdapter learning{scenario.knowledge, mapping};
    TRUTH_REQUIRE(learning.LearnFromObservation(observation));

    AIService ai;
    AIGoalDefinition goal;
    goal.id = AIGoalId::FromString("truth.goal.investigate");
    goal.canonical_name = "truth.goal.investigate";
    goal.intent_type = AIIntentTypeId::FromString("truth.intent.investigate");
    goal.target_policy = AITargetPolicy::Required;
    goal.base_priority_micro = 100;
    TRUTH_REQUIRE(ai.RegisterGoal(goal));

    AIProfile profile;
    profile.id = AIProfileId::FromString("truth.profile.guard_ai");
    profile.canonical_name = "truth.profile.guard_ai";
    profile.default_goals = {goal.id};
    profile.think_interval = GameplayDuration{1};
    TRUTH_REQUIRE(ai.RegisterProfile(profile));
    TRUTH_REQUIRE(ai.Freeze());
    TRUTH_REQUIRE(ai.RegisterAgent(scenario.observer, profile.id));

    KnowledgeAIAdapter context_builder{scenario.knowledge};
    const auto context = context_builder.BuildContext(
        scenario.observer, AIExecutionAvailability{true, true}, &scenario.perception, GameplayTimePoint{10});
    TRUTH_REQUIRE(std::any_of(context.targets.begin(), context.targets.end(), [&](const auto& candidate) {
        return candidate.target == scenario.target;
    }));

    GameplayContext causality;
    causality.tick = GameplayTickId{78};
    causality.time = GameplayTimePoint{10};
    const auto decision = ai.Think(scenario.observer, context, causality);
    TRUTH_REQUIRE(decision && decision.Value().intent.has_value());
    TRUTH_REQUIRE(decision.Value().intent->target == scenario.target);
    TRUTH_REQUIRE(decision.Value().intent->context.tick == GameplayTickId{78});

    AIIntentExecutionRecorder execution_log{4};
    TRUTH_REQUIRE(execution_log.Accept(*decision.Value().intent, ai));
    TRUTH_REQUIRE(execution_log.Succeed(decision.Value().intent->id, ai));
    TRUTH_REQUIRE(ai.FindAgent(scenario.observer)->activity == AIAgentActivity::Idle);
}
} // namespace

int main()
{
    return epidemic::truth_tests::Run("framework-integration", {
        {"observation becomes subjective knowledge", &ObservationBecomesSubjectiveKnowledge},
        {"knowledge drives a real targeted intent", &KnowledgeDrivesARealTargetedIntent},
    });
}