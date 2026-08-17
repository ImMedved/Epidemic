#include "Epidemic/GameFramework/AI/ai.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ai;
int main()
{
    AIService ai;
    auto intent = AIIntentTypeId::FromString("game.investigate");
    auto topic = TypeId::FromString("game.noise");
    AIGoalDefinition g;
    g.id = AIGoalId::FromString("game.investigate_noise");
    g.canonical_name = "game.investigate_noise";
    g.intent_type = intent;
    g.base_priority_micro = 1000;
    g.required_topic = topic;
    if (!ai.RegisterGoal(g))
        return 1;
    AIProfile p;
    p.id = AIProfileId::FromString("game.guard");
    p.canonical_name = "game.guard";
    p.default_goals = {g.id};
    if (!ai.RegisterProfile(p))
        return 2;
    ai.Freeze();
    GameplayObjectRef guard{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("guard")};
    if (!ai.RegisterAgent(guard, p.id, {10}))
        return 3;
    AIContextSnapshot empty;
    auto none = ai.Think(guard, empty, {});
    if (!none || none.Value().intent)
        return 4;
    AIContextSnapshot ctx;
    ctx.known_topics = {topic};
    GameplayObjectRef target{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("target")};
    ctx.perceived_targets = {target};
    auto thought = ai.Think(guard, ctx, {});
    if (!thought || !thought.Value().intent)
        return 5;
    if (thought.Value().intent->target != target)
        return 6;
    if (!ai.MarkIntentAccepted(thought.Value().intent->id))
        return 7;
    if (!ai.MarkIntentSucceeded(thought.Value().intent->id))
        return 8;
    if (ai.FindAgentsByActivity(AIAgentActivity::Idle).size() != 1)
        return 9;
    auto snap = ai.CaptureSnapshot();
    AIService restored;
    if (!restored.RestoreSnapshot(std::move(snap)))
        return 10;
    if (!restored.FindAgent(guard))
        return 11;
    AIService budgeted;
    if (!budgeted.RegisterGoal(g))
        return 12;
    if (!budgeted.RegisterProfile(p))
        return 13;
    budgeted.Freeze();
    GameplayObjectRef guard2{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("guard2")};
    if (!budgeted.RegisterAgent(guard, p.id) || !budgeted.RegisterAgent(guard2, p.id))
        return 14;
    budgeted.SetBudget({1, 4096, 512});
    GameplayContext tick1;
    tick1.tick = {1};
    GameplayContext tick2;
    tick2.tick = {2};
    auto b1 = budgeted.Think(guard, ctx, tick1);
    auto b2 = budgeted.Think(guard2, ctx, tick1);
    auto b3 = budgeted.Think(guard2, ctx, tick2);
    if (!b1 || !b1.Value().intent || !b2 || !b2.Value().deferred || !b3 || !b3.Value().intent)
        return 15;
    return 0;
}
