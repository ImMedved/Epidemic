#include "Epidemic/GameFramework/Population/population.h"
#include <cstdlib>
#include <iostream>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::population;

namespace
{
void Check(bool value,const char* message){if(!value){std::cerr<<message<<'\n';std::exit(1);}}
GameplayObjectRef Ref(const char* domain,const char* id){return {GameplayDomainId::FromString(domain),GameplayObjectId::FromString(id)};}
}

int main()
{
    PopulationService service;
    PopulationTemplate templ;
    templ.id=PopulationTemplateId::FromString("citizen.template");
    templ.entity_archetype=TypeId::FromString("entity.citizen");
    Check(static_cast<bool>(service.RegisterTemplate(templ)),"register template");

    PopulationGroup group;
    group.area=Ref("world.area","town");
    group.society_group=Ref("society.group","citizens");
    group.desired_count=200;
    auto group_id=service.CreateGroup(group);
    Check(static_cast<bool>(group_id),"create group");

    PopulationUnit unit;
    unit.group=group_id.Value();
    unit.template_id=templ.id;
    unit.state=PopulationUnitState::Latent;
    auto unit_id=service.CreateUnit(unit,{.time=GameplayTimePoint{10}});
    Check(static_cast<bool>(unit_id),"create unit");
    Check(service.GetPopulationCounts(group_id.Value()).latent==1,"latent count");

    auto entity=Ref("entities","npc.1");
    Check(static_cast<bool>(service.MaterializeUnit(unit_id.Value(),entity)),"materialize");
    Check(service.GetPopulationCounts(group_id.Value()).materialized==1,"materialized count");
    Check(service.FindMaterializationCandidates(group.area,10).empty(),"no candidate after materialize");

    Check(static_cast<bool>(service.DematerializeUnit(unit_id.Value())),"dematerialize");
    Check(service.GetPopulationCounts(group_id.Value()).abstract_units==1,"abstract count");
    Check(service.FindMaterializationCandidates(group.area,10).size()==1,"candidate after dematerialize");

    PopulationResidence residence;
    residence.unit=unit_id.Value();
    residence.home_area=group.area;
    residence.home_property=Ref("property","house.1");
    Check(static_cast<bool>(service.AssignResidence(residence)),"assign residence");
    Check(service.FindResidentsOfArea(group.area).size()==1,"resident query");

    PopulationMigration migration;
    migration.unit=unit_id.Value();
    migration.to=Ref("world.area","port");
    auto migration_id=service.StartMigration(migration);
    Check(static_cast<bool>(migration_id),"start migration");
    Check(static_cast<bool>(service.CompleteMigration(migration_id.Value())),"complete migration");
    Check(service.GetUnit(unit_id.Value())->current_area==migration.to,"migration area");

    auto snapshot=service.CaptureSnapshot();
    PopulationService restored;
    Check(static_cast<bool>(restored.RestoreSnapshot(snapshot)),"restore");
    Check(restored.GetUnit(unit_id.Value())->current_area==migration.to,"restore unit");
    Check(restored.ChangesSince(0).empty(),"restore no events");

    for(int i=0;i<1000;++i){PopulationUnit extra;extra.group=group_id.Value();Check(static_cast<bool>(restored.CreateUnit(extra)),"stress create unit");}
    Check(restored.GetDiagnostics().units==1001,"diagnostics units");
    return 0;
}
