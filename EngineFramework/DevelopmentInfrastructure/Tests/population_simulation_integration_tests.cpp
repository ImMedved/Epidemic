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
void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

GameplayObjectRef Ref(const char* domain, const char* id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

void RegisterBanditTemplate(PopulationService& population)
{
    PopulationTemplate templ;
    templ.id = PopulationTemplateId::FromString("population.bandit");
    templ.entity_archetype = TypeId::FromString("entity.bandit");
    Check(static_cast<bool>(population.RegisterTemplate(templ)), "register bandit population template");
    Check(static_cast<bool>(population.FreezeDefinitions()), "freeze population definitions");
}
} // namespace

int main()
{
    const auto forest = Ref("world.area", "forest");
    const auto town = Ref("world.area", "town");

    PopulationService population;
    RegisterBanditTemplate(population);
    PopulationGroup bandits;
    bandits.area = forest;
    auto bandit_group = population.CreateGroup(bandits);
    Check(static_cast<bool>(bandit_group), "create bandit group");
    for (int i = 0; i < 3; ++i)
    {
        PopulationUnit unit;
        unit.group = bandit_group.Value();
        unit.template_id = PopulationTemplateId::FromString("population.bandit");
        unit.state = PopulationUnitState::Abstract;
        unit.current_area = forest;
        Check(static_cast<bool>(population.CreateUnit(unit)), "create abstract bandit");
    }

    EncountersService encounters;
    SpawnTable table;
    table.id = SpawnTableId::FromString("bandits.table");
    table.roll_policy = SpawnRollPolicy::GuaranteedAll;
    SpawnEntry bandit;
    bandit.id = SpawnEntryId::FromString("bandit");
    bandit.archetype = TypeId::FromString("entity.bandit");
    bandit.weight = 1;
    bandit.min_count = 2;
    bandit.max_count = 2;
    table.entries.push_back(bandit);
    Check(static_cast<bool>(encounters.RegisterSpawnTable(table)), "register spawn table");
    EncounterDefinition definition;
    definition.id = EncounterDefinitionId::FromString("bandit.ambush");
    definition.spawn_table = table.id;
    Check(static_cast<bool>(encounters.RegisterEncounterDefinition(definition)), "register encounter");

    PopulationEncounterAdapter encounter_adapter;
    SpawnRequest request;
    request.encounter = definition.id;
    request.area = forest;
    request.seed = RandomSeed{55};
    request.context.time = GameplayTimePoint{100};
    auto spawn_result = encounter_adapter.SpawnFromPopulation(population, encounters, bandit_group.Value(), request, 2);
    Check(static_cast<bool>(spawn_result), "population backed spawn");
    const auto spawn = spawn_result.Value();
    Check(spawn.spawn.state == SpawnResultState::Succeeded, "population backed spawn succeeded");
    Check(spawn.allocated_units.size() == 2, "exactly two population units allocated");
    Check(spawn.spawn.spawned_records.size() == 2, "exactly two spawn records created");
    Check(spawn.spawn.created_entities.empty(), "encounters did not fabricate runtime entities");
    Check(population.GetPopulationCounts(bandit_group.Value()).materialized == 0,
          "allocation does not materialize residents before entity binding");
    for (const auto unit : spawn.allocated_units)
    {
        const auto* allocation = population.GetActiveAllocationForUnit(unit);
        Check(allocation != nullptr && allocation->correlation == spawn.spawn.request_id.value,
              "population owns pending encounter allocation");
    }

    SpawnRequest competing = request;
    competing.id = {};
    competing.seed = RandomSeed{77};
    auto competing_spawn = encounter_adapter.SpawnFromPopulation(population, encounters, bandit_group.Value(), competing, 2);
    Check(!competing_spawn, "second encounter cannot allocate already reserved residents");

    const auto population_snapshot = population.CaptureSnapshot();
    const auto encounters_snapshot = encounters.CaptureSnapshot();
    const auto adapter_snapshot = encounter_adapter.CaptureSnapshot();

    PopulationService restored_population;
    RegisterBanditTemplate(restored_population);
    Check(static_cast<bool>(restored_population.RestoreSnapshot(population_snapshot)), "restore population allocations");
    EncountersService restored_encounters;
    Check(static_cast<bool>(restored_encounters.RestoreSnapshot(encounters_snapshot)), "restore encounter state");
    PopulationEncounterAdapter restored_adapter;
    Check(static_cast<bool>(restored_adapter.RestoreSnapshot(adapter_snapshot, restored_population, restored_encounters)),
          "restore population encounter plan");

    for (std::size_t i = 0; i < spawn.allocated_units.size(); ++i)
    {
        const auto entity = Ref("entities", i == 0 ? "bandit.0" : "bandit.1");
        Check(static_cast<bool>(restored_adapter.BindSpawnedEntity(restored_population, restored_encounters,
                                                                  spawn.spawn.request_id,
                                                                  spawn.spawn.spawned_records[i], entity,
                                                                  {.time = GameplayTimePoint{150}})),
              "bind real encounter entity through population plan");
        const auto* unit = restored_population.GetUnit(spawn.allocated_units[i]);
        Check(unit != nullptr && unit->state == PopulationUnitState::Materialized && unit->entity == entity,
              "allocation commit materializes exactly its resident");
    }
    Check(restored_population.GetPopulationCounts(bandit_group.Value()).materialized == 2,
          "population materialized after all entity bindings");
    const auto* restored_plan = restored_adapter.FindPlan(spawn.spawn.request_id);
    Check(restored_plan != nullptr && restored_plan->state == PopulationEncounterPlanState::Completed,
          "population encounter plan completed after exact bindings");
    Check(restored_population.FindAllocationsByCorrelation(spawn.spawn.request_id.value).empty(),
          "terminal encounter allocations are pruned after complete binding");
    const auto completed_adapter_snapshot = restored_adapter.CaptureSnapshot();
    Check(completed_adapter_snapshot.plans.size() == 1 &&
              completed_adapter_snapshot.plans.front().state == PopulationEncounterPlanState::Completed,
          "completed encounter result is persisted as an idempotency tombstone");
    const auto completed_population_snapshot = restored_population.CaptureSnapshot();
    const auto completed_encounters_snapshot = restored_encounters.CaptureSnapshot();
    PopulationService replay_population;
    RegisterBanditTemplate(replay_population);
    Check(static_cast<bool>(replay_population.RestoreSnapshot(completed_population_snapshot)),
          "restore population after completed encounter");
    EncountersService replay_encounters;
    Check(static_cast<bool>(replay_encounters.RestoreSnapshot(completed_encounters_snapshot)),
          "restore encounters after completed encounter");
    PopulationEncounterAdapter replay_adapter;
    Check(static_cast<bool>(replay_adapter.RestoreSnapshot(completed_adapter_snapshot, replay_population, replay_encounters)),
          "restore completed population encounter tombstone");
    SpawnRequest replay_request = request;
    replay_request.id = spawn.spawn.request_id;
    const auto replay_spawn = replay_adapter.SpawnFromPopulation(
        replay_population, replay_encounters, bandit_group.Value(), replay_request, 2);
    Check(static_cast<bool>(replay_spawn) && replay_spawn.Value().allocated_units == spawn.allocated_units &&
              replay_spawn.Value().spawn.spawned_records == spawn.spawn.spawned_records,
          "completed spawn request remains idempotent after restore");
    Check(replay_population.FindAllocationsByCorrelation(spawn.spawn.request_id.value).empty(),
          "completed spawn replay does not reserve new residents");

    Check(static_cast<bool>(replay_encounters.CompleteEncounter(spawn.spawn.encounter_instance,
                                                            {.time = GameplayTimePoint{175}})),
          "complete encounter before pruning its idempotency record");
    Check(replay_encounters.PruneTerminalEncounters(1) == 1,
          "prune encounter and its SpawnRequestId idempotency record");
    const auto replay_after_encounter_prune = replay_adapter.SpawnFromPopulation(
        replay_population, replay_encounters, bandit_group.Value(), replay_request, 2);
    Check(!replay_after_encounter_prune && replay_adapter.FindPlan(spawn.spawn.request_id) == nullptr,
          "population tombstone is pruned only after encounters drops the matching request horizon");

    const auto resident = PopulationService::ResidentRef(spawn.allocated_units.front());
    RolesJobsService roles;
    JobDefinition job;
    job.id = JobDefinitionId::FromString("job.guard");
    Check(static_cast<bool>(roles.RegisterJobDefinition(job)), "register job");
    Workplace workplace;
    workplace.area = forest;
    auto workplace_id = roles.CreateWorkplace(workplace);
    Check(static_cast<bool>(workplace_id), "create workplace");
    JobAssignment assignment;
    assignment.worker = resident;
    assignment.job = job.id;
    assignment.workplace = workplace_id.Value();
    auto assignment_id = roles.AssignJob(assignment);
    Check(static_cast<bool>(assignment_id), "assign job to stable resident identity");
    WorkSchedule schedule;
    schedule.assignment = assignment_id.Value();
    WorkShift shift;
    shift.start = GameplayTimePoint{800};
    shift.duration = GameplayDuration{200};
    shift.task_type = JobTaskTypeId::FromString("task.guard");
    schedule.shifts.push_back(shift);
    Check(static_cast<bool>(roles.CreateSchedule(schedule)), "create schedule");

    NeedsLifeService needs;
    NeedDefinition work;
    work.id = NeedTypeId::FromString("need.work");
    work.default_value = 0;
    work.min_value = 0;
    work.max_value = 100;
    Check(static_cast<bool>(needs.RegisterNeedDefinition(work)), "register work need");
    NeedProfile profile;
    profile.subject = resident;
    profile.active_needs.push_back(work.id);
    Check(static_cast<bool>(needs.CreateNeedProfile(profile)), "create need profile for stable resident identity");

    Check(static_cast<bool>(restored_population.DematerializeUnit(spawn.allocated_units.front(),
                                                                  {.time = GameplayTimePoint{700}})),
          "dematerialize resident");
    Check(roles.FindJobsOfSubject(resident).size() == 1, "job identity survives dematerialization");
    Check(needs.GetNeedProfile(resident) != nullptr, "needs identity survives dematerialization");

    PopulationGroup outsiders;
    outsiders.area = town;
    auto outsider_group = restored_population.CreateGroup(outsiders);
    Check(static_cast<bool>(outsider_group), "create outsider group");
    PopulationUnit outsider;
    outsider.group = outsider_group.Value();
    outsider.template_id = PopulationTemplateId::FromString("population.bandit");
    outsider.state = PopulationUnitState::Abstract;
    outsider.current_area = town;
    auto outsider_id = restored_population.CreateUnit(outsider);
    Check(static_cast<bool>(outsider_id), "create outsider resident");
    NeedProfile outsider_profile;
    outsider_profile.subject = PopulationService::ResidentRef(outsider_id.Value());
    outsider_profile.active_needs.push_back(work.id);
    Check(static_cast<bool>(needs.CreateNeedProfile(outsider_profile)), "create outsider need profile");

    CityLifeAdapter city;
    auto morning = city.RunMorningStep(restored_population, roles, needs, forest, GameplayTimePoint{900}, 10,
                                       {.time = GameplayTimePoint{1}});
    Check(morning.activated_duties.size() == 1, "morning duty");
    Check(morning.critical_needs.size() == 1 && morning.critical_needs.front().subject == resident,
          "city critical-needs result is filtered to residents of requested area");

    RolesNeedsAdapter roles_needs;
    const auto* duty = roles.GetCurrentDuty(resident);
    Check(duty != nullptr, "current duty uses stable resident identity");
    Check(static_cast<bool>(roles_needs.CreateWorkPressureForDuty(*duty, needs, 400, {.time = GameplayTimePoint{900}})),
          "create work pressure");
    Check(static_cast<bool>(roles_needs.CreateWorkPressureForDuty(*duty, needs, 400, {.time = GameplayTimePoint{901}})),
          "retry same work pressure is idempotent");
    Check(needs.FindLifePressures(resident).size() == 1, "duty produces only one source-keyed work pressure");
    Check(static_cast<bool>(roles_needs.SatisfyWorkNeedFromDuty(*duty, needs, work.id, 100,
                                                               {.time = GameplayTimePoint{950}})),
          "satisfy work need");

    Check(static_cast<bool>(restored_population.MarkUnitDead(spawn.allocated_units.front(),
                                                             {.time = GameplayTimePoint{1000}})),
          "commit authoritative population death first");
    const auto death_revision = restored_population.GetUnit(spawn.allocated_units.front())->revision;
    PopulationLifecycleAdapterSnapshot pending_lifecycle;
    pending_lifecycle.reconciliations.push_back({spawn.allocated_units.front(), death_revision, false, false});
    PopulationLifecycleAdapter lifecycle;
    Check(static_cast<bool>(lifecycle.RestoreSnapshot(pending_lifecycle)), "restore pending death reconciliation");
    Check(static_cast<bool>(lifecycle.MarkResidentDead(restored_population, roles, needs,
                                                       spawn.allocated_units.front(),
                                                       {.time = GameplayTimePoint{1001}})),
          "resume dependent cleanup for already dead resident");
    Check(roles.FindJobsOfSubject(resident).front().state == AssignmentState::Cancelled,
          "stable resident job cancelled after death");
    Check(needs.FindLifePressures(resident).empty(), "stable resident pressures resolved after death");
    Check(lifecycle.CaptureSnapshot().reconciliations.empty(), "completed reconciliation is not persisted");

    return 0;
}
