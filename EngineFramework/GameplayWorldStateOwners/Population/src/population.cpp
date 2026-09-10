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


[[nodiscard]] constexpr bool IsValidPopulationGroupState(PopulationGroupState state) noexcept
{
    switch(state){case PopulationGroupState::Active:case PopulationGroupState::Dormant:case PopulationGroupState::Depleted:case PopulationGroupState::Removed:return true;} return false;
}
[[nodiscard]] constexpr bool IsValidPopulationUnitState(PopulationUnitState state) noexcept
{
    switch(state){case PopulationUnitState::Latent:case PopulationUnitState::Abstract:case PopulationUnitState::Materialized:case PopulationUnitState::Dead:case PopulationUnitState::Removed:return true;} return false;
}
[[nodiscard]] constexpr bool IsValidResidenceState(ResidenceState state) noexcept
{
    switch(state){case ResidenceState::Assigned:case ResidenceState::Unavailable:case ResidenceState::Abandoned:case ResidenceState::Destroyed:return true;} return false;
}
[[nodiscard]] constexpr bool IsValidMigrationState(MigrationState state) noexcept
{
    switch(state){case MigrationState::Planned:case MigrationState::Active:case MigrationState::Completed:case MigrationState::Cancelled:case MigrationState::Failed:return true;} return false;
}
[[nodiscard]] constexpr bool IsValidPopulationAllocationState(PopulationAllocationState state) noexcept
{
    switch(state){case PopulationAllocationState::Active:case PopulationAllocationState::Committed:case PopulationAllocationState::Released:return true;} return false;
}

bool AppendStagedChange(std::deque<PopulationChange> &journal, std::uint64_t &next_sequence,
                                     PopulationChange change, std::size_t capacity)
{
    if (next_sequence == 0)
        return false;
    change.sequence = next_sequence;
    journal.push_back(std::move(change));
    if (next_sequence == std::numeric_limits<std::uint64_t>::max())
        next_sequence = 0;
    else
        ++next_sequence;
    while (journal.size() > capacity)
        journal.pop_front();
    return true;
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

bool PopulationService::CanAdvanceRevision(std::size_t count) const noexcept
{
    if(count==0)return true;
    return count <= std::numeric_limits<std::uint64_t>::max()-revision_.value;
}

bool PopulationService::CanRecordChanges(std::size_t count) const noexcept
{
    if(count==0)return true;
    if(next_change_sequence_==0)return false;
    return count-1 <= std::numeric_limits<std::uint64_t>::max()-next_change_sequence_;
}

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
    if(definitions_frozen_)return foundation::Result<void>::Failure(Error("gameplay.population.definitions_frozen","population definitions are frozen"));
    if(!definition.id.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population.invalid_template","invalid population template"));
    if(templates_.contains(definition.id))return foundation::Result<void>::Failure(Error("gameplay.population.duplicate_template","duplicate population template"));
    if(!CanAdvanceRevision())return foundation::Result<void>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    const Revision next{revision_.value+1}; definition.revision=next;
    try{templates_.emplace(definition.id,std::move(definition));}catch(...){return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","population template publication failed"));}
    revision_=next;return foundation::Result<void>::Success();
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
    if(!group.area.IsValid()||!IsValidPopulationGroupState(group.state))return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.invalid_group","invalid population group"));
    if(group.state==PopulationGroupState::Removed)return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.invalid_group_state","cannot create a removed population group"));
    auto staged_ids=group_ids_;
    if(!group.id.IsValid())group.id=PopulationGroupId{staged_ids.Next()};
    if(!group.id.IsValid())return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.id_exhausted","population group id generator exhausted"));
    if(groups_.contains(group.id))return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.duplicate_group","duplicate population group"));
    AdvanceGeneratorPast(staged_ids,group.id);
    if(!CanAdvanceRevision())return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    if(!CanRecordChanges())return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.change_sequence_exhausted","population change sequence is exhausted"));
    group.current_known_count=0;group.materialized_count=0;group.revision=Revision{revision_.value+1};const auto id=group.id;const auto area=group.area;bool inserted=false;
    try{groups_.emplace(id,group);inserted=true;Record({0,PopulationChangeKind::GroupCreated,id,{},{},area,{},group.revision});}
    catch(...){if(inserted)groups_.erase(id);return foundation::Result<PopulationGroupId>::Failure(Error("gameplay.population.publication_failed","population group publication failed"));}
    group_ids_.Restore(staged_ids.GetSnapshot());revision_=group.revision;return foundation::Result<PopulationGroupId>::Success(id);
}

foundation::Result<PopulationUnitId> PopulationService::CreateUnit(PopulationUnit unit, GameplayContext context)
{
    if(!unit.group.IsValid()||!groups_.contains(unit.group))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.invalid_unit_group","invalid population group"));
    if(unit.template_id.IsValid()&&!templates_.contains(unit.template_id))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.unknown_template","unknown population template"));
    if(!IsValidPopulationUnitState(unit.state)||IsTerminalUnitState(unit.state))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.invalid_unit_state","invalid initial population unit state"));
    if(unit.state==PopulationUnitState::Materialized&&(!unit.entity||!unit.entity->IsValid()))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.invalid_entity_link","materialized unit requires an entity binding"));
    if(unit.state!=PopulationUnitState::Materialized&&unit.entity)return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.invalid_entity_link","abstract population unit cannot own an entity binding"));
    if(unit.entity&&unit_by_entity_.contains(*unit.entity))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.entity_already_bound","entity is already bound to another population unit"));

    if(!unit.current_area.IsValid())unit.current_area=groups_.at(unit.group).area;
    if(!unit.home_area.IsValid())unit.home_area=unit.current_area;
    if(!unit.current_area.IsValid()||!unit.home_area.IsValid())return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.invalid_unit_area","population unit requires valid current and home areas"));

    auto staged_ids=unit_ids_; if(!unit.id.IsValid())unit.id=PopulationUnitId{staged_ids.Next()};
    if(!unit.id.IsValid())return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.id_exhausted","population unit id generator exhausted"));
    if(units_.contains(unit.id))return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.duplicate_unit","duplicate population unit"));
    AdvanceGeneratorPast(staged_ids,unit.id);
    if(!CanAdvanceRevision())return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    if(!CanRecordChanges())return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.change_sequence_exhausted","population change sequence is exhausted"));
    unit.revision=Revision{revision_.value+1};const auto id=unit.id;const auto group=unit.group;const auto entity=unit.entity.value_or(GameplayObjectRef{});const auto area=unit.current_area;bool inserted=false;
    try
    {
        units_.emplace(id,unit);inserted=true;IndexUnit(unit);if(unit.entity)unit_by_entity_.emplace(*unit.entity,id);
        Record({0,PopulationChangeKind::UnitCreated,group,id,entity,area,context,unit.revision});
    }
    catch(...)
    {
        if(inserted){UnindexUnit(unit);if(unit.entity)unit_by_entity_.erase(*unit.entity);units_.erase(id);}
        return foundation::Result<PopulationUnitId>::Failure(Error("gameplay.population.publication_failed","population unit publication failed"));
    }
    unit_ids_.Restore(staged_ids.GetSnapshot());revision_=unit.revision;RecountGroup(group);return foundation::Result<PopulationUnitId>::Success(id);
}

foundation::Result<void> PopulationService::BindEntity(PopulationUnit &unit, GameplayObjectRef entity)
{
    if(!entity.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population.invalid_entity_link","invalid entity link"));
    auto existing=unit_by_entity_.find(entity);if(existing!=unit_by_entity_.end()&&existing->second!=unit.id)return foundation::Result<void>::Failure(Error("gameplay.population.entity_already_bound","entity is already bound to another population unit"));
    if(unit.entity&&*unit.entity==entity)return foundation::Result<void>::Success();
    bool inserted=false;
    if(existing==unit_by_entity_.end())
    {
        try{unit_by_entity_.emplace(entity,unit.id);inserted=true;}catch(...){return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","entity binding publication failed"));}
    }
    const auto old=unit.entity;unit.entity=entity;
    if(old&&*old!=entity)unit_by_entity_.erase(*old);
    (void)inserted;return foundation::Result<void>::Success();
}

void PopulationService::RemoveEntityBinding(PopulationUnit &unit) noexcept
{
    if (!unit.entity)
        return;
    unit_by_entity_.erase(*unit.entity);
    unit.entity.reset();
}

foundation::Result<void> PopulationService::SetUnitEntity(PopulationUnitId id, GameplayObjectRef entity, GameplayContext context)
{
    auto *unit=FindMutableUnit(id);if(!unit)return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing","population unit missing"));
    if(unit->state!=PopulationUnitState::Materialized)return foundation::Result<void>::Failure(Error("gameplay.population.invalid_entity_link_state","entity binding is only valid for materialized units"));
    if(!entity.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population.invalid_entity_link","invalid entity link"));
    auto existing=unit_by_entity_.find(entity);if(existing!=unit_by_entity_.end()&&existing->second!=id)return foundation::Result<void>::Failure(Error("gameplay.population.entity_already_bound","entity is already bound to another population unit"));
    if(unit->entity&&*unit->entity==entity)return foundation::Result<void>::Success();
    if(!CanAdvanceRevision())return foundation::Result<void>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    if(!CanRecordChanges())return foundation::Result<void>::Failure(Error("gameplay.population.change_sequence_exhausted","population change sequence is exhausted"));
    const Revision next{revision_.value+1};bool inserted=false;
    try{if(existing==unit_by_entity_.end()){unit_by_entity_.emplace(entity,id);inserted=true;}Record({0,PopulationChangeKind::EntityBindingChanged,unit->group,id,entity,unit->current_area,context,next});}
    catch(...){if(inserted)unit_by_entity_.erase(entity);return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","entity binding update failed"));}
    const auto old=unit->entity;unit->entity=entity;unit->revision=next;if(old&&*old!=entity)unit_by_entity_.erase(*old);revision_=next;return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::MaterializeUnit(PopulationUnitId id, GameplayObjectRef entity, GameplayContext context)
{
    auto *unit=FindMutableUnit(id);if(!unit||!entity.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population.invalid_materialize","invalid materialization request"));
    if(!CanMaterialize(unit->state))return foundation::Result<void>::Failure(Error("gameplay.population.invalid_materialize_state","population unit cannot be materialized from its current state"));
    if(unit->entity)return foundation::Result<void>::Failure(Error("gameplay.population.invalid_materialize_binding","abstract population unit already has an entity binding"));
    if(auto e=unit_by_entity_.find(entity);e!=unit_by_entity_.end()&&e->second!=id)return foundation::Result<void>::Failure(Error("gameplay.population.entity_already_bound","entity is already bound to another population unit"));
    if(!CanAdvanceRevision())return foundation::Result<void>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    if(!CanRecordChanges())return foundation::Result<void>::Failure(Error("gameplay.population.change_sequence_exhausted","population change sequence is exhausted"));
    const Revision next{revision_.value+1};const auto old_state=unit->state;bool state_inserted=false,entity_inserted=false;
    try
    {
        state_inserted=units_by_state_[StateIndex(PopulationUnitState::Materialized)].insert(id).second;
        entity_inserted=unit_by_entity_.emplace(entity,id).second;
        Record({0,PopulationChangeKind::UnitMaterialized,unit->group,id,entity,unit->current_area,context,next});
    }
    catch(...){if(state_inserted)units_by_state_[StateIndex(PopulationUnitState::Materialized)].erase(id);if(entity_inserted)unit_by_entity_.erase(entity);return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","materialization publication failed"));}
    units_by_state_[StateIndex(old_state)].erase(id);unit->entity=entity;unit->state=PopulationUnitState::Materialized;unit->revision=next;revision_=next;++diagnostics_.materialization_requests;RecountGroup(unit->group);return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::DematerializeUnit(PopulationUnitId id, GameplayContext context)
{
    auto *unit=FindMutableUnit(id);if(!unit)return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing","population unit missing"));
    if(unit->state!=PopulationUnitState::Materialized||!unit->entity)return foundation::Result<void>::Failure(Error("gameplay.population.invalid_dematerialize_state","only a materialized unit can be dematerialized"));
    if(!CanAdvanceRevision())return foundation::Result<void>::Failure(Error("gameplay.population.revision_exhausted","population revision is exhausted"));
    if(!CanRecordChanges())return foundation::Result<void>::Failure(Error("gameplay.population.change_sequence_exhausted","population change sequence is exhausted"));
    const Revision next{revision_.value+1};const auto old_entity=*unit->entity;bool state_inserted=false;
    try{state_inserted=units_by_state_[StateIndex(PopulationUnitState::Abstract)].insert(id).second;Record({0,PopulationChangeKind::UnitDematerialized,unit->group,id,old_entity,unit->current_area,context,next});}
    catch(...){if(state_inserted)units_by_state_[StateIndex(PopulationUnitState::Abstract)].erase(id);return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","dematerialization publication failed"));}
    units_by_state_[StateIndex(PopulationUnitState::Materialized)].erase(id);unit_by_entity_.erase(old_entity);unit->entity.reset();unit->state=PopulationUnitState::Abstract;unit->revision=next;revision_=next;++diagnostics_.dematerialization_requests;RecountGroup(unit->group);return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::MarkUnitDead(PopulationUnitId id, GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (IsTerminalUnitState(unit->state))
        return foundation::Result<void>::Failure(
            Error("gameplay.population.invalid_terminal_transition", "population unit is already terminal"));

    const bool releases_allocation = active_allocation_by_unit_.contains(id);
    const std::size_t change_count = 1 + (releases_allocation ? 1u : 0u);
    if (!CanAdvanceRevision() || !CanRecordChanges(change_count))
        return foundation::Result<void>::Failure(Error(!CanAdvanceRevision() ? "gameplay.population.revision_exhausted" :
                                                      "gameplay.population.change_sequence_exhausted",
                                                      "population mutation metadata is exhausted"));
    const Revision next{revision_.value + 1};
    const auto old_entity = unit->entity.value_or(GameplayObjectRef{});

    bool inserted_dead = false;
    std::deque<PopulationChange> staged_changes;
    std::uint64_t staged_sequence = next_change_sequence_;
    try
    {
        inserted_dead = units_by_state_[StateIndex(PopulationUnitState::Dead)].insert(id).second;
        staged_changes = changes_;
        if (releases_allocation)
            AppendStagedChange(staged_changes, staged_sequence,
                               {0, PopulationChangeKind::AllocationReleased, unit->group, id, {}, unit->current_area,
                                context, next}, change_journal_capacity_);
        AppendStagedChange(staged_changes, staged_sequence,
                           {0, PopulationChangeKind::UnitDied, unit->group, id, old_entity, unit->current_area,
                            context, next}, change_journal_capacity_);
    }
    catch (...)
    {
        if (inserted_dead) units_by_state_[StateIndex(PopulationUnitState::Dead)].erase(id);
        return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed", "population death publication failed"));
    }

    units_by_state_[StateIndex(unit->state)].erase(id);
    RemoveEntityBinding(*unit);
    unit->state = PopulationUnitState::Dead;
    unit->revision = next;
    if (auto active = active_migration_by_unit_.find(id); active != active_migration_by_unit_.end())
    {
        if (auto migration = migrations_.find(active->second); migration != migrations_.end())
        { migration->second.state = MigrationState::Failed; migration->second.revision = next; }
        active_migration_by_unit_.erase(active);
    }
    if (auto residence = active_residence_by_unit_.find(id); residence != active_residence_by_unit_.end())
    {
        if (auto record = residences_.find(residence->second); record != residences_.end() && record->second.state == ResidenceState::Assigned)
        { record->second.state = ResidenceState::Abandoned; record->second.revision = next; }
        active_residence_by_unit_.erase(residence);
    }
    if (auto allocation = active_allocation_by_unit_.find(id); allocation != active_allocation_by_unit_.end())
    {
        if (auto record = allocations_.find(allocation->second); record != allocations_.end() && record->second.state == PopulationAllocationState::Active)
        { record->second.state = PopulationAllocationState::Released; record->second.revision = next; }
        active_allocation_by_unit_.erase(allocation);
    }
    changes_.swap(staged_changes); next_change_sequence_ = staged_sequence; revision_ = next;
    RecountGroup(unit->group);
    return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::RetireUnit(PopulationUnitId id, GameplayContext context)
{
    auto *unit = FindMutableUnit(id);
    if (!unit)
        return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing", "population unit missing"));
    if (unit->state == PopulationUnitState::Removed) return foundation::Result<void>::Success();
    if (unit->state == PopulationUnitState::Dead)
        return foundation::Result<void>::Failure(Error("gameplay.population.invalid_terminal_transition", "terminal population unit cannot be retired"));
    const bool releases_allocation = active_allocation_by_unit_.contains(id);
    const std::size_t change_count = 1 + (releases_allocation ? 1u : 0u);
    if (!CanAdvanceRevision() || !CanRecordChanges(change_count))
        return foundation::Result<void>::Failure(Error(!CanAdvanceRevision() ? "gameplay.population.revision_exhausted" : "gameplay.population.change_sequence_exhausted", "population mutation metadata is exhausted"));
    const Revision next{revision_.value + 1}; const auto old_entity=unit->entity.value_or(GameplayObjectRef{});
    bool inserted_removed=false; std::deque<PopulationChange> staged_changes; auto staged_sequence=next_change_sequence_;
    try {
        inserted_removed=units_by_state_[StateIndex(PopulationUnitState::Removed)].insert(id).second;
        staged_changes=changes_;
        if(releases_allocation) AppendStagedChange(staged_changes,staged_sequence,{0,PopulationChangeKind::AllocationReleased,unit->group,id,{},unit->current_area,context,next},change_journal_capacity_);
        AppendStagedChange(staged_changes,staged_sequence,{0,PopulationChangeKind::UnitRetired,unit->group,id,old_entity,unit->current_area,context,next},change_journal_capacity_);
    } catch(...) { if(inserted_removed) units_by_state_[StateIndex(PopulationUnitState::Removed)].erase(id); return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","population retirement publication failed")); }
    units_by_state_[StateIndex(unit->state)].erase(id); RemoveEntityBinding(*unit); unit->state=PopulationUnitState::Removed; unit->revision=next;
    if(auto active=active_migration_by_unit_.find(id);active!=active_migration_by_unit_.end()){ if(auto m=migrations_.find(active->second);m!=migrations_.end()){m->second.state=MigrationState::Cancelled;m->second.revision=next;} active_migration_by_unit_.erase(active); }
    if(auto r=active_residence_by_unit_.find(id);r!=active_residence_by_unit_.end()){if(auto rec=residences_.find(r->second);rec!=residences_.end()&&rec->second.state==ResidenceState::Assigned){rec->second.state=ResidenceState::Abandoned;rec->second.revision=next;}active_residence_by_unit_.erase(r);}
    if(auto a=active_allocation_by_unit_.find(id);a!=active_allocation_by_unit_.end()){if(auto rec=allocations_.find(a->second);rec!=allocations_.end()&&rec->second.state==PopulationAllocationState::Active){rec->second.state=PopulationAllocationState::Released;rec->second.revision=next;}active_allocation_by_unit_.erase(a);}
    changes_.swap(staged_changes);next_change_sequence_=staged_sequence;revision_=next;RecountGroup(unit->group);return foundation::Result<void>::Success();
}

foundation::Result<PopulationResidenceId> PopulationService::AssignResidence(PopulationResidence residence,
                                                                              GameplayContext context)
{
    auto *unit=FindMutableUnit(residence.unit);
    if(!unit||!residence.home_area.IsValid()||IsTerminalUnitState(unit->state)||!IsValidResidenceState(residence.state))
        return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.invalid_residence","invalid residence"));
    if(!CanAdvanceRevision()||!CanRecordChanges()) return foundation::Result<PopulationResidenceId>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));
    const Revision next{revision_.value+1};
    if(auto existing=active_residence_by_unit_.find(residence.unit);existing!=active_residence_by_unit_.end()){
        auto it=residences_.find(existing->second);if(it==residences_.end()||it->second.state!=ResidenceState::Assigned)return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.residence_index_corrupt","active residence index is inconsistent"));
        std::deque<PopulationChange> staged=changes_;auto seq=next_change_sequence_;
        try{AppendStagedChange(staged,seq,{0,PopulationChangeKind::ResidenceChanged,unit->group,unit->id,unit->entity.value_or(GameplayObjectRef{}),residence.home_area,context,next},change_journal_capacity_);}catch(...){return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.publication_failed","residence publication failed"));}
        it->second.home_area=residence.home_area;it->second.home_property=residence.home_property;it->second.revision=next;unit->home_area=residence.home_area;unit->revision=next;changes_.swap(staged);next_change_sequence_=seq;revision_=next;return foundation::Result<PopulationResidenceId>::Success(it->second.id);
    }
    auto staged_ids=residence_ids_; if(!residence.id.IsValid())residence.id=PopulationResidenceId{staged_ids.Next()}; if(!residence.id.IsValid())return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.id_exhausted","population residence id generator exhausted")); if(residences_.contains(residence.id))return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.duplicate_residence","duplicate residence")); AdvanceGeneratorPast(staged_ids,residence.id);
    residence.state=ResidenceState::Assigned;residence.revision=next;const auto rid=residence.id;bool primary=false,indexed=false;std::deque<PopulationChange> staged;auto seq=next_change_sequence_;
    try{residences_.reserve(residences_.size()+1);active_residence_by_unit_.reserve(active_residence_by_unit_.size()+1);staged=changes_;AppendStagedChange(staged,seq,{0,PopulationChangeKind::ResidenceAssigned,unit->group,unit->id,unit->entity.value_or(GameplayObjectRef{}),residence.home_area,context,next},change_journal_capacity_);primary=residences_.emplace(rid,residence).second;indexed=active_residence_by_unit_.emplace(residence.unit,rid).second;if(!primary||!indexed)throw 1;}catch(...){if(indexed)active_residence_by_unit_.erase(residence.unit);if(primary)residences_.erase(rid);return foundation::Result<PopulationResidenceId>::Failure(Error("gameplay.population.publication_failed","residence publication failed"));}
    unit->home_area=residence.home_area;unit->revision=next;changes_.swap(staged);next_change_sequence_=seq;residence_ids_.Restore(staged_ids.GetSnapshot());revision_=next;return foundation::Result<PopulationResidenceId>::Success(rid);
}

foundation::Result<PopulationMigrationId> PopulationService::StartMigration(PopulationMigration migration,
                                                                             GameplayContext context)
{
    auto *unit=FindMutableUnit(migration.unit);if(!unit||!migration.to.IsValid()||IsTerminalUnitState(unit?unit->state:PopulationUnitState::Removed)||!IsValidMigrationState(migration.state))return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.invalid_migration","invalid migration"));
    if(active_migration_by_unit_.contains(migration.unit))return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.migration_already_active","population unit already has an active migration"));
    if(!migration.from.IsValid())migration.from=unit->current_area;if(migration.from!=unit->current_area)return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.migration_source_stale","migration source area does not match unit current area"));if(migration.to==migration.from)return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.invalid_migration","migration destination must differ from source"));
    auto staged_ids=migration_ids_;if(!migration.id.IsValid())migration.id=PopulationMigrationId{staged_ids.Next()};if(!migration.id.IsValid())return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.id_exhausted","population migration id generator exhausted"));if(migrations_.contains(migration.id))return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.duplicate_migration","duplicate migration"));AdvanceGeneratorPast(staged_ids,migration.id);
    if(!CanAdvanceRevision()||!CanRecordChanges())return foundation::Result<PopulationMigrationId>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));const Revision next{revision_.value+1};migration.state=MigrationState::Active;if(migration.started_at.ticks==0)migration.started_at=context.time;migration.revision=next;const auto mid=migration.id;bool primary=false,indexed=false;std::deque<PopulationChange> staged;auto seq=next_change_sequence_;
    try{migrations_.reserve(migrations_.size()+1);active_migration_by_unit_.reserve(active_migration_by_unit_.size()+1);staged=changes_;AppendStagedChange(staged,seq,{0,PopulationChangeKind::MigrationStarted,unit->group,unit->id,unit->entity.value_or(GameplayObjectRef{}),migration.to,context,next},change_journal_capacity_);primary=migrations_.emplace(mid,migration).second;indexed=active_migration_by_unit_.emplace(migration.unit,mid).second;if(!primary||!indexed)throw 1;}catch(...){if(indexed)active_migration_by_unit_.erase(migration.unit);if(primary)migrations_.erase(mid);return foundation::Result<PopulationMigrationId>::Failure(Error("gameplay.population.publication_failed","migration publication failed"));}
    changes_.swap(staged);next_change_sequence_=seq;migration_ids_.Restore(staged_ids.GetSnapshot());revision_=next;return foundation::Result<PopulationMigrationId>::Success(mid);
}

foundation::Result<void> PopulationService::FinishMigration(PopulationMigrationId id, MigrationState final_state,
                                                             GameplayContext context)
{
    if(final_state!=MigrationState::Completed&&final_state!=MigrationState::Cancelled&&final_state!=MigrationState::Failed)return foundation::Result<void>::Failure(Error("gameplay.population.invalid_migration_state","invalid final migration state"));
    auto it=migrations_.find(id);if(it==migrations_.end())return foundation::Result<void>::Failure(Error("gameplay.population.migration_missing","migration missing"));if(it->second.state!=MigrationState::Active)return foundation::Result<void>::Failure(Error("gameplay.population.migration_not_active","migration is not active"));auto *unit=FindMutableUnit(it->second.unit);if(!unit)return foundation::Result<void>::Failure(Error("gameplay.population.unit_missing","population unit missing"));if(IsTerminalUnitState(unit->state))return foundation::Result<void>::Failure(Error("gameplay.population.invalid_migration_unit_state","terminal population unit cannot finish migration"));if(final_state==MigrationState::Completed&&unit->current_area!=it->second.from)return foundation::Result<void>::Failure(Error("gameplay.population.migration_source_stale","population unit moved since migration started"));
    const std::size_t changes=final_state==MigrationState::Completed?2:1;if(!CanAdvanceRevision()||!CanRecordChanges(changes))return foundation::Result<void>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));const Revision next{revision_.value+1};
    bool new_area_inserted=false; if(final_state==MigrationState::Completed){try{new_area_inserted=units_by_area_[it->second.to].insert(unit->id).second;}catch(...){return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","migration area index publication failed"));}}
    std::deque<PopulationChange> staged;auto seq=next_change_sequence_;PopulationChangeKind kind=final_state==MigrationState::Completed?PopulationChangeKind::MigrationCompleted:(final_state==MigrationState::Cancelled?PopulationChangeKind::MigrationCancelled:PopulationChangeKind::MigrationFailed);GameplayObjectRef area=final_state==MigrationState::Completed?it->second.to:unit->current_area;
    try{staged=changes_;AppendStagedChange(staged,seq,{0,kind,unit->group,unit->id,unit->entity.value_or(GameplayObjectRef{}),area,context,next},change_journal_capacity_);if(final_state==MigrationState::Completed)AppendStagedChange(staged,seq,{0,PopulationChangeKind::UnitMigrated,unit->group,unit->id,unit->entity.value_or(GameplayObjectRef{}),area,context,next},change_journal_capacity_);}catch(...){if(new_area_inserted){auto x=units_by_area_.find(it->second.to);if(x!=units_by_area_.end()){x->second.erase(unit->id);if(x->second.empty())units_by_area_.erase(x);}}return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","migration publication failed"));}
    it->second.state=final_state;it->second.revision=next;active_migration_by_unit_.erase(unit->id);if(final_state==MigrationState::Completed){auto old=units_by_area_.find(unit->current_area);if(old!=units_by_area_.end()){old->second.erase(unit->id);if(old->second.empty())units_by_area_.erase(old);}unit->current_area=it->second.to;unit->revision=next;}changes_.swap(staged);next_change_sequence_=seq;revision_=next;return foundation::Result<void>::Success();
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
    if(request.units.empty()||!request.purpose.IsValid())return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.invalid_allocation_request","population allocation request is invalid"));
    auto canonical=request.units;std::sort(canonical.begin(),canonical.end());if(std::adjacent_find(canonical.begin(),canonical.end())!=canonical.end())return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.duplicate_allocation_unit","population allocation request contains duplicate units"));
    if(request.correlation.IsValid()){
        auto existing=allocations_by_correlation_.find(request.correlation);if(existing!=allocations_by_correlation_.end()){PopulationAllocationBatch batch;batch.correlation=request.correlation;std::vector<PopulationUnitId> eu;for(auto aid:existing->second){auto a=allocations_.find(aid);if(a==allocations_.end()||a->second.purpose!=request.purpose||a->second.state==PopulationAllocationState::Released)return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.allocation_correlation_conflict","population allocation correlation conflicts with existing allocation"));eu.push_back(a->second.unit);batch.tokens.push_back({a->second.id,a->second.unit});}std::sort(eu.begin(),eu.end());if(eu!=canonical)return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.allocation_correlation_conflict","population allocation correlation payload does not match"));std::sort(batch.tokens.begin(),batch.tokens.end(),[](auto&a,auto&b){return a.allocation<b.allocation;});return foundation::Result<PopulationAllocationBatch>::Success(std::move(batch));}
    }
    for(auto uid:canonical){auto *u=GetUnit(uid);if(!u||!CanMaterialize(u->state)||u->entity)return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.unit_not_allocatable","population unit cannot be allocated"));if(active_allocation_by_unit_.contains(uid))return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.unit_already_allocated","population unit already has an active allocation"));}
    if(!CanAdvanceRevision()||!CanRecordChanges(canonical.size()))return foundation::Result<PopulationAllocationBatch>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));
    auto staged_alloc_ids=allocation_ids_;auto staged_corr_ids=allocation_correlation_ids_;if(!request.correlation.IsValid())request.correlation=staged_corr_ids.Next();else AdvanceGeneratorPast(staged_corr_ids,PopulationAllocationId{request.correlation});if(!request.correlation.IsValid())return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.id_exhausted","population allocation correlation id generator exhausted"));
    std::vector<PopulationAllocationId> ids;try{ids.reserve(canonical.size());for(size_t i=0;i<canonical.size();++i){PopulationAllocationId aid{staged_alloc_ids.Next()};if(!aid.IsValid())return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.id_exhausted","population allocation id generator exhausted"));ids.push_back(aid);}}catch(...){return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.publication_failed","population allocation staging failed"));}
    const Revision next{revision_.value+1};PopulationAllocationBatch batch;batch.correlation=request.correlation;
    try{
        auto new_allocations=allocations_;auto new_active=active_allocation_by_unit_;auto new_corr=allocations_by_correlation_;auto new_changes=changes_;auto seq=next_change_sequence_;auto &corr=new_corr[request.correlation];corr.reserve(corr.size()+canonical.size());batch.tokens.reserve(canonical.size());
        for(size_t i=0;i<canonical.size();++i){PopulationAllocation a;a.id=ids[i];a.unit=canonical[i];a.purpose=request.purpose;a.correlation=request.correlation;a.expires_at=request.expires_at;a.state=PopulationAllocationState::Active;a.revision=next;new_allocations.emplace(a.id,a);new_active.emplace(a.unit,a.id);corr.push_back(a.id);batch.tokens.push_back({a.id,a.unit});auto *u=GetUnit(a.unit);AppendStagedChange(new_changes,seq,{0,PopulationChangeKind::AllocationReserved,u?u->group:PopulationGroupId{},a.unit,{},u?u->current_area:GameplayObjectRef{},context,next},change_journal_capacity_);}
        allocations_.swap(new_allocations);active_allocation_by_unit_.swap(new_active);allocations_by_correlation_.swap(new_corr);changes_.swap(new_changes);next_change_sequence_=seq;
    }catch(...){return foundation::Result<PopulationAllocationBatch>::Failure(Error("gameplay.population.publication_failed","population allocation publication failed"));}
    allocation_ids_.Restore(staged_alloc_ids.GetSnapshot());allocation_correlation_ids_.Restore(staged_corr_ids.GetSnapshot());revision_=next;std::sort(batch.tokens.begin(),batch.tokens.end(),[](auto&a,auto&b){return a.allocation<b.allocation;});return foundation::Result<PopulationAllocationBatch>::Success(std::move(batch));
}

foundation::Result<void> PopulationService::CommitAllocation(PopulationAllocationId id, GameplayObjectRef entity,
                                                              GameplayContext context)
{
    auto a=allocations_.find(id);if(a==allocations_.end()||!entity.IsValid())return foundation::Result<void>::Failure(Error("gameplay.population.allocation_missing","population allocation missing or entity invalid"));if(a->second.state==PopulationAllocationState::Committed)return a->second.bound_entity==entity?foundation::Result<void>::Success():foundation::Result<void>::Failure(Error("gameplay.population.allocation_commit_conflict","population allocation is already committed to another entity"));if(a->second.state!=PopulationAllocationState::Active)return foundation::Result<void>::Failure(Error("gameplay.population.allocation_not_active","population allocation is not active"));auto active=active_allocation_by_unit_.find(a->second.unit);if(active==active_allocation_by_unit_.end()||active->second!=id)return foundation::Result<void>::Failure(Error("gameplay.population.allocation_index_corrupt","population allocation index is inconsistent"));auto *unit=FindMutableUnit(a->second.unit);if(!unit||!CanMaterialize(unit->state)||unit->entity)return foundation::Result<void>::Failure(Error("gameplay.population.unit_not_allocatable","population unit cannot commit the allocation"));if(auto ex=unit_by_entity_.find(entity);ex!=unit_by_entity_.end()&&ex->second!=unit->id)return foundation::Result<void>::Failure(Error("gameplay.population.entity_already_bound","entity is already bound"));if(!CanAdvanceRevision()||!CanRecordChanges(2))return foundation::Result<void>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));const Revision next{revision_.value+1};
    bool entity_insert=false,state_insert=false;std::deque<PopulationChange> staged;auto seq=next_change_sequence_;
    try{entity_insert=unit_by_entity_.emplace(entity,unit->id).second;state_insert=units_by_state_[StateIndex(PopulationUnitState::Materialized)].insert(unit->id).second;staged=changes_;AppendStagedChange(staged,seq,{0,PopulationChangeKind::UnitMaterialized,unit->group,unit->id,entity,unit->current_area,context,next},change_journal_capacity_);AppendStagedChange(staged,seq,{0,PopulationChangeKind::AllocationCommitted,unit->group,unit->id,entity,unit->current_area,context,next},change_journal_capacity_);}catch(...){if(entity_insert)unit_by_entity_.erase(entity);if(state_insert)units_by_state_[StateIndex(PopulationUnitState::Materialized)].erase(unit->id);return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","population allocation commit publication failed"));}
    units_by_state_[StateIndex(unit->state)].erase(unit->id);unit->entity=entity;unit->state=PopulationUnitState::Materialized;unit->revision=next;a->second.state=PopulationAllocationState::Committed;a->second.bound_entity=entity;a->second.revision=next;active_allocation_by_unit_.erase(active);changes_.swap(staged);next_change_sequence_=seq;revision_=next;++diagnostics_.materialization_requests;RecountGroup(unit->group);return foundation::Result<void>::Success();
}

foundation::Result<void> PopulationService::ReleaseAllocation(PopulationAllocationId id, GameplayContext context)
{
    auto a=allocations_.find(id);if(a==allocations_.end())return foundation::Result<void>::Failure(Error("gameplay.population.allocation_missing","population allocation missing"));if(a->second.state==PopulationAllocationState::Released)return foundation::Result<void>::Success();if(a->second.state==PopulationAllocationState::Committed)return foundation::Result<void>::Failure(Error("gameplay.population.allocation_already_committed","committed population allocation cannot be released"));if(!CanAdvanceRevision()||!CanRecordChanges())return foundation::Result<void>::Failure(Error(!CanAdvanceRevision()?"gameplay.population.revision_exhausted":"gameplay.population.change_sequence_exhausted","population mutation metadata is exhausted"));const Revision next{revision_.value+1};auto *u=FindMutableUnit(a->second.unit);std::deque<PopulationChange> staged;auto seq=next_change_sequence_;try{staged=changes_;AppendStagedChange(staged,seq,{0,PopulationChangeKind::AllocationReleased,u?u->group:PopulationGroupId{},a->second.unit,{},u?u->current_area:GameplayObjectRef{},context,next},change_journal_capacity_);}catch(...){return foundation::Result<void>::Failure(Error("gameplay.population.publication_failed","population allocation release publication failed"));}a->second.state=PopulationAllocationState::Released;a->second.revision=next;active_allocation_by_unit_.erase(a->second.unit);changes_.swap(staged);next_change_sequence_=seq;revision_=next;return foundation::Result<void>::Success();
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
    if (!CanAdvanceRevision())
        return;
    bool changed = false;
    for (const auto id : index->second)
        changed = allocations_.erase(id) != 0 || changed;
    allocations_by_correlation_.erase(index);
    if (changed)
        revision_.value += 1;
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
        if (!group.id.IsValid() || !group.area.IsValid() || !IsValidPopulationGroupState(group.state) ||
            group.revision > snapshot.revision || !new_groups.emplace(group.id, group).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.population.restore_invalid", "invalid or duplicate population group snapshot"));
        group_ids.push_back(group.id);
    }

    for (auto unit : snapshot.units)
    {
        if (!unit.id.IsValid() || !unit.group.IsValid() || !new_groups.contains(unit.group) ||
            !unit.current_area.IsValid() || !unit.home_area.IsValid() || !IsValidPopulationUnitState(unit.state) ||
            unit.revision > snapshot.revision)
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
            !residence.home_area.IsValid() || !IsValidResidenceState(residence.state) ||
            residence.revision > snapshot.revision ||
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
            !IsValidMigrationState(migration.state) || migration.revision > snapshot.revision || !new_migrations.emplace(migration.id, migration).second)
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
            !IsValidPopulationAllocationState(allocation.state) || allocation.revision > snapshot.revision || !new_allocations.emplace(allocation.id, allocation).second)
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

    std::unordered_map<PopulationGroupId, std::unordered_set<PopulationUnitId, IdHash>, IdHash> new_units_by_group;
    std::unordered_map<GameplayObjectRef, std::unordered_set<PopulationUnitId, IdHash>> new_units_by_area;
    std::unordered_map<PopulationTemplateId, std::unordered_set<PopulationUnitId, IdHash>, IdHash> new_units_by_template;
    std::array<std::unordered_set<PopulationUnitId, IdHash>, 5> new_units_by_state;
    try
    {
        for (const auto &[id, unit] : new_units)
        {
            new_units_by_group[unit.group].insert(id);
            new_units_by_area[unit.current_area].insert(id);
            if (unit.template_id.IsValid()) new_units_by_template[unit.template_id].insert(id);
            new_units_by_state[StateIndex(unit.state)].insert(id);
        }
        for (auto &[gid, group] : new_groups)
        {
            std::uint32_t known = 0, materialized = 0;
            if (auto it = new_units_by_group.find(gid); it != new_units_by_group.end())
                for (auto uid : it->second)
                {
                    const auto &unit = new_units.at(uid);
                    if (unit.state != PopulationUnitState::Removed) ++known;
                    if (unit.state == PopulationUnitState::Materialized) ++materialized;
                }
            group.current_known_count = known;
            group.materialized_count = materialized;
        }
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(Error("gameplay.population.restore_allocation_failed",
                                                        "population restore index staging failed"));
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

    groups_.swap(new_groups);
    units_.swap(new_units);
    residences_.swap(new_residences);
    migrations_.swap(new_migrations);
    allocations_.swap(new_allocations);
    unit_by_entity_.swap(new_unit_by_entity);
    active_migration_by_unit_.swap(new_active_migrations);
    active_residence_by_unit_.swap(new_active_residences);
    active_allocation_by_unit_.swap(new_active_allocations);
    allocations_by_correlation_.swap(new_allocations_by_correlation);
    units_by_group_.swap(new_units_by_group);
    units_by_area_.swap(new_units_by_area);
    units_by_template_.swap(new_units_by_template);
    units_by_state_.swap(new_units_by_state);
    group_ids_.Restore(snapshot.group_ids);
    unit_ids_.Restore(snapshot.unit_ids);
    residence_ids_.Restore(snapshot.residence_ids);
    migration_ids_.Restore(snapshot.migration_ids);
    allocation_ids_.Restore(allocation_id_snapshot);
    allocation_correlation_ids_.Restore(allocation_correlation_snapshot);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
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
    if (next_change_sequence_ == 0) return;
    const auto sequence = next_change_sequence_;
    change.sequence = sequence;
    changes_.push_back(std::move(change));
    if (sequence == std::numeric_limits<std::uint64_t>::max()) next_change_sequence_ = 0;
    else next_change_sequence_ = sequence + 1;
    while (changes_.size() > change_journal_capacity_) changes_.pop_front();
}
} // namespace epidemic::gameplay::population
