#include "Epidemic/GameFramework/PopulationSimulationIntegration/population_simulation_adapters.h"
#include "Epidemic/Foundation/error.h"

namespace epidemic::gameplay::population_simulation
{
namespace{foundation::Error Error(std::string_view c,std::string_view m){return foundation::Error::Create(c,m);} }
PopulationBackedSpawnResult PopulationEncounterAdapter::SpawnFromPopulation(population::PopulationService& pop,encounters::EncountersService& enc,population::PopulationGroupId group,encounters::SpawnRequest request,std::size_t max_units) const
{
    PopulationBackedSpawnResult result;
    auto units=pop.FindUnitsByGroup(group);
    std::sort(units.begin(),units.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    for(const auto& unit:units)
    {
        if(result.allocated_units.size()>=max_units)break;
        if(unit.state==population::PopulationUnitState::Latent||unit.state==population::PopulationUnitState::Abstract)
        {
            result.allocated_units.push_back(unit.id);
        }
    }
    request.persistence=encounters::SpawnPersistencePolicy::PopulationBacked;
    result.spawn=enc.SpawnEncounter(request);
    const auto count=std::min(result.allocated_units.size(),result.spawn.created_entities.size());
    for(std::size_t index=0;index<count;++index)
    {
        auto materialized=pop.MaterializeUnit(result.allocated_units[index],result.spawn.created_entities[index],request.context);
        if(materialized)
        {
            result.spawn.linked_population_units.push_back(GameplayObjectRef{population::PopulationService::Domain(),result.allocated_units[index].value});
        }
    }
    return result;
}

foundation::Result<void> RolesNeedsAdapter::CreateWorkPressureForDuty(const roles_jobs::Duty& duty,needs_life::NeedsLifeService& needs,std::int64_t urgency,GameplayContext context) const
{
    if(!duty.subject.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population_sim.invalid_duty","invalid duty"));
    needs_life::LifePressure p;
    p.subject=duty.subject;
    p.type=TypeId::FromString("life.pressure.work");
    p.urgency=urgency;
    auto created=needs.CreateLifePressure(p,context);
    if(!created)return foundation::Result<void>::Failure(created.GetError());
    return foundation::Result<void>::Success();
}

foundation::Result<void> RolesNeedsAdapter::SatisfyWorkNeedFromDuty(const roles_jobs::Duty& duty,needs_life::NeedsLifeService& needs,needs_life::NeedTypeId work_need,std::int64_t amount,GameplayContext context) const
{
    if(!duty.subject.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population_sim.invalid_duty","invalid duty"));
    needs_life::SatisfyNeedRequest request;
    request.subject=duty.subject;
    request.need=work_need;
    request.amount=amount;
    request.source=TypeId::FromString("roles_jobs.duty");
    request.context=context;
    return needs.SatisfyNeed(request);
}

foundation::Result<void> PopulationLifecycleAdapter::MarkResidentDead(population::PopulationService& pop,roles_jobs::RolesJobsService& roles,needs_life::NeedsLifeService& needs,population::PopulationUnitId unit,GameplayContext context) const
{
    const auto* record=pop.GetUnit(unit);
    if(!record)return foundation::Result<void>::Failure(Error("gameplay.population_sim.unit_missing","population unit missing"));
    const auto subject=record->entity.value_or(GameplayObjectRef{population::PopulationService::Domain(),unit.value});
    auto marked=pop.MarkUnitDead(unit,context);
    if(!marked)return marked;
    for(const auto& assignment:roles.FindJobsOfSubject(subject))
    {
        auto cancelled=roles.CancelAssignment(assignment.id,context);
        if(!cancelled)return cancelled;
    }
    for(const auto& pressure:needs.FindLifePressures(subject))
    {
        auto resolved=needs.ResolveLifePressure(pressure.id,context);
        if(!resolved)return resolved;
    }
    return foundation::Result<void>::Success();
}

CityMorningResult CityLifeAdapter::RunMorningStep(population::PopulationService& pop,roles_jobs::RolesJobsService& roles,needs_life::NeedsLifeService& needs,GameplayObjectRef area,GameplayTimePoint now,std::size_t materialization_limit,GameplayContext context) const
{
    CityMorningResult result;
    result.activated_duties=roles.ActivateDueShifts(now,context);
    result.materialization_candidates=pop.FindMaterializationCandidates(area,materialization_limit);
    result.critical_needs=needs.FindCriticalNeeds(now);
    return result;
}
} // namespace epidemic::gameplay::population_simulation
