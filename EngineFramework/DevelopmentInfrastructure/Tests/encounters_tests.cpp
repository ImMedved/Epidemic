#include "Epidemic/GameFramework/Encounters/encounters.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::encounters;
using epidemic::gameplay::random::RandomSeed;

namespace
{
void Check(bool value, const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
GameplayTagSet Tags(std::initializer_list<const char*> names){GameplayTagSet set;for(auto* name:names)set.Add(TagId::FromString(name));return set;}
SpawnTable BasicTable(SpawnTableId id)
{
    SpawnTable table; table.id=id; table.roll_policy=SpawnRollPolicy::WeightedOne;
    SpawnEntry wolf; wolf.id=SpawnEntryId::FromString("wolf"); wolf.archetype=TypeId::FromString("entity.wolf"); wolf.weight=10; wolf.min_count=2; wolf.max_count=2; wolf.required_area_tags=Tags({"biome.forest"}); table.entries.push_back(wolf);
    SpawnEntry bear; bear.id=SpawnEntryId::FromString("bear"); bear.archetype=TypeId::FromString("entity.bear"); bear.weight=1; bear.required_area_tags=Tags({"biome.forest"}); table.entries.push_back(bear);
    return table;
}
EncounterDefinition BasicDefinition(EncounterDefinitionId id, SpawnTableId table)
{
    EncounterDefinition def; def.id=id; def.spawn_table=table; def.persistence=SpawnPersistencePolicy::Session; def.required_area_tags=Tags({"biome.forest"}); return def;
}
}

int main()
{
    const auto forest=Ref("world.area","forest");
    const auto clearing=Ref("world.position","forest.clearing");
    EncountersService service;
    auto table=BasicTable(SpawnTableId::FromString("wolves.table"));
    Check(static_cast<bool>(service.RegisterSpawnTable(table)),"register table");
    auto def=BasicDefinition(EncounterDefinitionId::FromString("forest.encounter"),table.id);
    Check(static_cast<bool>(service.RegisterEncounterDefinition(def)),"register encounter");

    SpawnPoint point; point.area=forest; point.position=clearing; point.tags=Tags({"spawn.ground"});
    auto point_id=service.AddSpawnPoint(point); Check(static_cast<bool>(point_id),"add spawn point");
    Check(service.FindSpawnPointsInArea(forest).size()==1,"find spawn point");

    SpawnRequest request; request.id=SpawnRequestId::FromRaw(0x2702,100); request.encounter=def.id; request.area=forest; request.spawn_point=point_id.Value(); request.area_tags=Tags({"biome.forest"}); request.seed=RandomSeed{123}; request.context.tick=GameplayTickId{1}; request.context.time=GameplayTimePoint{10};
    auto first=service.SpawnEncounter(request); Check(first.state==SpawnResultState::Succeeded,"spawn succeeded"); Check(!first.spawned_records.empty(),"spawn records");
    Check(service.GetEncounterInstance(first.encounter_instance)->origin==clearing,"spawn point determines semantic origin");
    auto repeated=service.SpawnEncounter(request); Check(repeated.encounter_instance==first.encounter_instance,"request idempotency");
    auto conflict=request; conflict.area=Ref("world.area","other"); Check(service.SpawnEncounter(conflict).state==SpawnResultState::Rejected,"request id conflict");

    auto real_entity=Ref("entities","wolf.1"); Check(static_cast<bool>(service.BindSpawnedEntity(first.spawned_records.front(),real_entity)),"bind spawned entity"); Check(service.GetSpawnedEntityRecord(first.spawned_records.front())->entity==real_entity,"bound entity");
    Check(static_cast<bool>(service.CompleteEncounter(first.encounter_instance)),"complete encounter");
    Check(service.FindActiveEncountersInArea(forest).empty(),"no active after complete");

    // Global and per-area encounter budgets are distinct and enforced.
    EncountersService budget_service; Check(static_cast<bool>(budget_service.RegisterSpawnTable(table)),"budget table"); Check(static_cast<bool>(budget_service.RegisterEncounterDefinition(def)),"budget def");
    EncounterBudgets budgets; budgets.max_active_global=1; budgets.max_active_per_area=1; budgets.max_spawn_operations_per_tick=16; budget_service.SetBudgets(budgets);
    SpawnRequest b1; b1.encounter=def.id; b1.area=forest; b1.area_tags=Tags({"biome.forest"}); b1.seed={1}; b1.context.tick=GameplayTickId{1};
    Check(budget_service.SpawnEncounter(b1).state==SpawnResultState::Succeeded,"first global budget spawn");
    SpawnRequest b2=b1; b2.id={}; b2.area=Ref("world.area","forest.2"); b2.seed={2}; Check(budget_service.SpawnEncounter(b2).state==SpawnResultState::Rejected,"global active budget applies across areas");

    // Spawn operation budget defers without mutating encounter state and can be retried on a later tick.
    EncountersService op_service; Check(static_cast<bool>(op_service.RegisterSpawnTable(table)),"op table"); Check(static_cast<bool>(op_service.RegisterEncounterDefinition(def)),"op def");
    EncounterBudgets op_budget; op_budget.max_spawn_operations_per_tick=1; op_service.SetBudgets(op_budget);
    SpawnRequest op; op.id=SpawnRequestId::FromRaw(0x2702,200); op.encounter=def.id; op.area=forest; op.area_tags=Tags({"biome.forest"}); op.seed={3}; op.context.tick=GameplayTickId{1};
    auto deferred=op_service.SpawnEncounter(op); Check(deferred.state==SpawnResultState::Deferred && !deferred.encounter_instance.IsValid(),"spawn operation budget defers before mutation");
    op_budget.max_spawn_operations_per_tick=8; op_service.SetBudgets(op_budget); op.context.tick=GameplayTickId{2};
    Check(op_service.SpawnEncounter(op).state==SpawnResultState::Succeeded,"deferred request can retry");

    // PickN count is data driven and respects area filters.
    EncountersService pick_service; SpawnTable pick=table; pick.id=SpawnTableId::FromString("pick.table"); pick.roll_policy=SpawnRollPolicy::PickNWithoutReplacement; pick.selection_min_count=1; pick.selection_max_count=1;
    SpawnEntry cave; cave.id=SpawnEntryId::FromString("cave"); cave.archetype=TypeId::FromString("entity.cave"); cave.required_area_tags=Tags({"biome.cave"}); pick.entries.push_back(cave);
    Check(static_cast<bool>(pick_service.RegisterSpawnTable(pick)),"pick table"); auto pick_def=BasicDefinition(EncounterDefinitionId::FromString("pick.encounter"),pick.id); Check(static_cast<bool>(pick_service.RegisterEncounterDefinition(pick_def)),"pick def");
    SpawnRequest pr; pr.encounter=pick_def.id; pr.area=forest; pr.area_tags=Tags({"biome.forest"}); pr.seed={10}; pr.context.tick=GameplayTickId{1};
    auto picked=pick_service.SpawnEncounter(pr); Check(picked.state==SpawnResultState::Succeeded,"pick spawn"); Check(picked.spawned_records.size()<=2,"pick uses one selected entry rather than hardcoded two entries");
    for(auto id:picked.spawned_records) Check(pick_service.GetSpawnedEntityRecord(id)->archetype!=TypeId::FromString("entity.cave"),"blocked filtered entry not selected");

    // Population backed encounters stay pending until every semantic slot is explicitly bound.
    EncountersService population_service; SpawnTable pop_table; pop_table.id=SpawnTableId::FromString("population.table"); pop_table.roll_policy=SpawnRollPolicy::PopulationBacked; SpawnEntry resident; resident.id=SpawnEntryId::FromString("resident"); resident.archetype=TypeId::FromString("entity.resident"); resident.min_count=2; resident.max_count=2; pop_table.entries.push_back(resident); Check(static_cast<bool>(population_service.RegisterSpawnTable(pop_table)),"population table");
    EncounterDefinition pop_def; pop_def.id=EncounterDefinitionId::FromString("population.encounter"); pop_def.spawn_table=pop_table.id; pop_def.persistence=SpawnPersistencePolicy::PopulationBacked; Check(static_cast<bool>(population_service.RegisterEncounterDefinition(pop_def)),"population def");
    SpawnRequest pop_req; pop_req.encounter=pop_def.id; pop_req.area=forest; pop_req.persistence=SpawnPersistencePolicy::PopulationBacked; pop_req.context.tick=GameplayTickId{1};
    auto pop_spawn=population_service.SpawnEncounter(pop_req); Check(pop_spawn.state==SpawnResultState::Deferred,"population spawn awaits binding"); Check(population_service.GetEncounterInstance(pop_spawn.encounter_instance)->state==EncounterState::AwaitingPopulationBinding,"awaiting population state");
    Check(static_cast<bool>(population_service.BindPopulationUnit(pop_spawn.spawned_records[0],Ref("population","u1"))),"bind pop 1");
    Check(!population_service.ActivatePopulationBackedEncounter(pop_spawn.encounter_instance),"cannot activate incomplete population binding");
    Check(static_cast<bool>(population_service.BindPopulationUnit(pop_spawn.spawned_records[1],Ref("population","u2"))),"bind pop 2");
    Check(static_cast<bool>(population_service.ActivatePopulationBackedEncounter(pop_spawn.encounter_instance)),"activate population backed encounter");

    // Respawn processor uses gameplay time and explicit area/origin contract.
    RespawnRule rule; rule.encounter=def.id; rule.area=forest; rule.delay=GameplayDuration{30}; rule.remaining_limit=1; rule.seed={99};
    auto respawn=service.ScheduleRespawn(rule,{.time=GameplayTimePoint{100}}); Check(static_cast<bool>(respawn),"schedule respawn");
    Check(service.ProcessDueRespawns(GameplayTimePoint{129},10,{.tick=GameplayTickId{4},.time=GameplayTimePoint{129}}).empty(),"respawn not due early");
    auto due=service.ProcessDueRespawns(GameplayTimePoint{130},10,{.tick=GameplayTickId{5},.time=GameplayTimePoint{130}}); Check(due.size()==1,"respawn due");

    // Bounded journal reports a gap instead of pretending retained history is complete.
    EncountersService journal; Check(static_cast<bool>(journal.RegisterSpawnTable(table)),"journal table"); Check(static_cast<bool>(journal.RegisterEncounterDefinition(def)),"journal def"); EncounterBudgets jb; jb.change_journal_capacity=2; journal.SetBudgets(jb);
    SpawnRequest jr; jr.encounter=def.id; jr.area=forest; jr.area_tags=Tags({"biome.forest"}); jr.seed={11}; jr.context.tick=GameplayTickId{1}; auto jspawn=journal.SpawnEncounter(jr); Check(jspawn.state==SpawnResultState::Succeeded,"journal spawn");
    auto stale=journal.ReadChangesSince(ChangeCursor{}); Check(stale.snapshot_required,"journal gap requires snapshot");

    // Restore is transactional and validates generator scopes/references before swap.
    const auto pre_restore_cursor = service.ReadChangesSince(ChangeCursor{}).latest_cursor;
    Check(pre_restore_cursor.sequence >= 2, "pre-restore encounter journal has multiple changes");
    auto snapshot=service.CaptureSnapshot(); EncountersService restored; Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore"); Check(restored.GetEncounterInstance(first.encounter_instance)!=nullptr,"restore encounter");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,"pre-restore encounter cursor requires snapshot");
    Check(restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required,"future encounter cursor incompatible");
    SpawnPoint epoch_point; epoch_point.area=forest; epoch_point.position=Ref("world.position","forest.epoch");
    Check(static_cast<bool>(restored.AddSpawnPoint(epoch_point)),"post-restore encounter change");
    Check(restored.ReadChangesSince(pre_restore_cursor).snapshot_required,"old encounter cursor remains incompatible");
    auto epoch_batch=restored.ReadChangesSince(ChangeCursor{}); Check(!epoch_batch.snapshot_required&&!epoch_batch.changes.empty(),"new encounter epoch readable");
    auto epoch_current=restored.ReadChangesSince(epoch_batch.latest_cursor); Check(!epoch_current.snapshot_required&&epoch_current.changes.empty(),"exact encounter cursor current");
    auto corrupted=snapshot; corrupted.instance_ids.scope=0xDEAD; const auto before=restored.CaptureSnapshot(); Check(!restored.RestoreSnapshot(corrupted),"reject invalid generator scope"); Check(restored.CaptureSnapshot().instances.size()==before.instances.size(),"failed restore leaves state intact");

    // Caller supplied IDs from the service scope advance the matching generator.
    EncountersService id_service; SpawnPoint explicit_point; explicit_point.id=SpawnPointId::FromRaw(0x2701,50); explicit_point.area=forest; explicit_point.position=clearing; Check(static_cast<bool>(id_service.AddSpawnPoint(explicit_point)),"explicit point id"); SpawnPoint next_point; next_point.area=forest; next_point.position=Ref("world.position","forest.next"); auto next_id=id_service.AddSpawnPoint(next_point); Check(static_cast<bool>(next_id) && next_id.Value().value.Low()>50,"caller id advances generator");

    return 0;
}
