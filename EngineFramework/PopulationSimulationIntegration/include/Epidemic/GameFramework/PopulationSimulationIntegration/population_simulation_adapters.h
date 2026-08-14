#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Encounters/encounters.h"
#include "Epidemic/GameFramework/NeedsLife/needs_life.h"
#include "Epidemic/GameFramework/Population/population.h"
#include "Epidemic/GameFramework/RolesJobs/roles_jobs.h"

#include <cstddef>
#include <vector>

namespace epidemic::gameplay::population_simulation
{
struct PopulationBackedSpawnResult
{
    encounters::SpawnResult spawn;
    std::vector<population::PopulationUnitId> allocated_units;
};

class PopulationEncounterAdapter
{
public:
    [[nodiscard]] PopulationBackedSpawnResult SpawnFromPopulation(
        population::PopulationService& population_service,
        encounters::EncountersService& encounter_service,
        population::PopulationGroupId group,
        encounters::SpawnRequest request,
        std::size_t max_units) const;
};

class RolesNeedsAdapter
{
public:
    [[nodiscard]] foundation::Result<void> CreateWorkPressureForDuty(
        const roles_jobs::Duty& duty,
        needs_life::NeedsLifeService& needs_service,
        std::int64_t urgency,
        GameplayContext context) const;

    [[nodiscard]] foundation::Result<void> SatisfyWorkNeedFromDuty(
        const roles_jobs::Duty& duty,
        needs_life::NeedsLifeService& needs_service,
        needs_life::NeedTypeId work_need,
        std::int64_t amount,
        GameplayContext context) const;
};

class PopulationLifecycleAdapter
{
public:
    [[nodiscard]] foundation::Result<void> MarkResidentDead(
        population::PopulationService& population_service,
        roles_jobs::RolesJobsService& roles_service,
        needs_life::NeedsLifeService& needs_service,
        population::PopulationUnitId unit,
        GameplayContext context) const;
};

struct CityMorningResult
{
    std::vector<roles_jobs::DutyId> activated_duties;
    std::vector<population::PopulationUnit> materialization_candidates;
    std::vector<needs_life::NeedState> critical_needs;
};

class CityLifeAdapter
{
public:
    [[nodiscard]] CityMorningResult RunMorningStep(
        population::PopulationService& population_service,
        roles_jobs::RolesJobsService& roles_service,
        needs_life::NeedsLifeService& needs_service,
        GameplayObjectRef area,
        GameplayTimePoint now,
        std::size_t materialization_limit,
        GameplayContext context) const;
};
}
