#include "Epidemic/GameFramework/PopulationSimulationIntegration/population_simulation_adapters.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>

namespace epidemic::gameplay::population_simulation
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] constexpr TypeId EncounterAllocationPurpose() noexcept
{
    return TypeId::FromString("population.allocation.encounter");
}

[[nodiscard]] constexpr TypeId WorkPressureType() noexcept
{
    return TypeId::FromString("life.pressure.work");
}

[[nodiscard]] bool IsAllocatableState(population::PopulationUnitState state) noexcept
{
    return state == population::PopulationUnitState::Latent || state == population::PopulationUnitState::Abstract;
}

void AppendU64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (std::uint32_t shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
}

[[nodiscard]] std::uint64_t ReadU64(const std::byte* data) noexcept
{
    std::uint64_t value = 0;
    for (std::uint32_t shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(data[shift / 8])) << shift;
    return value;
}

[[nodiscard]] std::vector<std::byte> EncodeDutyPressureSource(roles_jobs::DutyId duty)
{
    std::vector<std::byte> payload;
    payload.reserve(17);
    payload.push_back(std::byte{1});
    AppendU64(payload, duty.value.High());
    AppendU64(payload, duty.value.Low());
    return payload;
}

[[nodiscard]] bool IsDutyPressureSource(const std::vector<std::byte>& payload, roles_jobs::DutyId duty) noexcept
{
    return payload.size() == 17 && payload[0] == std::byte{1} && ReadU64(payload.data() + 1) == duty.value.High() &&
           ReadU64(payload.data() + 9) == duty.value.Low();
}

[[nodiscard]] bool IsTerminalAssignment(roles_jobs::AssignmentState state) noexcept
{
    return state == roles_jobs::AssignmentState::Completed || state == roles_jobs::AssignmentState::Cancelled ||
           state == roles_jobs::AssignmentState::Invalid;
}
} // namespace

PopulationBackedEncounterPlan* PopulationEncounterAdapter::FindMutablePlan(encounters::SpawnRequestId request) noexcept
{
    auto it = std::find_if(plans_.begin(), plans_.end(), [request](const auto& plan) { return plan.request == request; });
    return it == plans_.end() ? nullptr : &*it;
}

const PopulationBackedEncounterPlan* PopulationEncounterAdapter::FindPlan(encounters::SpawnRequestId request) const noexcept
{
    auto it = std::find_if(plans_.begin(), plans_.end(), [request](const auto& plan) { return plan.request == request; });
    return it == plans_.end() ? nullptr : &*it;
}

foundation::Result<PopulationBackedSpawnResult> PopulationEncounterAdapter::SpawnFromPopulation(
    population::PopulationService& pop,
    encounters::EncountersService& enc,
    population::PopulationGroupId group,
    encounters::SpawnRequest request,
    std::size_t max_units)
{
    if (!group.IsValid() || !pop.GetGroup(group) || !request.encounter.IsValid() || !request.area.IsValid())
        return foundation::Result<PopulationBackedSpawnResult>::Failure(
            Error("gameplay.population_sim.invalid_population_spawn", "invalid population-backed encounter request"));

    if (request.id.IsValid())
    {
        if (const auto* existing = FindPlan(request.id))
        {
            if (existing->group != group || existing->encounter != request.encounter || existing->area != request.area ||
                existing->seed != request.seed || existing->state == PopulationEncounterPlanState::Failed)
                return foundation::Result<PopulationBackedSpawnResult>::Failure(
                    Error("gameplay.population_sim.spawn_request_conflict", "spawn request id already belongs to another plan"));
            PopulationBackedSpawnResult result;
            result.spawn.request_id = existing->request;
            result.spawn.encounter_instance = existing->encounter_instance;
            result.spawn.state = encounters::SpawnResultState::Succeeded;
            result.spawn.revision = enc.CurrentRevision();
            for (const auto& slot : existing->slots)
            {
                result.allocated_units.push_back(slot.unit);
                result.spawn.spawned_records.push_back(slot.spawned_record);
                if (slot.entity.IsValid())
                {
                    result.spawn.created_entities.push_back(slot.entity);
                    result.spawn.linked_population_units.push_back(population::PopulationService::ResidentRef(slot.unit));
                }
            }
            return foundation::Result<PopulationBackedSpawnResult>::Success(std::move(result));
        }
    }

    auto preview = enc.PreviewSpawnArchetypes(request);
    if (!preview)
        return foundation::Result<PopulationBackedSpawnResult>::Failure(preview.GetError());
    if (preview.Value().empty())
        return foundation::Result<PopulationBackedSpawnResult>::Failure(
            Error("gameplay.population_sim.empty_population_spawn", "population-backed encounter resolved to no spawn slots"));
    if (preview.Value().size() > max_units)
        return foundation::Result<PopulationBackedSpawnResult>::Failure(
            Error("gameplay.population_sim.population_budget_exceeded", "encounter requires more population units than allowed"));

    auto candidates = pop.FindUnitsByGroup(group);
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::vector<population::PopulationUnitId> selected;
    std::vector<population::PopulationTemplateId> selected_templates;
    selected.reserve(preview.Value().size());
    selected_templates.reserve(preview.Value().size());

    std::unordered_set<population::PopulationUnitId, population::IdHash> used;
    for (const auto archetype : preview.Value())
    {
        const population::PopulationUnit* match = nullptr;
        for (const auto& unit : candidates)
        {
            if (used.contains(unit.id) || !IsAllocatableState(unit.state) || unit.entity ||
                pop.GetActiveAllocationForUnit(unit.id) != nullptr || !unit.template_id.IsValid())
                continue;
            const auto* definition = pop.GetTemplate(unit.template_id);
            if (!definition || definition->entity_archetype != archetype)
                continue;
            match = &unit;
            break;
        }
        if (!match)
            return foundation::Result<PopulationBackedSpawnResult>::Failure(
                Error("gameplay.population_sim.population_slot_unavailable",
                      "no unallocated population unit matches an encounter spawn archetype"));
        used.insert(match->id);
        selected.push_back(match->id);
        selected_templates.push_back(match->template_id);
    }

    population::PopulationAllocationRequest allocation_request;
    allocation_request.units = selected;
    allocation_request.purpose = EncounterAllocationPurpose();
    if (request.id.IsValid())
        allocation_request.correlation = request.id.value;
    auto reserved = pop.ReserveAllocations(std::move(allocation_request), request.context);
    if (!reserved)
        return foundation::Result<PopulationBackedSpawnResult>::Failure(reserved.GetError());

    request.id = encounters::SpawnRequestId{reserved.Value().correlation};
    request.persistence = encounters::SpawnPersistencePolicy::PopulationBacked;
    auto spawn = enc.SpawnEncounter(request);
    const bool population_spawn_created = spawn.state == encounters::SpawnResultState::Succeeded ||
                                          (spawn.state == encounters::SpawnResultState::Deferred &&
                                           spawn.encounter_instance.IsValid());
    if (!population_spawn_created)
    {
        for (const auto& token : reserved.Value().tokens)
            (void)pop.ReleaseAllocation(token.allocation, request.context);
        return foundation::Result<PopulationBackedSpawnResult>::Failure(
            Error("gameplay.population_sim.encounter_spawn_failed", "population-backed encounter spawn failed"));
    }
    spawn.state = encounters::SpawnResultState::Succeeded;

    if (spawn.spawned_records.size() != preview.Value().size() || reserved.Value().tokens.size() != preview.Value().size())
    {
        (void)enc.FailEncounter(spawn.encounter_instance, request.context);
        for (const auto& token : reserved.Value().tokens)
            (void)pop.ReleaseAllocation(token.allocation, request.context);
        return foundation::Result<PopulationBackedSpawnResult>::Failure(
            Error("gameplay.population_sim.spawn_plan_mismatch", "encounter result count does not match reserved population plan"));
    }

    PopulationBackedEncounterPlan plan;
    plan.request = request.id;
    plan.encounter = request.encounter;
    plan.encounter_instance = spawn.encounter_instance;
    plan.group = group;
    plan.area = request.area;
    plan.seed = request.seed;
    plan.slots.reserve(preview.Value().size());
    for (std::size_t index = 0; index < preview.Value().size(); ++index)
    {
        const auto* record = enc.GetSpawnedEntityRecord(spawn.spawned_records[index]);
        const auto token = std::find_if(reserved.Value().tokens.begin(), reserved.Value().tokens.end(),
                                        [unit = selected[index]](const auto& candidate) { return candidate.unit == unit; });
        if (!record || record->archetype != preview.Value()[index] || record->encounter != spawn.encounter_instance)
        {
            (void)enc.FailEncounter(spawn.encounter_instance, request.context);
            for (const auto& token : reserved.Value().tokens)
                (void)pop.ReleaseAllocation(token.allocation, request.context);
            return foundation::Result<PopulationBackedSpawnResult>::Failure(
                Error("gameplay.population_sim.spawn_plan_mismatch", "encounter spawned record does not match population plan"));
        }
        if (token == reserved.Value().tokens.end())
        {
            (void)enc.FailEncounter(spawn.encounter_instance, request.context);
            for (const auto& reserved_token : reserved.Value().tokens)
                (void)pop.ReleaseAllocation(reserved_token.allocation, request.context);
            return foundation::Result<PopulationBackedSpawnResult>::Failure(
                Error("gameplay.population_sim.spawn_plan_mismatch", "population allocation token does not match planned unit"));
        }
        PopulationBackedEncounterSlot slot;
        slot.allocation = token->allocation;
        slot.unit = token->unit;
        slot.population_template = selected_templates[index];
        slot.archetype = preview.Value()[index];
        slot.spawned_record = spawn.spawned_records[index];
        plan.slots.push_back(slot);
    }
    plans_.push_back(plan);

    PopulationBackedSpawnResult result;
    result.spawn = std::move(spawn);
    result.allocated_units = std::move(selected);
    return foundation::Result<PopulationBackedSpawnResult>::Success(std::move(result));
}

foundation::Result<void> PopulationEncounterAdapter::BindSpawnedEntity(
    population::PopulationService& pop,
    encounters::EncountersService& enc,
    encounters::SpawnRequestId request,
    encounters::SpawnedEntityRecordId spawned_record,
    GameplayObjectRef entity,
    GameplayContext context)
{
    auto* plan = FindMutablePlan(request);
    if (!plan || plan->state == PopulationEncounterPlanState::Failed || !entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.population_plan_missing", "population-backed encounter plan is unavailable"));
    auto slot = std::find_if(plan->slots.begin(), plan->slots.end(),
                             [spawned_record](const auto& candidate) { return candidate.spawned_record == spawned_record; });
    if (slot == plan->slots.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.population_slot_missing", "spawned record is not part of the population plan"));
    if (slot->entity.IsValid() && slot->entity != entity)
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.population_slot_conflict", "population slot is already bound to another entity"));
    if (slot->entity == entity)
        return foundation::Result<void>::Success();

    const auto* spawned = enc.GetSpawnedEntityRecord(slot->spawned_record);
    if (!spawned || spawned->encounter != plan->encounter_instance || spawned->archetype != slot->archetype)
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.spawn_record_mismatch", "encounter spawned record no longer matches population plan"));
    if (!spawned->entity.IsValid())
    {
        auto bound = enc.BindSpawnedEntity(slot->spawned_record, entity, context);
        if (!bound)
            return bound;
    }
    else if (spawned->entity != entity)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.spawn_entity_conflict", "encounter spawned record is bound to another entity"));
    }

    const auto* allocation = pop.GetAllocation(slot->allocation);
    if (!allocation || allocation->unit != slot->unit || allocation->correlation != request.value ||
        allocation->purpose != EncounterAllocationPurpose())
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.allocation_mismatch", "population allocation no longer matches encounter plan"));
    if (allocation->state == population::PopulationAllocationState::Active)
    {
        auto committed = pop.CommitAllocation(slot->allocation, entity, context);
        if (!committed)
            return committed;
    }
    else if (allocation->state != population::PopulationAllocationState::Committed || allocation->bound_entity != entity)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.allocation_commit_conflict", "population allocation cannot be reconciled with entity binding"));
    }

    slot->entity = entity;
    if (std::all_of(plan->slots.begin(), plan->slots.end(), [](const auto& item) { return item.entity.IsValid(); }))
    {
        plan->state = PopulationEncounterPlanState::Completed;
        pop.PruneTerminalAllocations(request.value);
        while (plans_.size() > 1024)
        {
            auto completed = std::find_if(plans_.begin(), plans_.end(), [](const auto& candidate) {
                return candidate.state == PopulationEncounterPlanState::Completed ||
                       candidate.state == PopulationEncounterPlanState::Failed;
            });
            if (completed == plans_.end())
                break;
            plans_.erase(completed);
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationEncounterAdapter::CancelPendingSpawn(
    population::PopulationService& pop,
    encounters::EncountersService& enc,
    encounters::SpawnRequestId request,
    GameplayContext context)
{
    auto* plan = FindMutablePlan(request);
    if (!plan)
        return foundation::Result<void>::Success();
    if (plan->state == PopulationEncounterPlanState::Completed ||
        std::any_of(plan->slots.begin(), plan->slots.end(), [](const auto& slot) { return slot.entity.IsValid(); }))
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.population_plan_reconciliation_required",
                  "population-backed encounter already committed entity bindings"));

    for (const auto& slot : plan->slots)
    {
        const auto* allocation = pop.GetAllocation(slot.allocation);
        if (allocation && allocation->state == population::PopulationAllocationState::Active)
        {
            auto released = pop.ReleaseAllocation(slot.allocation, context);
            if (!released)
                return released;
        }
    }
    const auto* encounter = enc.GetEncounterInstance(plan->encounter_instance);
    if (encounter && encounter->state == encounters::EncounterState::Active)
    {
        auto failed = enc.FailEncounter(plan->encounter_instance, context);
        if (!failed)
            return failed;
    }
    plan->state = PopulationEncounterPlanState::Failed;
    return foundation::Result<void>::Success();
}

PopulationEncounterAdapterSnapshot PopulationEncounterAdapter::CaptureSnapshot() const
{
    PopulationEncounterAdapterSnapshot snapshot;
    for (const auto& plan : plans_)
    {
        if (plan.state == PopulationEncounterPlanState::AwaitingEntityBindings)
            snapshot.plans.push_back(plan);
    }
    std::sort(snapshot.plans.begin(), snapshot.plans.end(), [](const auto& a, const auto& b) { return a.request < b.request; });
    return snapshot;
}

foundation::Result<void> PopulationEncounterAdapter::RestoreSnapshot(
    PopulationEncounterAdapterSnapshot snapshot,
    const population::PopulationService& pop,
    const encounters::EncountersService& enc)
{
    std::vector<PopulationBackedEncounterPlan> validated;
    std::sort(snapshot.plans.begin(), snapshot.plans.end(), [](const auto& a, const auto& b) { return a.request < b.request; });
    for (std::size_t index = 0; index < snapshot.plans.size(); ++index)
    {
        auto plan = snapshot.plans[index];
        if (!plan.request.IsValid() || !plan.encounter.IsValid() || !plan.encounter_instance.IsValid() ||
            !plan.group.IsValid() || !plan.area.IsValid() || plan.slots.empty() ||
            (index > 0 && snapshot.plans[index - 1].request == plan.request))
            return foundation::Result<void>::Failure(
                Error("gameplay.population_sim.restore_invalid_plan", "invalid population-backed encounter plan snapshot"));
        const auto* encounter = enc.GetEncounterInstance(plan.encounter_instance);
        if (!encounter || encounter->definition != plan.encounter || encounter->area != plan.area)
            return foundation::Result<void>::Failure(
                Error("gameplay.population_sim.restore_invalid_plan", "population plan references an incompatible encounter"));

        std::unordered_set<population::PopulationUnitId, population::IdHash> units;
        std::unordered_set<encounters::SpawnedEntityRecordId, encounters::IdHash> spawned_records;
        bool complete = true;
        for (auto& slot : plan.slots)
        {
            const auto* allocation = pop.GetAllocation(slot.allocation);
            const auto* unit = pop.GetUnit(slot.unit);
            const auto* definition = unit && unit->template_id.IsValid() ? pop.GetTemplate(unit->template_id) : nullptr;
            const auto* spawned = enc.GetSpawnedEntityRecord(slot.spawned_record);
            if (!slot.allocation.IsValid() || !slot.unit.IsValid() || !slot.population_template.IsValid() ||
                !slot.archetype.IsValid() || !slot.spawned_record.IsValid() || !allocation || !unit || !definition ||
                allocation->unit != slot.unit || allocation->correlation != plan.request.value ||
                allocation->purpose != EncounterAllocationPurpose() || unit->template_id != slot.population_template ||
                definition->entity_archetype != slot.archetype || !spawned || spawned->encounter != plan.encounter_instance ||
                spawned->archetype != slot.archetype || !units.insert(slot.unit).second ||
                !spawned_records.insert(slot.spawned_record).second ||
                allocation->state == population::PopulationAllocationState::Released)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population_sim.restore_invalid_slot", "invalid population-backed encounter slot snapshot"));

            if (spawned->entity.IsValid() && allocation->state == population::PopulationAllocationState::Committed &&
                spawned->entity != allocation->bound_entity)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population_sim.restore_binding_conflict", "encounter and population bindings disagree"));
            if (slot.entity.IsValid() &&
                ((spawned->entity.IsValid() && spawned->entity != slot.entity) ||
                 (allocation->state == population::PopulationAllocationState::Committed &&
                  allocation->bound_entity != slot.entity)))
                return foundation::Result<void>::Failure(
                    Error("gameplay.population_sim.restore_binding_conflict", "saved population plan binding disagrees with owners"));

            if (spawned->entity.IsValid() && allocation->state == population::PopulationAllocationState::Committed)
                slot.entity = spawned->entity;
            else
            {
                slot.entity = {};
                complete = false;
            }
        }
        plan.state = complete ? PopulationEncounterPlanState::Completed : PopulationEncounterPlanState::AwaitingEntityBindings;
        validated.push_back(std::move(plan));
    }
    plans_ = std::move(validated);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RolesNeedsAdapter::CreateWorkPressureForDuty(const roles_jobs::Duty& duty,
                                                                      needs_life::NeedsLifeService& needs,
                                                                      std::int64_t urgency,
                                                                      GameplayContext context) const
{
    if (!duty.id.IsValid() || !duty.subject.IsValid() || duty.subject.domain != population::PopulationService::Domain())
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.invalid_duty", "population work duty requires a stable resident subject"));

    for (const auto& pressure : needs.FindLifePressures(duty.subject))
    {
        if (pressure.type == WorkPressureType() && IsDutyPressureSource(pressure.payload, duty.id))
        {
            if (pressure.urgency != urgency)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population_sim.work_pressure_conflict", "duty already owns a work pressure with different urgency"));
            return foundation::Result<void>::Success();
        }
    }

    needs_life::LifePressure pressure;
    pressure.subject = duty.subject;
    pressure.type = WorkPressureType();
    pressure.urgency = urgency;
    pressure.payload = EncodeDutyPressureSource(duty.id);
    auto created = needs.CreateLifePressure(std::move(pressure), context);
    if (!created)
        return foundation::Result<void>::Failure(created.GetError());
    return foundation::Result<void>::Success();
}

foundation::Result<void> RolesNeedsAdapter::SatisfyWorkNeedFromDuty(const roles_jobs::Duty& duty,
                                                                    needs_life::NeedsLifeService& needs,
                                                                    needs_life::NeedTypeId work_need,
                                                                    std::int64_t amount,
                                                                    GameplayContext context) const
{
    if (!duty.id.IsValid() || !duty.subject.IsValid() || duty.subject.domain != population::PopulationService::Domain())
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.invalid_duty", "population work duty requires a stable resident subject"));
    needs_life::SatisfyNeedRequest request;
    request.subject = duty.subject;
    request.need = work_need;
    request.amount = amount;
    request.source = TypeId::FromString("roles_jobs.duty");
    request.context = context;
    return needs.SatisfyNeed(request);
}

PopulationLifecycleReconciliation* PopulationLifecycleAdapter::FindReconciliation(population::PopulationUnitId unit,
                                                                                   Revision death_revision) noexcept
{
    auto it = std::find_if(reconciliations_.begin(), reconciliations_.end(), [unit, death_revision](const auto& record) {
        return record.unit == unit && record.death_revision == death_revision;
    });
    return it == reconciliations_.end() ? nullptr : &*it;
}

foundation::Result<void> PopulationLifecycleAdapter::MarkResidentDead(population::PopulationService& pop,
                                                                      roles_jobs::RolesJobsService& roles,
                                                                      needs_life::NeedsLifeService& needs,
                                                                      population::PopulationUnitId unit,
                                                                      GameplayContext context)
{
    const auto* before = pop.GetUnit(unit);
    if (!before)
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.unit_missing", "population unit missing"));
    const auto legacy_entity = before->entity.value_or(GameplayObjectRef{});

    if (before->state != population::PopulationUnitState::Dead)
    {
        auto marked = pop.MarkUnitDead(unit, context);
        if (!marked)
            return marked;
    }

    const auto* dead = pop.GetUnit(unit);
    if (!dead || dead->state != population::PopulationUnitState::Dead)
        return foundation::Result<void>::Failure(
            Error("gameplay.population_sim.death_not_committed", "population death was not committed"));

    auto* reconciliation = FindReconciliation(unit, dead->revision);
    if (!reconciliation)
    {
        reconciliations_.push_back({unit, dead->revision, false, false});
        reconciliation = &reconciliations_.back();
    }

    const auto resident = population::PopulationService::ResidentRef(unit);
    if (!reconciliation->roles_cleaned)
    {
        std::array<GameplayObjectRef, 2> subjects{resident, legacy_entity};
        for (const auto subject : subjects)
        {
            if (!subject.IsValid())
                continue;
            for (const auto& assignment : roles.FindJobsOfSubject(subject))
            {
                if (IsTerminalAssignment(assignment.state))
                    continue;
                auto cancelled = roles.CancelAssignment(assignment.id, context);
                if (!cancelled)
                    return cancelled;
            }
        }
        reconciliation->roles_cleaned = true;
    }

    if (!reconciliation->needs_cleaned)
    {
        std::array<GameplayObjectRef, 2> subjects{resident, legacy_entity};
        for (const auto subject : subjects)
        {
            if (!subject.IsValid())
                continue;
            for (const auto& pressure : needs.FindLifePressures(subject))
            {
                auto resolved = needs.ResolveLifePressure(pressure.id, context);
                if (!resolved)
                    return resolved;
            }
        }
        reconciliation->needs_cleaned = true;
    }
    return foundation::Result<void>::Success();
}

PopulationLifecycleAdapterSnapshot PopulationLifecycleAdapter::CaptureSnapshot() const
{
    PopulationLifecycleAdapterSnapshot snapshot;
    for (const auto& record : reconciliations_)
    {
        if (!record.Complete())
            snapshot.reconciliations.push_back(record);
    }
    std::sort(snapshot.reconciliations.begin(), snapshot.reconciliations.end(), [](const auto& a, const auto& b) {
        if (a.unit != b.unit)
            return a.unit < b.unit;
        return a.death_revision < b.death_revision;
    });
    return snapshot;
}

foundation::Result<void> PopulationLifecycleAdapter::RestoreSnapshot(PopulationLifecycleAdapterSnapshot snapshot)
{
    std::sort(snapshot.reconciliations.begin(), snapshot.reconciliations.end(), [](const auto& a, const auto& b) {
        if (a.unit != b.unit)
            return a.unit < b.unit;
        return a.death_revision < b.death_revision;
    });
    for (std::size_t i = 0; i < snapshot.reconciliations.size(); ++i)
    {
        const auto& record = snapshot.reconciliations[i];
        if (!record.unit.IsValid() || record.death_revision.value == 0 || record.Complete() ||
            (i > 0 && snapshot.reconciliations[i - 1].unit == record.unit &&
             snapshot.reconciliations[i - 1].death_revision == record.death_revision))
            return foundation::Result<void>::Failure(
                Error("gameplay.population_sim.restore_invalid_reconciliation", "invalid population lifecycle reconciliation snapshot"));
    }
    reconciliations_ = std::move(snapshot.reconciliations);
    return foundation::Result<void>::Success();
}

void PopulationLifecycleAdapter::PruneCompleted()
{
    std::erase_if(reconciliations_, [](const auto& record) { return record.Complete(); });
}

CityMorningResult CityLifeAdapter::RunMorningStep(population::PopulationService& pop,
                                                  roles_jobs::RolesJobsService& roles,
                                                  needs_life::NeedsLifeService& needs,
                                                  GameplayObjectRef area,
                                                  GameplayTimePoint now,
                                                  std::size_t materialization_limit,
                                                  GameplayContext context) const
{
    CityMorningResult result;
    context.time = now;
    result.activated_duties = roles.ActivateDueShifts(now, context);
    result.materialization_candidates = pop.FindMaterializationCandidates(area, materialization_limit);

    std::unordered_set<GameplayObjectRef> residents;
    for (const auto& unit : pop.FindUnitsInArea(area))
    {
        if (unit.state != population::PopulationUnitState::Dead && unit.state != population::PopulationUnitState::Removed)
            residents.insert(population::PopulationService::ResidentRef(unit.id));
    }
    for (const auto& need : needs.FindCriticalNeeds(now))
    {
        if (residents.contains(need.subject))
            result.critical_needs.push_back(need);
    }
    return result;
}
} // namespace epidemic::gameplay::population_simulation
