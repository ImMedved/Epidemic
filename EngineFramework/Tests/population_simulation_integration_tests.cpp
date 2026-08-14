#include "Epidemic/GameFramework/PopulationSimulationIntegration/population_simulation_adapters.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::population;
using namespace epidemic::gameplay::encounters;
using namespace epidemic::gameplay::roles_jobs;
using namespace epidemic::gameplay::needs_life;
using namespace epidemic::gameplay::population_simulation;
using epidemic::gameplay::random::RandomSeed;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
}

int main()
{
    auto forest=Ref("world.area","forest");
    PopulationService population;
    PopulationGroup bandits;
    bandits.area=forest;
    auto bandit_group=population.CreateGroup(bandits);
    Check(static_cast<bool>(bandit_group),"create bandit group");
    for(int i=0;i<3;++i){PopulationUnit unit;unit.group=bandit_group.Value();unit.state=PopulationUnitState::Abstract;unit.current_area=forest;Check(static_cast<bool>(population.CreateUnit(unit)),"create abstract bandit");}

    EncountersService encounters;
    SpawnTable table;
    table.id=SpawnTableId::FromString("bandits.table");
    table.roll_policy=SpawnRollPolicy::GuaranteedAll;
    SpawnEntry bandit;
    bandit.id=SpawnEntryId::FromString("bandit");
    bandit.archetype=TypeId::FromString("entity.bandit");
    bandit.weight=1;
    bandit.min_count=2;
    bandit.max_count=2;
    table.entries.push_back(bandit);
    Check(static_cast<bool>(encounters.RegisterSpawnTable(table)),"register spawn table");
    EncounterDefinition def;
    def.id=EncounterDefinitionId::FromString("bandit.ambush");
    def.spawn_table=table.id;
    Check(static_cast<bool>(encounters.RegisterEncounterDefinition(def)),"register encounter");

    PopulationEncounterAdapter encounter_adapter;
    SpawnRequest request;
    request.encounter=def.id;
    request.area=forest;
    request.seed=RandomSeed{55};
    auto spawn=encounter_adapter.SpawnFromPopulation(population,encounters,bandit_group.Value(),request,2);
    Check(spawn.spawn.state==SpawnResultState::Succeeded,"population backed spawn");    Check(spawn.allocated_units.size()==2,"allocated units");
    Check(spawn.spawn.spawned_records.size()==2,"spawn records created");
    Check(spawn.spawn.created_entities.empty(),"encounters did not fabricate entities");
    Check(population.GetPopulationCounts(bandit_group.Value()).materialized==0,"population waits for real entity binding");
    for(std::size_t i=0;i<spawn.allocated_units.size();++i){auto entity=Ref("entities",i==0?"bandit.0":"bandit.1");Check(static_cast<bool>(encounters.BindSpawnedEntity(spawn.spawn.spawned_records[i],entity)),"bind spawned entity");Check(static_cast<bool>(population.MaterializeUnit(spawn.allocated_units[i],entity,request.context)),"materialize real entity");}
    Check(population.GetPopulationCounts(bandit_group.Value()).materialized==2,"population materialized after entity bind");

    RolesJobsService roles;
    JobDefinition job;
    job.id=JobDefinitionId::FromString("job.guard");
    Check(static_cast<bool>(roles.RegisterJobDefinition(job)),"register job");
    Workplace workplace;
    workplace.area=forest;
    auto workplace_id=roles.CreateWorkplace(workplace);
    Check(static_cast<bool>(workplace_id),"create workplace");
    auto unit_record=population.GetUnit(spawn.allocated_units.front());
    Check(unit_record!=nullptr,"unit record");
    JobAssignment assignment;
    assignment.worker=unit_record->entity.value();
    assignment.job=job.id;
    assignment.workplace=workplace_id.Value();
    auto assignment_id=roles.AssignJob(assignment);
    Check(static_cast<bool>(assignment_id),"assign job");
    WorkSchedule schedule;
    schedule.assignment=assignment_id.Value();
    WorkShift shift;
    shift.start=GameplayTimePoint{800};
    shift.duration=GameplayDuration{200};
    shift.task_type=JobTaskTypeId::FromString("task.guard");
    schedule.shifts.push_back(shift);
    Check(static_cast<bool>(roles.CreateSchedule(schedule)),"create schedule");

    NeedsLifeService needs;
    NeedDefinition work;
    work.id=NeedTypeId::FromString("need.work");
    work.default_value=0;
    work.min_value=0;
    work.max_value=100;
    Check(static_cast<bool>(needs.RegisterNeedDefinition(work)),"register work need");
    NeedProfile profile;
    profile.subject=assignment.worker;
    profile.active_needs.push_back(work.id);
    Check(static_cast<bool>(needs.CreateNeedProfile(profile)),"create need profile");

    CityLifeAdapter city;
    auto morning=city.RunMorningStep(population,roles,needs,forest,GameplayTimePoint{900},10,{.time=GameplayTimePoint{900}});
    Check(morning.activated_duties.size()==1,"morning duty");
    RolesNeedsAdapter roles_needs;
    const auto* duty=roles.GetCurrentDuty(assignment.worker);
    Check(duty!=nullptr,"current duty for adapter");
    Check(static_cast<bool>(roles_needs.CreateWorkPressureForDuty(*duty,needs,400,{.time=GameplayTimePoint{900}})),"work pressure");
    Check(needs.FindLifePressures(assignment.worker).size()==1,"pressure created");
    Check(static_cast<bool>(roles_needs.SatisfyWorkNeedFromDuty(*duty,needs,work.id,100,{.time=GameplayTimePoint{950}})),"satisfy work need");

    PopulationLifecycleAdapter lifecycle;
    Check(static_cast<bool>(lifecycle.MarkResidentDead(population,roles,needs,spawn.allocated_units.front(),{.time=GameplayTimePoint{1000}})),"mark resident dead");
    Check(population.GetUnit(spawn.allocated_units.front())->state==PopulationUnitState::Dead,"resident dead");
    Check(roles.FindJobsOfSubject(assignment.worker).front().state==AssignmentState::Cancelled,"job cancelled after death");
    Check(needs.FindLifePressures(assignment.worker).empty(),"pressures resolved after death");
    return 0;
}


