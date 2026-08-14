#include "Epidemic/GameFramework/Encounters/encounters.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::encounters;
using epidemic::gameplay::random::RandomSeed;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
}

int main()
{
    EncountersService service;
    SpawnTable table;
    table.id=SpawnTableId::FromString("wolves.table");
    table.roll_policy=SpawnRollPolicy::WeightedOne;
    SpawnEntry wolf;
    wolf.id=SpawnEntryId::FromString("wolf");
    wolf.archetype=TypeId::FromString("entity.wolf");
    wolf.weight=10;
    wolf.min_count=2;
    wolf.max_count=2;
    table.entries.push_back(wolf);
    SpawnEntry bear;
    bear.id=SpawnEntryId::FromString("bear");
    bear.archetype=TypeId::FromString("entity.bear");
    bear.weight=1;
    table.entries.push_back(bear);
    Check(static_cast<bool>(service.RegisterSpawnTable(table)),"register table");

    EncounterDefinition def;
    def.id=EncounterDefinitionId::FromString("forest.encounter");
    def.spawn_table=table.id;
    def.persistence=SpawnPersistencePolicy::Session;
    Check(static_cast<bool>(service.RegisterEncounterDefinition(def)),"register encounter");

    SpawnPoint point;
    point.area=Ref("world.area","forest");
    auto point_id=service.AddSpawnPoint(point);
    Check(static_cast<bool>(point_id),"add spawn point");
    Check(service.FindSpawnPointsInArea(point.area).size()==1,"find spawn point");

    SpawnRequest request;
    request.encounter=def.id;
    request.area=point.area;
    request.spawn_point=point_id.Value();
    request.seed=RandomSeed{123};
    auto first=service.SpawnEncounter(request);
    auto second_service=EncountersService{};
    Check(static_cast<bool>(second_service.RegisterSpawnTable(table)),"register table second");
    Check(static_cast<bool>(second_service.RegisterEncounterDefinition(def)),"register def second");
    auto second=second_service.SpawnEncounter(request);
    Check(first.state==SpawnResultState::Succeeded,"spawn succeeded");
    Check(first.spawned_records.size()==second.spawned_records.size(),"deterministic count");
    Check(first.created_entities.empty(),"encounters must not fabricate entity refs");
    auto real_entity=Ref("entities","wolf.1");
    Check(static_cast<bool>(service.BindSpawnedEntity(first.spawned_records.front(),real_entity)),"bind spawned entity");
    Check(service.GetSpawnedEntityRecord(first.spawned_records.front())->entity==real_entity,"bound real entity");
    Check(service.FindActiveEncountersInArea(point.area).size()==1,"active encounter query");
    Check(static_cast<bool>(service.CompleteEncounter(first.encounter_instance)),"complete encounter");
    Check(service.FindActiveEncountersInArea(point.area).empty(),"no active after complete");

    RespawnRule rule;
    rule.encounter=def.id;
    rule.delay=GameplayDuration{300};
    auto respawn=service.ScheduleRespawn(rule);
    Check(static_cast<bool>(respawn),"schedule respawn");

    auto snapshot=service.CaptureSnapshot();
    EncountersService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore");
    Check(restored.GetDiagnostics().respawn_schedules==0,"restore diagnostics are runtime-only");
    Check(restored.GetEncounterInstance(first.encounter_instance)!=nullptr,"restore encounter");
    return 0;
}

