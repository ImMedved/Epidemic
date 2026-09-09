#include "Epidemic/GameFramework/Population/population.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::population
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] constexpr bool IsTerminalUnitState(PopulationUnitState state) noexcept
{
    return state == PopulationUnitState::Dead || state == PopulationUnitState::Removed;
}

[[nodiscard]] constexpr bool CanMaterialize(PopulationUnitState state) noexcept
{
    return state == PopulationUnitState::Latent || state == PopulationUnitState::Abstract;
}

[[nodiscard]] constexpr std::size_t StateIndex(PopulationUnitState state) noexcept
{
    return static_cast<std::size_t>(state);
}

[[nodiscard]] bool SameTemplateDefinition(const PopulationTemplate &a, const PopulationTemplate &b)
{
    return a.id == b.id && a.entity_archetype == b.entity_archetype && a.tags.Values() == b.tags.Values() &&
           a.generation_payload == b.generation_payload && a.persistent == b.persistent;
}

template <class TWrappedId>
void AdvanceGeneratorPast(MonotonicIdGenerator<GameplayObjectId> &generator, const TWrappedId &id)
{
    if (!id.IsValid() || id.value.High() != generator.Scope().Raw())
        return;

    auto snapshot = generator.GetSnapshot();
    if (snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;

    if (id.value.Low() == std::numeric_limits<std::uint64_t>::max())
        snapshot.next = 0;
    else
        snapshot.next = id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class TWrappedId>
[[nodiscard]] std::uint64_t MaxLowForScope(const std::vector<TWrappedId> &ids, IdScopeId scope) noexcept
{
    std::uint64_t max_low = 0;
    for (const auto &id : ids)
    {
        if (id.IsValid() && id.value.High() == scope.Raw())
            max_low = std::max(max_low, id.value.Low());
    }
    return max_low;
}
} // namespace

PopulationUnit *PopulationService::FindMutableUnit(PopulationUnitId id) noexcept
{
    auto it = units_.find(id);
    return it == units_.end() ? nullptr : &it->second;
}

PopulationGroup *PopulationService::FindMutableGroup(PopulationGroupId id) noexcept
{
    auto it = groups_.find(id);
    return it == groups_.end() ? nullptr : &it->second;
}

foundation::Result<void> PopulationService::RegisterTemplate(PopulationTemplate definition)
{
    if (definitions_frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.definitions_frozen", "population definitions are frozen"));
    if (!definition.id.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_template", "invalid population template"));
    if (templates_.contains(definition.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.duplicate_template", "duplicate population template"));

    Bump();
    definition.revision = revision_;
    templates_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::FreezeDefinitions()
{
    if (definitions_frozen_)
        return foundation::Result<void>::Success();
    definitions_frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<PopulationGroupId> PopulationService::CreateGroup(PopulationGroup group)
{
    if (!group.area.IsValid())
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.invalid_group", "invalid population group"));
    if (group.state == PopulationGroupState::Removed)
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.invalid_group_state", "cannot create a removed population group"));

    if (!group.id.IsValid())
        group.id = PopulationGroupId{group_ids_.Next()};
    if (!group.id.IsValid())
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.id_exhausted", "population group id generator exhausted"));
    if (groups_.contains(group.id))
        return foundation::Result<PopulationGroupId>::Failure(
            Error("gameplay.population.duplicate_group", "duplicate population group"));

    AdvanceGeneratorPast(group_ids_, group.id);
    group.current_known_count = 0;
    group.materialized_count = 0;
    Bump();
    group.revision = revision_;
    const auto id = group.id;
    const auto area = group.area;
    groups_.emplace(id, std::move(group));
    Record({0, PopulationChangeKind::GroupCreated, id, {}, {}, area, {}, revision_});
    return foundation::Result<PopulationGroupId>::Success(id);
}

foundation::Result<PopulationUnitId> PopulationService::CreateUnit(PopulationUnit unit, GameplayContext context)
{
    if (!unit.group.IsValid() || !groups_.contains(unit.group))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_unit_group", "invalid population group"));
    if (unit.template_id.IsValid() && !templates_.contains(unit.template_id))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.unknown_template", "unknown population template"));
    if (IsTerminalUnitState(unit.state))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_unit_state", "invalid initial population unit state"));
    if (unit.state == PopulationUnitState::Materialized && (!unit.entity || !unit.entity->IsValid()))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_entity_link", "materialized unit requires an entity binding"));
    if (unit.state != PopulationUnitState::Materialized && unit.entity)
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_entity_link", "abstract population unit cannot own an entity binding"));
    if (unit.entity && unit_by_entity_.contains(*unit.entity))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.entity_already_bound", "entity is already bound to another population unit"));

    if (!unit.id.IsValid())
        unit.id = PopulationUnitId{unit_ids_.Next()};
    if (!unit.id.IsValid())
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.id_exhausted", "population unit id generator exhausted"));
    if (units_.contains(unit.id))
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.duplicate_unit", "duplicate population unit"));

    AdvanceGeneratorPast(unit_ids_, unit.id);
    if (!unit.current_area.IsValid())
        unit.current_area = groups_.at(unit.group).area;
    if (!unit.home_area.IsValid())
        unit.home_area = unit.current_area;
    if (!unit.current_area.IsValid() || !unit.home_area.IsValid())
        return foundation::Result<PopulationUnitId>::Failure(
            Error("gameplay.population.invalid_unit_area", "population unit requires valid current and home areas"));

    Bump();
    unit.revision = revision_;
    const auto id = unit.id;
    const auto group = unit.group;
    const auto entity = unit.entity.value_or(GameplayObjectRef{});
    const auto area = unit.current_area;
    units_.emplace(id, unit);
    IndexUnit(unit);
    if (unit.entity)
        unit_by_entity_.emplace(*unit.entity, id);
    RecountGroup(group);
    Record({0, PopulationChangeKind::UnitCreated, group, id, entity, area, context, revision_});
    return foundation::Result<PopulationUnitId>::Success(id);
}

foundation::Result<void> PopulationService::BindEntity(PopulationUnit &unit, GameplayObjectRef entity)
{
    if (!entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_entity_link", "invalid entity link"));
    auto existing = unit_by_entity_.find(entity);
    if (existing != unit_by_entity_.end() && existing->second != unit.id)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.entity_already_bound", "entity is already bound to another population unit"));

    if (unit.entity && *unit.entity != entity)
        unit_by_entity_.erase(*unit.entity);
    unit.entity = entity;
    unit_by_entity_[entity] = unit.id;
    return foundation::Result<void>::Success();
}

void PopulationService::RemoveEntityBinding(PopulationUnit &unit) noexcept
{
    if (!unit.entity)
        return;
    unit_by_entity_.erase(*unit.entity);
    unit.entity.reset();
}

foundation::Result<void> PopulationService::SetUnitEntity(PopulationUnitId id, GameplayObjectRef entity,
                                                           GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (unit->state != PopulationUnitState::Materialized)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_entity_link_state", "entity binding is only valid for materialized units"));
    auto bound = BindEntity(*unit, entity);
    if (!bound)
        return bound;

    Bump();
    unit->revision = revision_;
    Record({0, PopulationChangeKind::EntityBindingChanged, unit->group, id, entity, unit->current_area, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::MaterializeUnit(PopulationUnitId id, GameplayObjectRef entity,
                                                             GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit || !entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_materialize", "invalid materialization request"));
    if (!CanMaterialize(unit->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_materialize_state", "population unit cannot be materialized from its current state"));
    if (unit->entity)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_materialize_binding", "abstract population unit already has an entity binding"));

    auto bound = BindEntity(*unit, entity);
    if (!bound)
        return bound;

    UnindexUnit(*unit);
    unit->state = PopulationUnitState::Materialized;
    Bump();
    unit->revision = revision_;
    IndexUnit(*unit);
    ++diagnostics_.materialization_requests;
    RecountGroup(unit->group);
    Record({0, PopulationChangeKind::UnitMaterialized, unit->group, id, entity, unit->current_area, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::DematerializeUnit(PopulationUnitId id, GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (unit->state != PopulationUnitState::Materialized || !unit->entity)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_dematerialize_state", "only a materialized unit can be dematerialized"));

    const auto old_entity = *unit->entity;
    UnindexUnit(*unit);
    RemoveEntityBinding(*unit);
    unit->state = PopulationUnitState::Abstract;
    Bump();
    unit->revision = revision_;
    IndexUnit(*unit);
    ++diagnostics_.dematerialization_requests;
    RecountGroup(unit->group);
    Record({0, PopulationChangeKind::UnitDematerialized, unit->group, id, old_entity, unit->current_area, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::MarkUnitDead(PopulationUnitId id, GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (IsTerminalUnitState(unit->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_terminal_transition", "population unit is already terminal"));

    const auto old_entity = unit->entity.value_or(GameplayObjectRef{});
    UnindexUnit(*unit);
    RemoveEntityBinding(*unit);
    unit->state = PopulationUnitState::Dead;
    Bump();
    unit->revision = revision_;
    IndexUnit(*unit);

    if (auto active = active_migration_by_unit_.find(id); active != active_migration_by_unit_.end())
    {
        auto migration = migrations_.find(active->second);
        if (migration != migrations_.end())
        {
            migration->second.state = MigrationState::Failed;
            migration->second.revision = revision_;
        }
        active_migration_by_unit_.erase(active);
    }
    if (auto residence = active_residence_by_unit_.find(id); residence != active_residence_by_unit_.end())
    {
        auto record = residences_.find(residence->second);
        if (record != residences_.end() && record->second.state == ResidenceState::Assigned)
        {
            record->second.state = ResidenceState::Abandoned;
            record->second.revision = revision_;
        }
        active_residence_by_unit_.erase(residence);
    }
    if (auto allocation = active_allocation_by_unit_.find(id); allocation != active_allocation_by_unit_.end())
    {
        auto record = allocations_.find(allocation->second);
        if (record != allocations_.end() && record->second.state == PopulationAllocationState::Active)
        {
            record->second.state = PopulationAllocationState::Released;
            record->second.revision = revision_;
            Record({0, PopulationChangeKind::AllocationReleased, unit->group, id, {}, unit->current_area, context,
                    revision_});
        }
        active_allocation_by_unit_.erase(allocation);
    }

    RecountGroup(unit->group);
    Record({0, PopulationChangeKind::UnitDied, unit->group, id, old_entity, unit->current_area, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::RetireUnit(PopulationUnitId id, GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (unit->state == PopulationUnitState::Removed)
        return foundation::Result<void>::Success();
    if (unit->state == PopulationUnitState::Dead)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_terminal_transition", "terminal population unit cannot be retired"));

    const auto old_entity = unit->entity.value_or(GameplayObjectRef{});
    UnindexUnit(*unit);
    RemoveEntityBinding(*unit);
    unit->state = PopulationUnitState::Removed;
    Bump();
    unit->revision = revision_;
    IndexUnit(*unit);

    if (auto active = active_migration_by_unit_.find(id); active != active_migration_by_unit_.end())
    {
        auto migration = migrations_.find(active->second);
        if (migration != migrations_.end())
        {
            migration->second.state = MigrationState::Cancelled;
            migration->second.revision = revision_;
        }
        active_migration_by_unit_.erase(active);
    }
    if (auto residence = active_residence_by_unit_.find(id); residence != active_residence_by_unit_.end())
    {
        auto record = residences_.find(residence->second);
        if (record != residences_.end() && record->second.state == ResidenceState::Assigned)
        {
            record->second.state = ResidenceState::Abandoned;
            record->second.revision = revision_;
        }
        active_residence_by_unit_.erase(residence);
    }
    if (auto allocation = active_allocation_by_unit_.find(id); allocation != active_allocation_by_unit_.end())
    {
        auto record = allocations_.find(allocation->second);
        if (record != allocations_.end() && record->second.state == PopulationAllocationState::Active)
        {
            record->second.state = PopulationAllocationState::Released;
            record->second.revision = revision_;
            Record({0, PopulationChangeKind::AllocationReleased, unit->group, id, {}, unit->current_area, context,
                    revision_});
        }
        active_allocation_by_unit_.erase(allocation);
    }

    RecountGroup(unit->group);
    Record({0, PopulationChangeKind::UnitRetired, unit->group, id, old_entity, unit->current_area, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<PopulationResidenceId> PopulationService::AssignResidence(PopulationResidence residence,
                                                                              GameplayContext context)
{
    auto *unit = FindMutableUnit(residence.unit);
    if (!unit || !residence.home_area.IsValid() || IsTerminalUnitState(unit->state))
        return foundation::Result<PopulationResidenceId>::Failure(
            Error("gameplay.population.invalid_residence", "invalid residence"));

    auto existing = active_residence_by_unit_.find(residence.unit);
    if (existing != active_residence_by_unit_.end())
    {
        auto it = residences_.find(existing->second);
        if (it == residences_.end() || it->second.state != ResidenceState::Assigned)
            return foundation::Result<PopulationResidenceId>::Failure(
                Error("gameplay.population.residence_index_corrupt", "active residence index is inconsistent"));

        Bump();
        it->second.home_area = residence.home_area;
        it->second.home_property = residence.home_property;
        it->second.revision = revision_;
        unit->home_area = residence.home_area;
        unit->revision = revision_;
        Record({0, PopulationChangeKind::ResidenceChanged, unit->group, unit->id,
                unit->entity.value_or(GameplayObjectRef{}), residence.home_area, context, revision_});
        return foundation::Result<PopulationResidenceId>::Success(it->second.id);
    }

    if (!residence.id.IsValid())
        residence.id = PopulationResidenceId{residence_ids_.Next()};
    if (!residence.id.IsValid())
        return foundation::Result<PopulationResidenceId>::Failure(
            Error("gameplay.population.id_exhausted", "population residence id generator exhausted"));
    if (residences_.contains(residence.id))
        return foundation::Result<PopulationResidenceId>::Failure(
            Error("gameplay.population.duplicate_residence", "duplicate residence"));

    AdvanceGeneratorPast(residence_ids_, residence.id);
    residence.state = ResidenceState::Assigned;
    Bump();
    residence.revision = revision_;
    unit->home_area = residence.home_area;
    unit->revision = revision_;
    const auto id = residence.id;
    const auto area = residence.home_area;
    residences_.emplace(id, residence);
    active_residence_by_unit_[residence.unit] = id;
    Record({0, PopulationChangeKind::ResidenceAssigned, unit->group, unit->id,
            unit->entity.value_or(GameplayObjectRef{}), area, context, revision_});
    return foundation::Result<PopulationResidenceId>::Success(id);
}

foundation::Result<PopulationMigrationId> PopulationService::StartMigration(PopulationMigration migration,
                                                                             GameplayContext context)
{
    auto *unit = FindMutableUnit(migration.unit);
    if (!unit || !migration.to.IsValid() || IsTerminalUnitState(unit ? unit->state : PopulationUnitState::Removed))
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.invalid_migration", "invalid migration"));
    if (active_migration_by_unit_.contains(migration.unit))
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.migration_already_active", "population unit already has an active migration"));

    if (!migration.from.IsValid())
        migration.from = unit->current_area;
    if (migration.from != unit->current_area)
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.migration_source_stale", "migration source area does not match unit current area"));
    if (migration.to == migration.from)
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.invalid_migration", "migration destination must differ from source"));

    if (!migration.id.IsValid())
        migration.id = PopulationMigrationId{migration_ids_.Next()};
    if (!migration.id.IsValid())
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.id_exhausted", "population migration id generator exhausted"));
    if (migrations_.contains(migration.id))
        return foundation::Result<PopulationMigrationId>::Failure(
            Error("gameplay.population.duplicate_migration", "duplicate migration"));

    AdvanceGeneratorPast(migration_ids_, migration.id);
    migration.state = MigrationState::Active;
    if (migration.started_at.ticks == 0)
        migration.started_at = context.time;
    Bump();
    migration.revision = revision_;
    const auto id = migration.id;
    const auto destination = migration.to;
    migrations_.emplace(id, migration);
    active_migration_by_unit_[migration.unit] = id;
    Record({0, PopulationChangeKind::MigrationStarted, unit->group, unit->id,
            unit->entity.value_or(GameplayObjectRef{}), destination, context, revision_});
    return foundation::Result<PopulationMigrationId>::Success(id);
}

foundation::Result<void> PopulationService::FinishMigration(PopulationMigrationId id, MigrationState final_state,
                                                             GameplayContext context)
{
    auto it = migrations_.find(id);
    if (it == migrations_.end())
        return foundation::Result<void>::Failure(Error("gameplay.population.migration_missing", "migration missing"));
    if (it->second.state != MigrationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.migration_not_active", "migration is not active"));

    auto *unit = FindMutableUnit(it->second.unit);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (IsTerminalUnitState(unit->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_migration_unit_state", "terminal population unit cannot finish migration"));

    if (final_state == MigrationState::Completed && unit->current_area != it->second.from)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.migration_source_stale", "population unit moved since migration started"));

    Bump();
    it->second.state = final_state;
    it->second.revision = revision_;
    active_migration_by_unit_.erase(unit->id);

    PopulationChangeKind kind = PopulationChangeKind::MigrationFailed;
    GameplayObjectRef area = unit->current_area;
    if (final_state == MigrationState::Completed)
    {
        UnindexUnit(*unit);
        unit->current_area = it->second.to;
        unit->revision = revision_;
        IndexUnit(*unit);
        area = unit->current_area;
        kind = PopulationChangeKind::MigrationCompleted;
    }
    else if (final_state == MigrationState::Cancelled)
    {
        kind = PopulationChangeKind::MigrationCancelled;
    }

    Record({0, kind, unit->group, unit->id, unit->entity.value_or(GameplayObjectRef{}), area, context, revision_});
    if (final_state == MigrationState::Completed)
        Record({0, PopulationChangeKind::UnitMigrated, unit->group, unit->id,
                unit->entity.value_or(GameplayObjectRef{}), area, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::CompleteMigration(PopulationMigrationId id, GameplayContext context)
{
    return FinishMigration(id, MigrationState::Completed, context);
}

foundation::Result<void> PopulationService::CancelMigration(PopulationMigrationId id, GameplayContext context)
{
    return FinishMigration(id, MigrationState::Cancelled, context);
}

foundation::Result<void> PopulationService::FailMigration(PopulationMigrationId id, GameplayContext context)
{
    return FinishMigration(id, MigrationState::Failed, context);
}

foundation::Result<PopulationAllocationBatch> PopulationService::ReserveAllocations(
    PopulationAllocationRequest request, GameplayContext context)
{
    if (!request.purpose.IsValid() || request.units.empty())
        return foundation::Result<PopulationAllocationBatch>::Failure(
            Error("gameplay.population.invalid_allocation", "population allocation requires a purpose and units"));

    std::vector<PopulationUnitId> canonical_units = request.units;
    std::sort(canonical_units.begin(), canonical_units.end());
    if (std::adjacent_find(canonical_units.begin(), canonical_units.end()) != canonical_units.end())
        return foundation::Result<PopulationAllocationBatch>::Failure(
            Error("gameplay.population.duplicate_allocation_unit", "population allocation contains duplicate units"));

    if (request.correlation.IsValid())
    {
        auto existing_index = allocations_by_correlation_.find(request.correlation);
        if (existing_index != allocations_by_correlation_.end())
        {
            std::vector<PopulationUnitId> existing_units;
            PopulationAllocationBatch existing_batch;
            existing_batch.correlation = request.correlation;
            for (const auto allocation_id : existing_index->second)
            {
                auto allocation = allocations_.find(allocation_id);
                if (allocation == allocations_.end() || allocation->second.purpose != request.purpose ||
                    allocation->second.state == PopulationAllocationState::Released)
                    return foundation::Result<PopulationAllocationBatch>::Failure(
                        Error("gameplay.population.allocation_correlation_conflict",
                              "population allocation correlation already belongs to a different or released allocation"));
                existing_units.push_back(allocation->second.unit);
                existing_batch.tokens.push_back({allocation->second.id, allocation->second.unit});
            }
            std::sort(existing_units.begin(), existing_units.end());
            if (existing_units != canonical_units)
                return foundation::Result<PopulationAllocationBatch>::Failure(
                    Error("gameplay.population.allocation_correlation_conflict",
                          "population allocation correlation payload does not match the existing allocation"));
            std::sort(existing_batch.tokens.begin(), existing_batch.tokens.end(), [](const auto &a, const auto &b) {
                return a.allocation < b.allocation;
            });
            return foundation::Result<PopulationAllocationBatch>::Success(std::move(existing_batch));
        }
    }

    for (const auto unit_id : canonical_units)
    {
        const auto *unit = GetUnit(unit_id);
        if (!unit || !CanMaterialize(unit->state) || unit->entity)
            return foundation::Result<PopulationAllocationBatch>::Failure(
                Error("gameplay.population.unit_not_allocatable", "population unit cannot be allocated"));
        if (active_allocation_by_unit_.contains(unit_id))
            return foundation::Result<PopulationAllocationBatch>::Failure(
                Error("gameplay.population.unit_already_allocated", "population unit already has an active allocation"));
    }

    if (!request.correlation.IsValid())
    {
        request.correlation = allocation_correlation_ids_.Next();
        if (!request.correlation.IsValid())
            return foundation::Result<PopulationAllocationBatch>::Failure(
                Error("gameplay.population.id_exhausted", "population allocation correlation id generator exhausted"));
    }

    const auto generator_before = allocation_ids_.GetSnapshot();
    std::vector<PopulationAllocationId> generated_ids;
    generated_ids.reserve(canonical_units.size());
    for (std::size_t i = 0; i < canonical_units.size(); ++i)
    {
        auto id = PopulationAllocationId{allocation_ids_.Next()};
        if (!id.IsValid())
        {
            allocation_ids_.Restore(generator_before);
            return foundation::Result<PopulationAllocationBatch>::Failure(
                Error("gameplay.population.id_exhausted", "population allocation id generator exhausted"));
        }
        generated_ids.push_back(id);
    }

    Bump();
    PopulationAllocationBatch batch;
    batch.correlation = request.correlation;
    auto &correlation_index = allocations_by_correlation_[request.correlation];
    correlation_index.reserve(correlation_index.size() + canonical_units.size());
    for (std::size_t i = 0; i < canonical_units.size(); ++i)
    {
        PopulationAllocation allocation;
        allocation.id = generated_ids[i];
        allocation.unit = canonical_units[i];
        allocation.purpose = request.purpose;
        allocation.correlation = request.correlation;
        allocation.expires_at = request.expires_at;
        allocation.revision = revision_;
        allocations_.emplace(allocation.id, allocation);
        active_allocation_by_unit_[allocation.unit] = allocation.id;
        correlation_index.push_back(allocation.id);
        batch.tokens.push_back({allocation.id, allocation.unit});
        const auto *unit = GetUnit(allocation.unit);
        Record({0, PopulationChangeKind::AllocationReserved, unit ? unit->group : PopulationGroupId{}, allocation.unit,
                {}, unit ? unit->current_area : GameplayObjectRef{}, context, revision_});
    }
    std::sort(batch.tokens.begin(), batch.tokens.end(), [](const auto &a, const auto &b) {
        return a.allocation < b.allocation;
    });
    return foundation::Result<PopulationAllocationBatch>::Success(std::move(batch));
}

foundation::Result<void> PopulationService::CommitAllocation(PopulationAllocationId id, GameplayObjectRef entity,
                                                              GameplayContext context)
{
    auto allocation = allocations_.find(id);
    if (allocation == allocations_.end() || !entity.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_missing", "population allocation missing or entity invalid"));
    if (allocation->second.state == PopulationAllocationState::Committed)
    {
        if (allocation->second.bound_entity == entity)
            return foundation::Result<void>::Success();
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_commit_conflict", "population allocation is already committed to another entity"));
    }
    if (allocation->second.state != PopulationAllocationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_not_active", "population allocation is not active"));

    auto active = active_allocation_by_unit_.find(allocation->second.unit);
    if (active == active_allocation_by_unit_.end() || active->second != id)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_index_corrupt", "population allocation index is inconsistent"));

    auto *unit = FindMutableUnit(allocation->second.unit);
    if (!unit || !CanMaterialize(unit->state) || unit->entity)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.unit_not_allocatable", "population unit cannot commit the allocation"));
    auto existing = unit_by_entity_.find(entity);
    if (existing != unit_by_entity_.end() && existing->second != unit->id)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.entity_already_bound", "entity is already bound to another population unit"));

    UnindexUnit(*unit);
    auto bound = BindEntity(*unit, entity);
    if (!bound)
    {
        IndexUnit(*unit);
        return bound;
    }
    unit->state = PopulationUnitState::Materialized;
    Bump();
    unit->revision = revision_;
    allocation->second.state = PopulationAllocationState::Committed;
    allocation->second.bound_entity = entity;
    allocation->second.revision = revision_;
    active_allocation_by_unit_.erase(active);
    IndexUnit(*unit);
    ++diagnostics_.materialization_requests;
    RecountGroup(unit->group);
    Record({0, PopulationChangeKind::UnitMaterialized, unit->group, unit->id, entity, unit->current_area, context,
            revision_});
    Record({0, PopulationChangeKind::AllocationCommitted, unit->group, unit->id, entity, unit->current_area, context,
            revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::ReleaseAllocation(PopulationAllocationId id, GameplayContext context)
{
    auto allocation = allocations_.find(id);
    if (allocation == allocations_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_missing", "population allocation missing"));
    if (allocation->second.state == PopulationAllocationState::Released)
        return foundation::Result<void>::Success();
    if (allocation->second.state == PopulationAllocationState::Committed)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.allocation_already_committed", "committed population allocation cannot be released"));

    auto *unit = FindMutableUnit(allocation->second.unit);
    Bump();
    allocation->second.state = PopulationAllocationState::Released;
    allocation->second.revision = revision_;
    active_allocation_by_unit_.erase(allocation->second.unit);
    Record({0, PopulationChangeKind::AllocationReleased, unit ? unit->group : PopulationGroupId{}, allocation->second.unit,
            {}, unit ? unit->current_area : GameplayObjectRef{}, context, revision_});
    return foundation::Result<void>::Success();
}

void PopulationService::PruneTerminalAllocations(GameplayObjectId correlation)
{
    auto index = allocations_by_correlation_.find(correlation);
    if (index == allocations_by_correlation_.end())
        return;
    for (const auto id : index->second)
    {
        auto allocation = allocations_.find(id);
        if (allocation != allocations_.end() && allocation->second.state == PopulationAllocationState::Active)
            return;
    }
    bool changed = false;
    for (const auto id : index->second)
        changed = allocations_.erase(id) != 0 || changed;
    allocations_by_correlation_.erase(index);
    if (changed)
        Bump();
}

const PopulationGroup *PopulationService::GetGroup(PopulationGroupId id) const noexcept
{
    auto it = groups_.find(id);
    return it == groups_.end() ? nullptr : &it->second;
}

const PopulationUnit *PopulationService::GetUnit(PopulationUnitId id) const noexcept
{
    auto it = units_.find(id);
    return it == units_.end() ? nullptr : &it->second;
}

const PopulationTemplate *PopulationService::GetTemplate(PopulationTemplateId id) const noexcept
{
    auto it = templates_.find(id);
    return it == templates_.end() ? nullptr : &it->second;
}

const PopulationAllocation *PopulationService::GetAllocation(PopulationAllocationId id) const noexcept
{
    auto it = allocations_.find(id);
    return it == allocations_.end() ? nullptr : &it->second;
}

const PopulationAllocation *PopulationService::GetActiveAllocationForUnit(PopulationUnitId unit) const noexcept
{
    auto index = active_allocation_by_unit_.find(unit);
    return index == active_allocation_by_unit_.end() ? nullptr : GetAllocation(index->second);
}

const PopulationUnit *PopulationService::FindUnitByEntity(GameplayObjectRef entity) const noexcept
{
    auto it = unit_by_entity_.find(entity);
    return it == unit_by_entity_.end() ? nullptr : GetUnit(it->second);
}

std::vector<PopulationAllocation> PopulationService::FindAllocationsByCorrelation(GameplayObjectId correlation) const
{
    std::vector<PopulationAllocation> out;
    auto index = allocations_by_correlation_.find(correlation);
    if (index == allocations_by_correlation_.end())
        return out;
    out.reserve(index->second.size());
    for (const auto id : index->second)
    {
        auto allocation = allocations_.find(id);
        if (allocation != allocations_.end())
            out.push_back(allocation->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PopulationGroup> PopulationService::FindGroupsInArea(GameplayObjectRef area) const
{
    std::vector<PopulationGroup> out;
    for (const auto &[id, group] : groups_)
    {
        (void)id;
        if (group.area == area)
            out.push_back(group);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PopulationUnit> PopulationService::FindUnitsByGroup(PopulationGroupId group) const
{
    std::vector<PopulationUnit> out;
    auto index = units_by_group_.find(group);
    if (index == units_by_group_.end())
        return out;
    out.reserve(index->second.size());
    for (const auto id : index->second)
        if (auto it = units_.find(id); it != units_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PopulationUnit> PopulationService::FindUnitsByState(PopulationUnitState state) const
{
    std::vector<PopulationUnit> out;
    const auto index = StateIndex(state);
    if (index >= units_by_state_.size())
        return out;
    out.reserve(units_by_state_[index].size());
    for (const auto id : units_by_state_[index])
        if (auto it = units_.find(id); it != units_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.group != b.group)
            return a.group < b.group;
        return a.id < b.id;
    });
    return out;
}

std::vector<PopulationUnit> PopulationService::FindUnitsInArea(GameplayObjectRef area) const
{
    std::vector<PopulationUnit> out;
    auto index = units_by_area_.find(area);
    if (index == units_by_area_.end())
        return out;
    out.reserve(index->second.size());
    for (const auto id : index->second)
    {
        auto unit = units_.find(id);
        if (unit != units_.end())
            out.push_back(unit->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PopulationUnit> PopulationService::FindResidentsOfArea(GameplayObjectRef area) const
{
    std::vector<PopulationUnit> out;
    for (const auto &[id, residence] : residences_)
    {
        (void)id;
        if (residence.state != ResidenceState::Assigned || residence.home_area != area)
            continue;
        if (auto unit = units_.find(residence.unit); unit != units_.end())
            out.push_back(unit->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PopulationUnit> PopulationService::FindMaterializationCandidates(GameplayObjectRef area,
                                                                             std::size_t limit) const
{
    std::vector<PopulationUnit> out;
    auto index = units_by_area_.find(area);
    if (index == units_by_area_.end())
        return out;
    out.reserve(std::min(limit, index->second.size()));
    for (const auto id : index->second)
    {
        auto it = units_.find(id);
        if (it != units_.end() && CanMaterialize(it->second.state))
            out.push_back(it->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

PopulationCounts PopulationService::GetPopulationCounts(PopulationGroupId group) const
{
    PopulationCounts counts;
    auto add = [&counts](PopulationUnitState state) {
        switch (state)
        {
        case PopulationUnitState::Latent: ++counts.latent; break;
        case PopulationUnitState::Abstract: ++counts.abstract_units; break;
        case PopulationUnitState::Materialized: ++counts.materialized; break;
        case PopulationUnitState::Dead: ++counts.dead; break;
        case PopulationUnitState::Removed: ++counts.removed; break;
        }
    };

    if (!group.IsValid())
    {
        for (const auto &[id, unit] : units_)
        {
            (void)id;
            add(unit.state);
        }
        return counts;
    }

    auto index = units_by_group_.find(group);
    if (index == units_by_group_.end())
        return counts;
    for (const auto id : index->second)
        if (auto it = units_.find(id); it != units_.end())
            add(it->second.state);
    return counts;
}

PopulationChangeBatch PopulationService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    PopulationChangeBatch batch;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                        : next_change_sequence_ - 1;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (next_change_sequence_ == 0 || sequence > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    if (changes_.empty())
    {
        batch.snapshot_required = sequence < batch.latest_sequence;
        return batch;
    }
    if (sequence < batch.oldest_available_sequence && batch.oldest_available_sequence - sequence > 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [sequence](const auto &change) { return change.sequence > sequence; });
    return batch;
}

std::vector<PopulationChange> PopulationService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

void PopulationService::PruneChangesThrough(std::uint64_t sequence)
{
    while (!changes_.empty() && changes_.front().sequence <= sequence)
        changes_.pop_front();
}

PopulationSnapshot PopulationService::CaptureSnapshot() const
{
    PopulationSnapshot snapshot;
    std::unordered_set<PopulationUnitId, IdHash> persisted_units;
    std::unordered_set<GameplayObjectId> active_allocation_correlations;

    for (const auto &[id, group] : groups_)
    {
        (void)id;
        snapshot.groups.push_back(group);
    }
    for (const auto &[id, unit] : units_)
    {
        bool persistent = true;
        if (unit.template_id.IsValid())
        {
            auto templ = templates_.find(unit.template_id);
            persistent = templ != templates_.end() && templ->second.persistent;
        }
        if (!persistent)
            continue;
        snapshot.units.push_back(unit);
        persisted_units.insert(id);
    }
    for (const auto &[id, residence] : residences_)
    {
        (void)id;
        if (persisted_units.contains(residence.unit))
            snapshot.residences.push_back(residence);
    }
    for (const auto &[id, migration] : migrations_)
    {
        (void)id;
        if (persisted_units.contains(migration.unit))
            snapshot.migrations.push_back(migration);
    }
    for (const auto &[id, allocation] : allocations_)
    {
        (void)id;
        if (allocation.state == PopulationAllocationState::Active && persisted_units.contains(allocation.unit))
            active_allocation_correlations.insert(allocation.correlation);
    }
    for (const auto &[id, allocation] : allocations_)
    {
        (void)id;
        if (persisted_units.contains(allocation.unit) && active_allocation_correlations.contains(allocation.correlation))
            snapshot.allocations.push_back(allocation);
    }

    std::sort(snapshot.groups.begin(), snapshot.groups.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.units.begin(), snapshot.units.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.residences.begin(), snapshot.residences.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.migrations.begin(), snapshot.migrations.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.allocations.begin(), snapshot.allocations.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.group_ids = group_ids_.GetSnapshot();
    snapshot.unit_ids = unit_ids_.GetSnapshot();
    snapshot.residence_ids = residence_ids_.GetSnapshot();
    snapshot.migration_ids = migration_ids_.GetSnapshot();
    snapshot.allocation_ids = allocation_ids_.GetSnapshot();
    snapshot.allocation_correlation_ids = allocation_correlation_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> PopulationService::RestoreSnapshot(PopulationSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    // Pre-allocation snapshots have zero-initialized generator fields. They are safe to accept only
    // when no allocation records are present; the current empty allocation generators then remain canonical.
    auto allocation_id_snapshot = snapshot.allocation_ids;
    auto allocation_correlation_snapshot = snapshot.allocation_correlation_ids;
    if (snapshot.allocations.empty() && allocation_id_snapshot.scope == 0)
        allocation_id_snapshot = allocation_ids_.GetSnapshot();
    if (snapshot.allocations.empty() && allocation_correlation_snapshot.scope == 0)
        allocation_correlation_snapshot = allocation_correlation_ids_.GetSnapshot();

    std::unordered_map<PopulationGroupId, PopulationGroup, IdHash> new_groups;
    std::unordered_map<PopulationUnitId, PopulationUnit, IdHash> new_units;
    std::unordered_map<PopulationResidenceId, PopulationResidence, IdHash> new_residences;
    std::unordered_map<PopulationMigrationId, PopulationMigration, IdHash> new_migrations;
    std::unordered_map<PopulationAllocationId, PopulationAllocation, IdHash> new_allocations;
    std::unordered_map<GameplayObjectRef, PopulationUnitId> new_unit_by_entity;
    std::unordered_map<PopulationUnitId, PopulationMigrationId, IdHash> new_active_migrations;
    std::unordered_map<PopulationUnitId, PopulationResidenceId, IdHash> new_active_residences;
    std::unordered_map<PopulationUnitId, PopulationAllocationId, IdHash> new_active_allocations;
    std::unordered_map<GameplayObjectId, std::vector<PopulationAllocationId>> new_allocations_by_correlation;

    std::vector<PopulationGroupId> group_ids;
    std::vector<PopulationUnitId> unit_ids;
    std::vector<PopulationResidenceId> residence_ids;
    std::vector<PopulationMigrationId> migration_ids;
    std::vector<PopulationAllocationId> allocation_ids;
    std::vector<PopulationAllocationId> allocation_correlations;

    for (auto group : snapshot.groups)
    {
        if (!group.id.IsValid() || !group.area.IsValid() || group.revision > snapshot.revision ||
            !new_groups.emplace(group.id, group).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid or duplicate population group snapshot"));
        group_ids.push_back(group.id);
    }

    for (auto unit : snapshot.units)
    {
        if (!unit.id.IsValid() || !unit.group.IsValid() || !new_groups.contains(unit.group) ||
            !unit.current_area.IsValid() || !unit.home_area.IsValid() || unit.revision > snapshot.revision)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid population unit snapshot"));
        if (unit.template_id.IsValid() && !templates_.contains(unit.template_id))
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_unknown_template", "population unit references an unknown current template"));
        if (unit.state == PopulationUnitState::Materialized)
        {
            if (!unit.entity || !unit.entity->IsValid() || !new_unit_by_entity.emplace(*unit.entity, unit.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population.restore_invalid_binding", "invalid or duplicate population entity binding"));
        }
        else if (unit.entity)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid_binding", "non-materialized population unit has an entity binding"));
        }
        if (!new_units.emplace(unit.id, unit).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "duplicate population unit snapshot"));
        unit_ids.push_back(unit.id);
    }

    for (auto residence : snapshot.residences)
    {
        if (!residence.id.IsValid() || !residence.unit.IsValid() || !new_units.contains(residence.unit) ||
            !residence.home_area.IsValid() || residence.revision > snapshot.revision ||
            !new_residences.emplace(residence.id, residence).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid or duplicate population residence snapshot"));
        if (residence.state == ResidenceState::Assigned &&
            !new_active_residences.emplace(residence.unit, residence.id).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_multiple_residences", "population unit has multiple active residences"));
        residence_ids.push_back(residence.id);
    }

    for (auto migration : snapshot.migrations)
    {
        if (!migration.id.IsValid() || !migration.unit.IsValid() || !new_units.contains(migration.unit) ||
            !migration.from.IsValid() || !migration.to.IsValid() || migration.from == migration.to ||
            migration.revision > snapshot.revision || !new_migrations.emplace(migration.id, migration).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid or duplicate population migration snapshot"));
        if (migration.state == MigrationState::Active)
        {
            const auto &unit = new_units.at(migration.unit);
            if (IsTerminalUnitState(unit.state) || unit.current_area != migration.from ||
                !new_active_migrations.emplace(migration.unit, migration.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population.restore_invalid_migration", "invalid active population migration snapshot"));
        }
        migration_ids.push_back(migration.id);
    }

    for (auto allocation : snapshot.allocations)
    {
        if (!allocation.id.IsValid() || !allocation.unit.IsValid() || !new_units.contains(allocation.unit) ||
            !allocation.purpose.IsValid() || !allocation.correlation.IsValid() ||
            allocation.revision > snapshot.revision || !new_allocations.emplace(allocation.id, allocation).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid_allocation", "invalid or duplicate population allocation snapshot"));
        if (allocation.state == PopulationAllocationState::Active)
        {
            const auto &unit = new_units.at(allocation.unit);
            if (!CanMaterialize(unit.state) || unit.entity ||
                !new_active_allocations.emplace(allocation.unit, allocation.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.population.restore_invalid_allocation", "invalid active population allocation snapshot"));
        }
        if (allocation.state == PopulationAllocationState::Committed && !allocation.bound_entity.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid_allocation", "committed population allocation has no entity"));
        if (allocation.state != PopulationAllocationState::Committed && allocation.bound_entity.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid_allocation", "uncommitted population allocation has an entity"));
        new_allocations_by_correlation[allocation.correlation].push_back(allocation.id);
        allocation_ids.push_back(allocation.id);
        allocation_correlations.push_back(PopulationAllocationId{allocation.correlation});
    }

    const auto group_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.group_ids, group_ids_.Scope(), MaxLowForScope(group_ids, group_ids_.Scope()));
    const auto unit_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.unit_ids, unit_ids_.Scope(), MaxLowForScope(unit_ids, unit_ids_.Scope()));
    const auto residence_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.residence_ids, residence_ids_.Scope(), MaxLowForScope(residence_ids, residence_ids_.Scope()));
    const auto migration_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.migration_ids, migration_ids_.Scope(), MaxLowForScope(migration_ids, migration_ids_.Scope()));
    const auto allocation_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        allocation_id_snapshot, allocation_ids_.Scope(), MaxLowForScope(allocation_ids, allocation_ids_.Scope()));
    const auto allocation_correlation_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        allocation_correlation_snapshot, allocation_correlation_ids_.Scope(),
        MaxLowForScope(allocation_correlations, allocation_correlation_ids_.Scope()));
    if (!group_generator || !unit_generator || !residence_generator || !migration_generator || !allocation_generator ||
        !allocation_correlation_generator)
        return foundation::Result<void>::Failure(
            Error("gameplay.population.restore_generator_invalid", "invalid population id generator snapshot"));

    groups_ = std::move(new_groups);
    units_ = std::move(new_units);
    residences_ = std::move(new_residences);
    migrations_ = std::move(new_migrations);
    allocations_ = std::move(new_allocations);
    unit_by_entity_ = std::move(new_unit_by_entity);
    active_migration_by_unit_ = std::move(new_active_migrations);
    active_residence_by_unit_ = std::move(new_active_residences);
    active_allocation_by_unit_ = std::move(new_active_allocations);
    allocations_by_correlation_ = std::move(new_allocations_by_correlation);
    group_ids_.Restore(snapshot.group_ids);
    unit_ids_.Restore(snapshot.unit_ids);
    residence_ids_.Restore(snapshot.residence_ids);
    migration_ids_.Restore(snapshot.migration_ids);
    allocation_ids_.Restore(allocation_id_snapshot);
    allocation_correlation_ids_.Restore(allocation_correlation_snapshot);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    RebuildIndexes();
    for (const auto &[id, group] : groups_)
    {
        (void)group;
        RecountGroup(id);
    }
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

PopulationDiagnostics PopulationService::GetDiagnostics() const noexcept
{
    auto diagnostics = diagnostics_;
    diagnostics.groups = groups_.size();
    diagnostics.units = units_.size();
    diagnostics.migrations = migrations_.size();
    diagnostics.residences = residences_.size();
    diagnostics.allocations = allocations_.size();
    diagnostics.active_allocations = active_allocation_by_unit_.size();
    const auto counts = GetPopulationCounts();
    diagnostics.latent = counts.latent;
    diagnostics.abstract_units = counts.abstract_units;
    diagnostics.materialized = counts.materialized;
    return diagnostics;
}

void PopulationService::IndexUnit(const PopulationUnit &unit)
{
    units_by_group_[unit.group].insert(unit.id);
    units_by_area_[unit.current_area].insert(unit.id);
    if (unit.template_id.IsValid())
        units_by_template_[unit.template_id].insert(unit.id);
    const auto state = StateIndex(unit.state);
    if (state < units_by_state_.size())
        units_by_state_[state].insert(unit.id);
}

void PopulationService::UnindexUnit(const PopulationUnit &unit)
{
    if (auto it = units_by_group_.find(unit.group); it != units_by_group_.end())
    {
        it->second.erase(unit.id);
        if (it->second.empty()) units_by_group_.erase(it);
    }
    if (auto it = units_by_area_.find(unit.current_area); it != units_by_area_.end())
    {
        it->second.erase(unit.id);
        if (it->second.empty()) units_by_area_.erase(it);
    }
    if (unit.template_id.IsValid())
    {
        if (auto it = units_by_template_.find(unit.template_id); it != units_by_template_.end())
        {
            it->second.erase(unit.id);
            if (it->second.empty()) units_by_template_.erase(it);
        }
    }
    const auto state = StateIndex(unit.state);
    if (state < units_by_state_.size())
        units_by_state_[state].erase(unit.id);
}

void PopulationService::RebuildIndexes()
{
    units_by_group_.clear();
    units_by_area_.clear();
    units_by_template_.clear();
    for (auto &state : units_by_state_)
        state.clear();
    unit_by_entity_.clear();
    active_migration_by_unit_.clear();
    active_residence_by_unit_.clear();
    active_allocation_by_unit_.clear();
    allocations_by_correlation_.clear();

    for (const auto &[id, unit] : units_)
    {
        (void)id;
        IndexUnit(unit);
        if (unit.state == PopulationUnitState::Materialized && unit.entity)
            unit_by_entity_[*unit.entity] = unit.id;
    }
    for (const auto &[id, migration] : migrations_)
        if (migration.state == MigrationState::Active)
            active_migration_by_unit_[migration.unit] = id;
    for (const auto &[id, residence] : residences_)
        if (residence.state == ResidenceState::Assigned)
            active_residence_by_unit_[residence.unit] = id;
    for (const auto &[id, allocation] : allocations_)
    {
        allocations_by_correlation_[allocation.correlation].push_back(id);
        if (allocation.state == PopulationAllocationState::Active)
            active_allocation_by_unit_[allocation.unit] = id;
    }
}

void PopulationService::RecountGroup(PopulationGroupId id)
{
    auto *group = FindMutableGroup(id);
    if (!group)
        return;
    std::uint32_t known = 0;
    std::uint32_t materialized = 0;
    auto index = units_by_group_.find(id);
    if (index != units_by_group_.end())
    {
        for (const auto unit_id : index->second)
        {
            auto unit = units_.find(unit_id);
            if (unit == units_.end() || unit->second.state == PopulationUnitState::Removed)
                continue;
            ++known;
            if (unit->second.state == PopulationUnitState::Materialized)
                ++materialized;
        }
    }
    group->current_known_count = known;
    group->materialized_count = materialized;
    group->revision = revision_;
}

void PopulationService::Record(PopulationChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    while (changes_.size() > change_journal_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::population
