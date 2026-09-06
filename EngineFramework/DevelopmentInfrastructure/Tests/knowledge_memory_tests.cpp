#include "Epidemic/GameFramework/Knowledge/knowledge.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::knowledge;
int main()
{
    KnowledgeService k;
    GameplayObjectRef npc{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("npc")};
    GameplayObjectRef guard{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("guard")};
    GameplayObjectRef player{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("player")};
    KnowledgeProfile np;
    np.subject = npc;
    KnowledgeProfile gp;
    gp.subject = guard;
    if (!k.CreateProfile(np))
        return 1;
    if (!k.CreateProfile(gp))
        return 2;
    KnowledgeTopic topic;
    topic.id = KnowledgeTopicId::FromString("game.theft");
    topic.primary_subject = player;
    auto id = k.Learn({npc,
                       BeliefTypeId::FromString("game.witnessed"),
                       topic,
                       KnowledgeSourceId::FromString("direct"),
                       player,
                       KnowledgeConfidence::High,
                       {},
                       GameplayContext{GameplayTickId{77}, GameplayTimePoint{5}}});
    if (!id)
        return 3;
    const auto *learned = k.FindKnowledge(id.Value());
    if (!learned || learned->source_kind != KnowledgeSourceId::FromString("direct") || learned->source != player)
        return 14;
    if (learned->learned_at != GameplayTimePoint{5} || learned->last_confirmed_at != GameplayTimePoint{5})
        return 15;
    if (k.FindKnowledgeAboutSubject(npc, player).size() != 1)
        return 4;
    auto mem = k.CreateMemory({npc,
                               MemoryTypeId::FromString("game.saw"),
                               player,
                               {},
                               MemoryImportance::Low,
                               MemoryPersistencePolicy::Timed,
                               {10},
                               {},
                               GameplayContext{GameplayTickId{99}, GameplayTimePoint{6}}});
    if (!mem)
        return 5;
    const auto *memory = k.FindMemory(mem.Value());
    if (!memory || memory->time != GameplayTimePoint{6})
        return 16;
    auto shared = k.Share({npc, guard, id.Value(), KnowledgeShareMode::Report, {}});
    if (!shared)
        return 6;
    const auto guard_knowledge = k.FindKnowledgeByOwner(guard);
    if (guard_knowledge.empty())
        return 7;
    if (guard_knowledge.front().source_kind != KnowledgeSourceId::FromString("framework.knowledge.report") ||
        guard_knowledge.front().source != npc)
        return 17;
    if (!k.Decay({20}))
        return 8;
    if (!k.FindMemoriesByOwner(npc).empty())
        return 9;
    auto snap = k.CaptureSnapshot();
    KnowledgeService r;
    if (!r.CreateProfile(np))
        return 10;
    if (!r.CreateProfile(gp))
        return 11;
    if (!r.RestoreSnapshot(std::move(snap)))
        return 12;
    if (r.FindKnowledgeByOwner(npc).empty())
        return 13;
    return 0;
}
