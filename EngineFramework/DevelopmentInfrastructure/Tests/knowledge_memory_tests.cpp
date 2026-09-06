#include "Epidemic/GameFramework/Knowledge/knowledge.h"

#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::knowledge;

namespace
{
GameplayObjectRef Ref(std::string_view name)
{
    return {GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString(name)};
}
MemoryDecayRule Rule()
{
    MemoryDecayRule r;
    r.id = MemoryDecayRuleId::FromString("test.decay.standard");
    r.canonical_name = "test.decay.standard";
    r.interval = GameplayDuration{10};
    r.confidence_steps = 1;
    r.outdated_at_or_below = KnowledgeConfidence::Low;
    return r;
}
KnowledgeTopic Topic(std::string_view id, GameplayObjectRef subject, std::optional<TagId> tag = std::nullopt)
{
    KnowledgeTopic t;
    t.id = KnowledgeTopicId::FromString(id);
    t.primary_subject = subject;
    if (tag)
        t.tags.Add(*tag);
    return t;
}
LearnKnowledgeRequest LearnReq(GameplayObjectRef owner, BeliefTypeId type, KnowledgeTopic topic,
                               GameplayObjectRef source, KnowledgeAssertionValue assertion,
                               KnowledgeEpistemicState state, KnowledgeConfidence confidence, GameplayTimePoint time)
{
    LearnKnowledgeRequest r;
    r.learner = owner;
    r.type = type;
    r.topic = std::move(topic);
    r.source_kind = KnowledgeSourceId::FromString("test.source.direct");
    r.source_object = source;
    r.assertion = assertion;
    r.epistemic_state = state;
    r.confidence = confidence;
    r.context.time = time;
    return r;
}
}

int main()
{
    const auto npc = Ref("npc");
    const auto guard = Ref("guard");
    const auto clerk = Ref("clerk");
    const auto player = Ref("player");
    const auto stranger = Ref("stranger");
    const auto allowed_tag = TagId::FromString("knowledge.share.allowed");

    KnowledgeService k(8);
    auto decay = Rule();
    if (!k.RegisterDecayRule(decay) || !k.FreezeDefinitions())
        return 1;

    KnowledgeProfile np;
    np.subject = npc;
    np.default_decay_rule = decay.id;
    np.sharing_tags.Add(allowed_tag);
    KnowledgeProfile gp;
    gp.subject = guard;
    gp.default_decay_rule = decay.id;
    KnowledgeProfile cp;
    cp.subject = clerk;
    cp.default_decay_rule = decay.id;
    if (!k.CreateProfile(np) || !k.CreateProfile(gp) || !k.CreateProfile(cp))
        return 2;

    const auto belief = BeliefTypeId::FromString("game.witnessed");
    auto denied = k.Learn(LearnReq(npc, belief, Topic("game.door_open", player, allowed_tag), player,
                                   KnowledgeAssertionValue::Denied, KnowledgeEpistemicState::Known,
                                   KnowledgeConfidence::Certain, GameplayTimePoint{5}));
    if (!denied)
        return 3;
    const auto* first = k.FindKnowledge(denied.Value());
    if (!first || first->assertion != KnowledgeAssertionValue::Denied ||
        first->epistemic_state != KnowledgeEpistemicState::Known || first->confidence != KnowledgeConfidence::Certain)
        return 4;

    auto weak_confirmation = k.Learn(LearnReq(npc, belief, Topic("game.door_open", player, allowed_tag), player,
                                               KnowledgeAssertionValue::Denied, KnowledgeEpistemicState::Rumor,
                                               KnowledgeConfidence::Low, GameplayTimePoint{6}));
    if (!weak_confirmation || weak_confirmation.Value() != denied.Value())
        return 5;
    first = k.FindKnowledge(denied.Value());
    if (!first || first->epistemic_state != KnowledgeEpistemicState::Known ||
        first->confidence != KnowledgeConfidence::Certain)
        return 6;

    auto opposite = k.Learn(LearnReq(npc, belief, Topic("game.door_open", player, allowed_tag), player,
                                     KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Suspected,
                                     KnowledgeConfidence::Medium, GameplayTimePoint{7}));
    if (!opposite || opposite.Value() == denied.Value() || k.FindKnowledgeByTopic(npc, KnowledgeTopicId::FromString("game.door_open")).size() != 2)
        return 7;

    if (k.Share({stranger, guard, denied.Value(), KnowledgeShareMode::Report, {}}))
        return 8;
    auto shared = k.Share({npc, guard, denied.Value(), KnowledgeShareMode::Report, GameplayContext{.time = GameplayTimePoint{8}}});
    if (!shared)
        return 9;
    const auto* guard_record = k.FindKnowledge(shared.Value());
    if (!guard_record || guard_record->assertion != KnowledgeAssertionValue::Denied ||
        guard_record->derived_from != denied.Value() || guard_record->transmission_depth != 1 ||
        guard_record->original_source != player)
        return 10;
    auto reshared = k.Share({guard, clerk, shared.Value(), KnowledgeShareMode::Rumor, GameplayContext{.time = GameplayTimePoint{9}}});
    if (!reshared)
        return 11;
    const auto* clerk_record = k.FindKnowledge(reshared.Value());
    if (!clerk_record || clerk_record->assertion != KnowledgeAssertionValue::Denied ||
        clerk_record->epistemic_state != KnowledgeEpistemicState::Rumor || clerk_record->transmission_depth != 2 ||
        clerk_record->original_source != player)
        return 12;

    auto blocked = k.Learn(LearnReq(npc, belief, Topic("game.secret", player, TagId::FromString("knowledge.private")), player,
                                    KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known,
                                    KnowledgeConfidence::High, GameplayTimePoint{10}));
    if (!blocked || k.Share({npc, guard, blocked.Value(), KnowledgeShareMode::Tell, {}}))
        return 13;

    ContradictKnowledgeRequest contradiction;
    contradiction.old_record = opposite.Value();
    contradiction.new_assertion = KnowledgeAssertionValue::Denied;
    contradiction.epistemic_state = KnowledgeEpistemicState::Known;
    contradiction.confidence = KnowledgeConfidence::High;
    contradiction.source_kind = KnowledgeSourceId::FromString("test.source.correction");
    contradiction.source_object = guard;
    contradiction.context.time = GameplayTimePoint{11};
    auto corrected = k.Contradict(contradiction);
    if (!corrected)
        return 14;
    if (k.FindKnowledge(opposite.Value())->epistemic_state != KnowledgeEpistemicState::Contradicted ||
        k.FindKnowledge(corrected.Value())->assertion != KnowledgeAssertionValue::Denied)
        return 15;

    CreateMemoryRequest m1;
    m1.owner = npc;
    m1.type = MemoryTypeId::FromString("game.memory.one");
    m1.subject = player;
    m1.persistence = MemoryPersistencePolicy::Persistent;
    m1.context.time = GameplayTimePoint{12};
    auto mem1 = k.CreateMemory(m1);
    auto m2 = m1;
    m2.type = MemoryTypeId::FromString("game.memory.two");
    auto mem2 = k.CreateMemory(m2);
    if (!mem1 || !mem2)
        return 16;
    CreateMemoryRequest summary;
    summary.owner = npc;
    summary.type = MemoryTypeId::FromString("game.memory.summary");
    summary.subject = player;
    summary.persistence = MemoryPersistencePolicy::Persistent;
    summary.context.time = GameplayTimePoint{13};
    const MemoryRecordId compact_ids[] = {mem1.Value(), mem2.Value()};
    auto compact = k.CompactMemories(npc, compact_ids, summary, GameplayContext{.time = GameplayTimePoint{13}});
    if (!compact || k.FindMemory(mem1.Value()) || k.FindMemory(mem2.Value()) || !k.FindMemory(compact.Value()))
        return 17;

    KnowledgeService frequent;
    KnowledgeService sparse;
    auto dr = Rule();
    if (!frequent.RegisterDecayRule(dr) || !sparse.RegisterDecayRule(dr))
        return 18;
    if (!frequent.FreezeDefinitions() || !sparse.FreezeDefinitions())
        return 181;
    KnowledgeProfile fp;
    fp.subject = npc;
    fp.default_decay_rule = dr.id;
    if (!frequent.CreateProfile(fp) || !sparse.CreateProfile(fp))
        return 19;
    auto fr = frequent.Learn(LearnReq(npc, belief, Topic("game.decay", player), player,
                                      KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known,
                                      KnowledgeConfidence::Certain, GameplayTimePoint{0}));
    auto sr = sparse.Learn(LearnReq(npc, belief, Topic("game.decay", player), player,
                                    KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known,
                                    KnowledgeConfidence::Certain, GameplayTimePoint{0}));
    if (!fr || !sr)
        return 20;
    if (!frequent.Decay(GameplayTimePoint{10}) || !frequent.Decay(GameplayTimePoint{20}) ||
        !frequent.Decay(GameplayTimePoint{30}) || !sparse.Decay(GameplayTimePoint{30}))
        return 201;
    const auto* f = frequent.FindKnowledge(fr.Value());
    const auto* s = sparse.FindKnowledge(sr.Value());
    if (!f || !s || f->confidence != s->confidence || f->epistemic_state != s->epistemic_state || f->last_decay_at != s->last_decay_at)
        return 21;

    KnowledgeService retention;
    if (!retention.FreezeDefinitions())
        return 211;
    KnowledgeProfile session_profile;
    session_profile.subject = npc;
    session_profile.retention = MemoryPersistencePolicy::Session;
    if (!retention.CreateProfile(session_profile))
        return 22;
    auto transient_by_profile = retention.Learn(LearnReq(npc, belief, Topic("game.session", player), player,
                                                          KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known,
                                                          KnowledgeConfidence::High, GameplayTimePoint{1}));
    if (!transient_by_profile || !retention.CaptureSnapshot().knowledge.empty())
        return 23;

    auto snap = k.CaptureSnapshot();
    KnowledgeService restored;
    if (!restored.RegisterDecayRule(decay) || !restored.FreezeDefinitions() || !restored.RestoreSnapshot(snap))
        return 24;
    if (restored.FindKnowledgeByOwner(npc).empty() || restored.FindKnowledgeAboutSubject(npc, player).empty())
        return 25;

    KnowledgeService transactional;
    if (!transactional.RegisterDecayRule(decay) || !transactional.FreezeDefinitions())
        return 26;
    KnowledgeProfile sentinel;
    sentinel.subject = stranger;
    sentinel.default_decay_rule = decay.id;
    if (!transactional.CreateProfile(sentinel))
        return 27;
    auto corrupt = snap;
    corrupt.knowledge_ids.scope ^= 1;
    if (transactional.RestoreSnapshot(corrupt) || !transactional.FindProfile(stranger))
        return 28;
    corrupt = snap;
    if (!corrupt.knowledge.empty())
        corrupt.knowledge_ids.next = corrupt.knowledge.front().id.value.Low();
    if (transactional.RestoreSnapshot(corrupt) || !transactional.FindProfile(stranger))
        return 29;

    KnowledgeService bounded(2);
    if (!bounded.FreezeDefinitions())
        return 291;
    KnowledgeProfile bounded_profile;
    bounded_profile.subject = npc;
    if (!bounded.CreateProfile(bounded_profile))
        return 30;
    for (int i = 0; i < 3; ++i)
    {
        auto q = LearnReq(npc, BeliefTypeId::FromString(i == 0 ? "b.one" : i == 1 ? "b.two" : "b.three"),
                          Topic(i == 0 ? "t.one" : i == 1 ? "t.two" : "t.three", player), player,
                          KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known, KnowledgeConfidence::Medium,
                          GameplayTimePoint{i + 1});
        if (!bounded.Learn(std::move(q)))
            return 31;
    }
    auto batch = bounded.ReadChangesSince(0);
    if (!batch.snapshot_required || !batch.changes.empty())
        return 32;

    auto exhausted = snap;
    exhausted.knowledge.clear();
    exhausted.memories.clear();
    exhausted.knowledge_ids.next = 0;
    KnowledgeService no_ids;
    if (!no_ids.RegisterDecayRule(decay) || !no_ids.FreezeDefinitions() || !no_ids.RestoreSnapshot(exhausted))
        return 33;
    auto fail_id = no_ids.Learn(LearnReq(npc, belief, Topic("game.exhausted", player), player,
                                         KnowledgeAssertionValue::Affirmed, KnowledgeEpistemicState::Known,
                                         KnowledgeConfidence::High, GameplayTimePoint{20}));
    if (fail_id)
        return 34;

    if (!k.RemoveProfile(npc) || k.FindProfile(npc) || !k.FindKnowledgeByOwner(npc).empty() || !k.FindMemoriesByOwner(npc).empty())
        return 35;
    return 0;
}
