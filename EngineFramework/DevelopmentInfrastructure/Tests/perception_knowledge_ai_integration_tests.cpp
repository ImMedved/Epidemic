#include "Epidemic/GameFramework/PerceptionKnowledgeAIIntegration/perception_knowledge_ai_adapters.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::perception;
using namespace epidemic::gameplay::knowledge;
using namespace epidemic::gameplay::ai;
using namespace epidemic::gameplay::integration;

#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr);                                   \
            std::abort();                                                                                              \
        }                                                                                              \
    } while (false)

namespace
{
const AIInputValue *FindInput(const AITargetCandidate &candidate, AIInputKeyId key)
{
    const auto it = std::find_if(candidate.inputs.begin(), candidate.inputs.end(),
                                 [&](const AIInputValue &input) { return input.key == key; });
    return it == candidate.inputs.end() ? nullptr : &*it;
}

const AITargetCandidate *FindTarget(const AIContextSnapshot &context, GameplayObjectRef target)
{
    const auto it = std::find_if(context.targets.begin(), context.targets.end(),
                                 [&](const AITargetCandidate &candidate) { return candidate.target == target; });
    return it == context.targets.end() ? nullptr : &*it;
}
} // namespace

int main()
{
    const GameplayObjectRef npc{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("npc")};
    const GameplayObjectRef player{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("player")};
    const GameplayObjectRef remembered{GameplayDomainId::FromString("test.entity"),
                                       GameplayObjectId::FromString("remembered")};
    const GameplayObjectRef materialized_agent{
        GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("materialized_agent")};
    const GameplayObjectRef projected_agent{
        GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("projected_agent")};

    PerceptionService perception;
    const auto hearing = SenseTypeId::FromString("framework.sense.hearing");
    SenseDefinition hearing_definition;
    hearing_definition.id = hearing;
    hearing_definition.canonical_name = "framework.sense.hearing";
    hearing_definition.evaluation_model = SenseEvaluationModel::Hearing;
    hearing_definition.base_range_mm = 20'000;
    CHECK(perception.RegisterSense(hearing_definition));

    PerceiverProfileDefinition perception_profile;
    perception_profile.id = PerceiverProfileId::FromString("game.npc");
    perception_profile.canonical_name = "game.npc";
    perception_profile.senses = {hearing};
    CHECK(perception.RegisterProfileDefinition(perception_profile));
    perception.SetTemporalPolicy(PerceptionTemporalPolicy{GameplayDuration{5}, GameplayDuration{1}, 100'000});
    perception.Freeze();
    CHECK(perception.RegisterPerceiver(npc, perception_profile.id));

    PerceptionStimulus stimulus;
    stimulus.sense = hearing;
    stimulus.source = player;
    stimulus.strength_micro = 1'000'000;
    stimulus.position = {1000, 0, 0};
    stimulus.tags.Add(TagId::FromString("game.sound.footstep"));
    const auto stimulus_id = perception.CreateStimulus(stimulus);
    CHECK(stimulus_id);

    PerceiverEvaluationSample sample;
    sample.subject = npc;
    sample.position = {0, 0, 0};
    sample.materialized = true;
    sample.runtime_projection_available = true;
    const auto observations = perception.ProcessStimulus(
        stimulus_id.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {sample}});
    CHECK(observations && !observations.Value().empty());
    const auto observation = observations.Value().front();
    CHECK(observation.perceived_subject == player);

    KnowledgeService knowledge;
    KnowledgeProfile knowledge_profile;
    knowledge_profile.subject = npc;
    CHECK(knowledge.CreateProfile(knowledge_profile));

    PerceptionKnowledgeMapping mapping;
    mapping.observed_topic = KnowledgeTopicId::FromString("game.enemy_visible");
    PerceptionKnowledgeAdapter perception_knowledge{knowledge, mapping};
    const auto learned = perception_knowledge.LearnFromObservation(observation);
    CHECK(learned);
    CHECK(learned.Value().knowledge.IsValid());
    CHECK(learned.Value().memory.has_value());
    CHECK(!learned.Value().memory_error.has_value());

    const auto *learned_record = knowledge.FindKnowledge(learned.Value().knowledge);
    CHECK(learned_record != nullptr);
    CHECK(learned_record->subject == player);
    CHECK(learned_record->source == player);
    CHECK(!learned_record->payload.empty());

    // Memory is explicitly best-effort: failure is visible but does not erase valid Knowledge.
    PerceptionKnowledgeMapping broken_memory_mapping = mapping;
    broken_memory_mapping.observation_memory = {};
    PerceptionKnowledgeAdapter best_effort_memory{knowledge, broken_memory_mapping};
    const auto learned_without_memory = best_effort_memory.LearnFromObservation(observation);
    CHECK(learned_without_memory);
    CHECK(learned_without_memory.Value().knowledge.IsValid());
    CHECK(!learned_without_memory.Value().memory.has_value());
    CHECK(learned_without_memory.Value().memory_error.has_value());

    // Anonymous perception remains learnable without leaking the authoritative stimulus source.
    auto anonymous = observation;
    anonymous.perceived_subject = {};
    anonymous.identity_confidence_micro = 0;
    anonymous.position_uncertainty_mm = 2500;
    const auto anonymous_learning = perception_knowledge.LearnFromObservation(anonymous);
    CHECK(anonymous_learning && anonymous_learning.Value().knowledge.IsValid());
    const auto *anonymous_record = knowledge.FindKnowledge(anonymous_learning.Value().knowledge);
    CHECK(anonymous_record != nullptr);
    CHECK(!anonymous_record->subject.IsValid());
    CHECK(!anonymous_record->source.IsValid());
    CHECK(!anonymous_record->topic.primary_subject.IsValid());
    CHECK(anonymous_record->epistemic_state == KnowledgeEpistemicState::Suspected);

    // A last-known record stays knowledge, not current perception.
    KnowledgeTopic last_known_topic;
    last_known_topic.id = KnowledgeTopicId::FromString("framework.knowledge.last_known_position");
    last_known_topic.primary_subject = remembered;
    LearnKnowledgeRequest last_known;
    last_known.learner = npc;
    last_known.type = BeliefTypeId::FromString("framework.knowledge.location");
    last_known.topic = last_known_topic;
    last_known.source_kind = KnowledgeSourceId::FromString("framework.knowledge.memory");
    last_known.source_object = remembered;
    last_known.assertion = KnowledgeAssertionValue::Affirmed;
    last_known.epistemic_state = KnowledgeEpistemicState::Known;
    last_known.confidence = KnowledgeConfidence::Medium;
    last_known.context.time = GameplayTimePoint{1};
    CHECK(knowledge.Learn(last_known));

    AIService ai;
    const auto goal = AIGoalId::FromString("game.attack_known_enemy");
    const auto intent_type = AIIntentTypeId::FromString("game.use_ability");
    AIGoalDefinition goal_definition;
    goal_definition.id = goal;
    goal_definition.canonical_name = "game.attack_known_enemy";
    goal_definition.intent_type = intent_type;
    goal_definition.target_policy = AITargetPolicy::Required;
    AIConsideration known;
    known.id = AIConsiderationId::FromString("game.knows_enemy");
    known.evaluator = AIService::InputEvaluatorId();
    known.input_key = AIInputKeyId::FromType(TypeId{mapping.observed_topic.value.Raw()});
    known.scope = AIConsiderationScope::Target;
    goal_definition.considerations = {known};
    goal_definition.base_priority_micro = 5000;
    CHECK(ai.RegisterGoal(goal_definition));

    AIProfile ai_profile;
    ai_profile.id = AIProfileId::FromString("game.combatant");
    ai_profile.canonical_name = "game.combatant";
    ai_profile.default_goals = {goal};
    ai_profile.think_interval = GameplayDuration{1};
    CHECK(ai.RegisterProfile(ai_profile));

    const auto availability_goal = AIGoalId::FromString("game.availability_probe");
    AIGoalDefinition availability_goal_definition;
    availability_goal_definition.id = availability_goal;
    availability_goal_definition.canonical_name = "game.availability_probe";
    availability_goal_definition.intent_type = AIIntentTypeId::FromString("game.availability_intent");
    availability_goal_definition.target_policy = AITargetPolicy::Targetless;
    availability_goal_definition.base_priority_micro = 1;
    CHECK(ai.RegisterGoal(availability_goal_definition));

    AIProfile materialized_profile;
    materialized_profile.id = AIProfileId::FromString("game.requires_materialized");
    materialized_profile.canonical_name = "game.requires_materialized";
    materialized_profile.default_goals = {availability_goal};
    materialized_profile.materialization_policy = AIMaterializationPolicy::RequiresMaterialized;
    CHECK(ai.RegisterProfile(materialized_profile));

    AIProfile projected_ai_profile;
    projected_ai_profile.id = AIProfileId::FromString("game.requires_projection");
    projected_ai_profile.canonical_name = "game.requires_projection";
    projected_ai_profile.default_goals = {availability_goal};
    projected_ai_profile.materialization_policy = AIMaterializationPolicy::RequiresRuntimeProjection;
    CHECK(ai.RegisterProfile(projected_ai_profile));

    CHECK(ai.Freeze());
    CHECK(ai.RegisterAgent(npc, ai_profile.id));
    CHECK(ai.RegisterAgent(materialized_agent, materialized_profile.id));
    CHECK(ai.RegisterAgent(projected_agent, projected_ai_profile.id));

    KnowledgeAIInputKeys keys;
    KnowledgeAIAdapter knowledge_ai{knowledge, keys};

    // H06: composition-provided neutral availability drives AI materialization gates.
    const auto materialized_unavailable =
        knowledge_ai.BuildContext(materialized_agent, AIExecutionAvailability{false, false}, nullptr,
                                  GameplayTimePoint{1});
    CHECK(!materialized_unavailable.materialized && !materialized_unavailable.runtime_projection_available);
    const auto materialized_blocked = ai.Think(materialized_agent, materialized_unavailable, {});
    CHECK(materialized_blocked && materialized_blocked.Value().deferred);
    CHECK(materialized_blocked.Value().defer_reason == AIThinkDeferReason::MaterializationUnavailable);

    const auto materialized_available =
        knowledge_ai.BuildContext(materialized_agent, AIExecutionAvailability{true, false}, nullptr,
                                  GameplayTimePoint{1});
    CHECK(materialized_available.materialized && !materialized_available.runtime_projection_available);
    const auto materialized_think = ai.Think(materialized_agent, materialized_available, {});
    CHECK(materialized_think && materialized_think.Value().intent.has_value());

    const auto projection_unavailable =
        knowledge_ai.BuildContext(projected_agent, AIExecutionAvailability{true, false}, nullptr,
                                  GameplayTimePoint{1});
    const auto projection_blocked = ai.Think(projected_agent, projection_unavailable, {});
    CHECK(projection_blocked && projection_blocked.Value().deferred);
    CHECK(projection_blocked.Value().defer_reason == AIThinkDeferReason::MaterializationUnavailable);

    const auto projection_available =
        knowledge_ai.BuildContext(projected_agent, AIExecutionAvailability{true, true}, nullptr,
                                  GameplayTimePoint{1});
    CHECK(projection_available.materialized && projection_available.runtime_projection_available);
    const auto projection_think = ai.Think(projected_agent, projection_available, {});
    CHECK(projection_think && projection_think.Value().intent.has_value());

    const auto knowledge_only_context = knowledge_ai.BuildContext(
        npc, AIExecutionAvailability{}, nullptr, GameplayTimePoint{2});
    const auto *known_player = FindTarget(knowledge_only_context, player);
    const auto *last_known_target = FindTarget(knowledge_only_context, remembered);
    CHECK(known_player != nullptr && known_player->source == AITargetSource::Known);
    CHECK(last_known_target != nullptr && last_known_target->source == AITargetSource::LastKnown);
    CHECK(FindInput(*known_player, keys.knowledge_source) != nullptr);
    CHECK(FindInput(*known_player, keys.current_perception) == nullptr);

    const auto first_think = ai.Think(npc, knowledge_only_context, {});
    CHECK(first_think && first_think.Value().intent && first_think.Value().intent->target == player);

    AIIntentExecutionRecorder recorder{1};
    CHECK(recorder.Accept(*first_think.Value().intent, ai));
    CHECK(recorder.AcceptedIntents().size() == 1);
    CHECK(recorder.Succeed(first_think.Value().intent->id, ai));

    // At time 3 the observation is still active and is merged with Knowledge into one candidate.
    const auto current_context = knowledge_ai.BuildContext(
        npc, AIExecutionAvailability{true, true}, &perception, GameplayTimePoint{3});
    const auto *current_player = FindTarget(current_context, player);
    CHECK(current_player != nullptr);
    CHECK(current_player->source == AITargetSource::Perceived);
    CHECK(FindInput(*current_player, keys.current_perception) != nullptr);
    CHECK(FindInput(*current_player, keys.knowledge_source) != nullptr);
    CHECK(FindInput(*current_player, keys.perception_confidence) != nullptr);
    CHECK(FindInput(*current_player, keys.identity_confidence) != nullptr);
    CHECK(FindInput(*current_player, keys.position_uncertainty) != nullptr);
    CHECK(FindInput(*current_player, keys.perception_sense) != nullptr);
    CHECK(FindInput(*current_player, keys.perception_semantics) != nullptr);
    CHECK(std::count_if(current_context.targets.begin(), current_context.targets.end(),
                        [&](const AITargetCandidate &candidate) { return candidate.target == player; }) == 1);

    // M29: read validity is evaluated against requested time even if Perception::AdvanceTime was not called.
    const auto expired_context = knowledge_ai.BuildContext(
        npc, AIExecutionAvailability{true, true}, &perception, GameplayTimePoint{6});
    const auto *expired_player = FindTarget(expired_context, player);
    CHECK(expired_player != nullptr);
    CHECK(expired_player->source == AITargetSource::Known);
    CHECK(FindInput(*expired_player, keys.current_perception) == nullptr);
    CHECK(FindInput(*expired_player, keys.knowledge_source) != nullptr);

    // Recorder retention is diagnostic only and bounded; AIService remains authoritative.
    const auto second_think = ai.Think(npc, expired_context, {});
    CHECK(second_think && second_think.Value().intent);
    CHECK(recorder.Accept(*second_think.Value().intent, ai));
    CHECK(recorder.AcceptedIntents().size() == 1);
    CHECK(recorder.AcceptedIntents().front().id == second_think.Value().intent->id);

    return 0;
}
