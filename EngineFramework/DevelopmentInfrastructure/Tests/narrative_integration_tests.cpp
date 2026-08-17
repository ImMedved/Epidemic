#include "Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::narrative;
using namespace epidemic::gameplay::narrative_integration;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *domain, const char *id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}
class DiscoveryResolver final : public INarrativeConditionResolver
{
  public:
    NarrativeConditionResult Evaluate(const NarrativeConditionDefinition &d,
                                      const NarrativeEvaluationContext &c) const override
    {
        return {d.id,
                c.event.tags.HasExact(TagId::FromString("narrative.discovery")) ? ConditionEvaluationState::Satisfied
                                                                                : ConditionEvaluationState::Unsatisfied,
                1'000'000,
                {},
                {}};
    }
};
} // namespace

int main()
{
    NarrativeService service;
    DiscoveryResolver discovery;
    NarrativeConsequenceRecorder recorder;
    auto condition_type = NarrativeConditionTypeId::FromString("condition.discovery");
    auto consequence_type = NarrativeConsequenceTypeId::FromString("narrative.external_reward");
    Check(static_cast<bool>(service.RegisterConditionResolver(condition_type, discovery)), "condition resolver");
    Check(static_cast<bool>(service.RegisterConsequenceHandler(consequence_type, recorder)), "recorder handler");
    auto thread = NarrativeThreadId::FromString("thread.corpse");
    auto objective = NarrativeObjectiveId::FromString("objective.investigate_corpse");
    auto beat = NarrativeBeatId::FromString("beat.corpse_discovered");
    auto condition = NarrativeConditionId::FromString("condition.corpse_discovered");
    auto consequence = NarrativeConsequenceId::FromString("consequence.notify_ai");
    NarrativeConditionDefinition cond;
    cond.id = condition;
    cond.type = condition_type;
    Check(static_cast<bool>(service.RegisterConditionDefinition(cond)), "condition");
    NarrativeConsequenceDefinition cons;
    cons.id = consequence;
    cons.type = consequence_type;
    Check(static_cast<bool>(service.RegisterConsequenceDefinition(cons)), "consequence");
    NarrativeBeatDefinition beat_def;
    beat_def.id = beat;
    beat_def.thread = thread;
    beat_def.activation_conditions.push_back(condition);
    beat_def.consequences.push_back(consequence);
    Check(static_cast<bool>(service.RegisterBeatDefinition(beat_def)), "beat");
    NarrativeObjectiveDefinition obj;
    obj.id = objective;
    obj.thread = thread;
    obj.start_conditions.push_back(condition);
    Check(static_cast<bool>(service.RegisterObjectiveDefinition(obj)), "objective");
    NarrativeThreadDefinition t;
    t.id = thread;
    t.beats.push_back(beat);
    t.root_objectives.push_back(objective);
    Check(static_cast<bool>(service.RegisterThreadDefinition(t)), "thread");
    Check(static_cast<bool>(service.FreezeDefinitions()), "freeze");

    auto player = Ref("game.actor", "player");
    auto corpse = Ref("entity", "corpse");
    auto forest = Ref("world.area", "forest");
    NarrativeSemanticEventAdapter events;
    Check(static_cast<bool>(events.ProcessDiscovery(service, player, corpse, forest, TypeId::FromString("topic.corpse"),
                                                    {.time = GameplayTimePoint{100},
                                                     .correlation = CorrelationId::FromString("corpse.discovery"),
                                                     .actor = player})),
          "process discovery adapter");
    Check(service.FindClues(player, TypeId::FromString("topic.corpse")).size() == 1, "knowledge adapter created clue");
    Check(service.GetThreadState(thread) != nullptr &&
              service.GetThreadState(thread)->state == NarrativeRuntimeState::Active,
          "thread active from adapter");
    Check(service.GetObjectiveState(objective) != nullptr &&
              service.GetObjectiveState(objective)->state == NarrativeObjectiveRuntimeState::Active,
          "objective active from adapter");
    Check(service.ExecutePendingConsequences({.default_owner = player, .now = GameplayTimePoint{110}}, 8).size() == 1,
          "semantic consequence executed");
    Check(recorder.Recorded().size() == 1, "recorder stores semantic output");

    NarrativeKnowledgeAdapter knowledge;
    auto rumor = knowledge.CreateRumorFromSharedKnowledge(service, player, TypeId::FromString("topic.destroyed_bridge"),
                                                          500'000, GameplayDuration{10},
                                                          {.time = GameplayTimePoint{200}, .actor = player});
    Check(static_cast<bool>(rumor), "create rumor from shared knowledge");
    Check(service.FindRumors(player, TypeId::FromString("topic.destroyed_bridge")).size() == 1, "rumor query");
    auto expired = service.ExpireRumors(GameplayTimePoint{211}, {.time = GameplayTimePoint{211}, .actor = player});
    Check(expired.size() == 1, "rumor expiration");
    return 0;
}
