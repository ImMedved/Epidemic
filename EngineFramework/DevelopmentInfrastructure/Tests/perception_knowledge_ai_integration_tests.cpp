#include "Epidemic/GameFramework/PerceptionKnowledgeAIIntegration/perception_knowledge_ai_adapters.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::perception;
using namespace epidemic::gameplay::knowledge;
using namespace epidemic::gameplay::ai;
using namespace epidemic::gameplay::integration;
int main()
{
    PerceptionService p;
    auto hearing = SenseTypeId::FromString("framework.sense.hearing");
    SenseDefinition hd;
    hd.id = hearing;
    hd.canonical_name = "framework.sense.hearing";
    hd.base_range_mm = 20000;
    if (!p.RegisterSense(hd))
        return 1;
    GameplayObjectRef npc{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("npc")};
    GameplayObjectRef player{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("player")};
    PerceiverProfile pp;
    pp.id = PerceiverProfileId::FromString("game.npc");
    pp.subject = npc;
    pp.canonical_name = "game.npc";
    pp.senses = {hearing};
    if (!p.RegisterProfile(pp))
        return 2;
    p.Freeze();
    PerceptionStimulus stim;
    stim.sense = hearing;
    stim.source = player;
    stim.strength_micro = 800000;
    auto sid = p.CreateStimulus(stim);
    if (!sid)
        return 3;
    auto obs = p.ProcessStimulus(sid.Value(), {1});
    if (!obs || obs.Value().empty())
        return 4;
    KnowledgeService k;
    KnowledgeProfile kp;
    kp.subject = npc;
    if (!k.CreateProfile(kp))
        return 5;
    PerceptionKnowledgeMapping map;
    map.observed_topic = KnowledgeTopicId::FromString("game.enemy_visible");
    PerceptionKnowledgeAdapter pka{k, map};
    auto kr = pka.LearnFromObservation(obs.Value()[0]);
    if (!kr)
        return 6;
    AIService ai;
    auto goal = AIGoalId::FromString("game.attack_known_enemy");
    AIIntentTypeId intent = AIIntentTypeId::FromString("game.use_ability");
    AIGoalDefinition gd;
    gd.id = goal;
    gd.canonical_name = "game.attack_known_enemy";
    gd.intent_type = intent;
    gd.required_topic = TypeId{map.observed_topic.value.Raw()};
    gd.base_priority_micro = 5000;
    if (!ai.RegisterGoal(gd))
        return 7;
    AIProfile ap;
    ap.id = AIProfileId::FromString("game.combatant");
    ap.canonical_name = "game.combatant";
    ap.default_goals = {goal};
    if (!ai.RegisterProfile(ap))
        return 8;
    ai.Freeze();
    if (!ai.RegisterAgent(npc, ap.id))
        return 9;
    KnowledgeAIAdapter kai{k};
    auto ctx = kai.BuildContext(npc, &p, {2});
    auto think = ai.Think(npc, ctx, {});
    if (!think || !think.Value().intent)
        return 10;
    AIIntentExecutionLog log;
    if (!log.Accept(*think.Value().intent, ai))
        return 11;
    if (log.AcceptedIntents().size() != 1)
        return 12;
    if (!log.Succeed(think.Value().intent->id, ai))
        return 13;
    return 0;
}
