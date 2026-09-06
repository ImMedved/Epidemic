#include "Epidemic/GameFramework/Perception/perception.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::perception;
int main()
{
    PerceptionService s;
    auto hearing = SenseTypeId::FromString("framework.sense.hearing");
    SenseDefinition hear;
    hear.id = hearing;
    hear.canonical_name = "framework.sense.hearing";
    hear.base_range_mm = 10000;
    if (!s.RegisterSense(hear))
        return 1;
    GameplayObjectRef npc{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("npc")};
    GameplayObjectRef player{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("player")};
    PerceiverProfile p;
    p.id = PerceiverProfileId::FromString("test.npc.perception");
    p.subject = npc;
    p.canonical_name = "test.npc.perception";
    p.senses = {hearing};
    if (!s.RegisterProfile(p))
        return 2;
    s.Freeze();
    PerceptionStimulus stim;
    stim.sense = hearing;
    stim.source = player;
    stim.position = {1000, 0, 0};
    stim.strength_micro = 900000;
    stim.created_at = {0};
    stim.lifetime = {10};
    auto sid = s.CreateStimulus(stim);
    if (!sid)
        return 3;
    auto out = s.ProcessStimulus(sid.Value(), {1});
    if (!out || out.Value().size() != 1)
        return 4;
    if (out.Value()[0].perceiver != npc)
        return 5;
    auto *aw = s.GetAwareness(npc, player);
    if (!aw || aw->level < AwarenessLevel::Aware)
        return 6;
    if (s.FindObservationsByPerceiver(npc).size() != 1)
        return 7;
    auto snap = s.CaptureSnapshot();
    PerceptionService r;
    if (!r.RegisterSense(hear))
        return 8;
    r.Freeze();
    if (!r.RestoreSnapshot(std::move(snap)))
        return 9;
    if (!r.GetAwareness(npc, player))
        return 10;
    if (!r.ExpireStimuli({20}))
        return 11;
    if (!r.FindStimuliInArea({}).empty())
        return 12;

    PerceptionService ranged;
    if (!ranged.RegisterSense(hear))
        return 13;
    if (!ranged.RegisterProfile(p))
        return 14;
    ranged.Freeze();
    PerceptionStimulus far_stim = stim;
    far_stim.position = {20000, 0, 0};
    auto far_sid = ranged.CreateStimulus(far_stim);
    if (!far_sid)
        return 15;
    auto far_out = ranged.ProcessStimulus(
        far_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {{npc, {0, 0, 0}}}});
    if (!far_out || !far_out.Value().empty())
        return 16;

    PerceptionService visual;
    auto vision = SenseTypeId::FromString("framework.sense.vision");
    SenseDefinition see;
    see.id = vision;
    see.canonical_name = "framework.sense.vision";
    see.base_range_mm = 10000;
    if (!visual.RegisterSense(see))
        return 17;
    PerceiverProfile vp = p;
    vp.id = PerceiverProfileId::FromString("test.npc.visual");
    vp.senses = {vision};
    if (!visual.RegisterProfile(vp))
        return 18;
    visual.Freeze();
    PerceptionStimulus visual_stim = stim;
    visual_stim.sense = vision;
    visual_stim.strength_micro = 1'000'000;
    visual_stim.position = {1000, 0, 0};
    auto visual_sid = visual.CreateStimulus(visual_stim);
    if (!visual_sid)
        return 19;
    auto visual_out = visual.ProcessStimulus(
        visual_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}, {{npc, {0, 0, 0}}}});
    if (!visual_out || visual_out.Value().size() != 1)
        return 20;
    auto visual_diag = visual.GetDiagnostics();
    if (visual_diag.visibility_tests != 1 || visual_diag.audibility_tests != 0)
        return 21;

    PerceptionService budgeted;
    if (!budgeted.RegisterSense(hear))
        return 22;
    if (!budgeted.RegisterProfile(p))
        return 23;
    budgeted.Freeze();
    budgeted.SetBudget({1, 256, 4096});
    auto s1 = budgeted.CreateStimulus(stim);
    auto s2 = budgeted.CreateStimulus(stim);
    if (!s1 || !s2)
        return 24;
    auto bo1 =
        budgeted.ProcessStimulus(s1.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}});
    auto bo2 =
        budgeted.ProcessStimulus(s2.Value(), PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{1}, {}});
    auto bo3 =
        budgeted.ProcessStimulus(s2.Value(), PerceptionProcessingContext{GameplayTickId{2}, GameplayTimePoint{2}, {}});
    if (!bo1 || bo1.Value().empty() || !bo2 || !bo2.Value().empty() || !bo3 || bo3.Value().empty())
        return 25;

    PerceptionService temporal;
    if (!temporal.RegisterSense(hear))
        return 26;
    if (!temporal.RegisterProfile(p))
        return 27;
    temporal.SetTemporalPolicy({GameplayDuration{5}, GameplayDuration{10}, 250'000});
    temporal.Freeze();
    PerceptionStimulus temporal_stim = stim;
    temporal_stim.strength_micro = 1'000'000;
    temporal_stim.position = {0, 0, 0};
    temporal_stim.lifetime = GameplayDuration{100};
    auto temporal_sid = temporal.CreateStimulus(temporal_stim);
    if (!temporal_sid)
        return 28;
    auto temporal_out = temporal.ProcessStimulus(
        temporal_sid.Value(),
        PerceptionProcessingContext{GameplayTickId{1}, GameplayTimePoint{10}, {}, {{npc, {0, 0, 0}}}});
    if (!temporal_out || temporal_out.Value().size() != 1)
        return 29;
    if (!temporal.ExpireStimuli(GameplayTimePoint{15}))
        return 30;
    if (!temporal.FindObservationsByPerceiver(npc).empty())
        return 31;
    const auto *temporal_awareness = temporal.GetAwareness(npc, player);
    if (!temporal_awareness || temporal_awareness->level != AwarenessLevel::Aware)
        return 32;
    if (!temporal.ExpireStimuli(GameplayTimePoint{30}))
        return 33;
    temporal_awareness = temporal.GetAwareness(npc, player);
    if (!temporal_awareness || temporal_awareness->level != AwarenessLevel::Lost ||
        temporal_awareness->suspicion_micro != 0)
        return 34;
    if (!temporal.ExpireStimuli(GameplayTimePoint{40}))
        return 35;
    temporal_awareness = temporal.GetAwareness(npc, player);
    if (!temporal_awareness || temporal_awareness->level != AwarenessLevel::Unaware)
        return 36;
    return 0;
}
