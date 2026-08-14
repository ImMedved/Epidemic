#include "Epidemic/GameFramework/NeedsLife/needs_life.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::needs_life;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
}

int main()
{
    NeedsLifeService service;
    NeedDefinition hunger;
    hunger.id=NeedTypeId::FromString("need.hunger");
    hunger.default_value=100;
    hunger.min_value=0;
    hunger.max_value=100;
    hunger.decay_per_tick=1;
    Check(static_cast<bool>(service.RegisterNeedDefinition(hunger)),"register hunger");

    auto npc=Ref("entities","npc.worker");
    NeedProfile profile;
    profile.subject=npc;
    profile.active_needs.push_back(hunger.id);
    auto profile_id=service.CreateNeedProfile(profile,{.time=GameplayTimePoint{0}});
    Check(static_cast<bool>(profile_id),"create profile");
    auto evaluated=service.EvaluateNeed(npc,hunger.id,GameplayTimePoint{100});
    Check(evaluated.value==0,"lazy decay");
    Check(evaluated.threshold==NeedThreshold::Critical,"critical threshold");
    Check(static_cast<bool>(service.CommitNeedEvaluation(npc,hunger.id,GameplayTimePoint{100})),"commit evaluation");
    Check(service.FindCriticalNeeds(GameplayTimePoint{100}).size()==1,"find critical");

    SatisfyNeedRequest satisfy;
    satisfy.subject=npc;
    satisfy.need=hunger.id;
    satisfy.amount=80;
    satisfy.context.time=GameplayTimePoint{100};
    Check(static_cast<bool>(service.SatisfyNeed(satisfy)),"satisfy need");
    Check(service.GetCommittedNeedState(npc,hunger.id)->threshold==NeedThreshold::Satisfied,"satisfied threshold");

    LifePressure pressure;
    pressure.subject=npc;
    pressure.type=TypeId::FromString("pressure.safety");
    pressure.urgency=500;
    auto pressure_id=service.CreateLifePressure(pressure);
    Check(static_cast<bool>(pressure_id),"create pressure");
    Check(service.FindLifePressures(npc).size()==1,"find pressure");
    Check(static_cast<bool>(service.ResolveLifePressure(pressure_id.Value())),"resolve pressure");
    Check(service.FindLifePressures(npc).empty(),"pressure resolved");

    LifeRoutine routine;
    routine.subject=npc;
    RoutineEntry entry;
    entry.start=GameplayTimePoint{800};
    entry.duration=GameplayDuration{400};
    entry.routine_type=TypeId::FromString("routine.work");
    routine.entries.push_back(entry);
    auto routine_id=service.SetRoutine(routine);
    Check(static_cast<bool>(routine_id),"set routine");
    Check(service.GetRoutine(npc)!=nullptr,"get routine");

    auto snapshot=service.CaptureSnapshot();
    NeedsLifeService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore");
    Check(restored.GetCommittedNeedState(npc,hunger.id)!=nullptr,"restore need state");
    Check(static_cast<bool>(restored.SimulateLifeInterval({npc,GameplayTimePoint{100},GameplayTimePoint{110},TypeId::FromString("coarse"),{.time=GameplayTimePoint{110}}})),"simulate interval");
    return 0;
}
