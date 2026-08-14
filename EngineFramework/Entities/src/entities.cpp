#include "Epidemic/GameFramework/Entities/entities.h"

#include "Epidemic/Foundation/error.h"

#include <limits>

namespace epidemic::gameplay::entities
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}
} // namespace

EntityService::EntityService()
    : ids_(GameplayObjectId::FromString("framework.entities.instances").High())
{
}

foundation::Result<EntityArchetypeId> EntityService::RegisterArchetype(EntityArchetypeDefinition definition)
{
    if (frozen_)
    {
        return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.registry_frozen", "entity archetype registry is frozen"));
    }
    if (definition.canonical_name.empty())
    {
        return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.entity_archetype_invalid", "entity archetype name must not be empty"));
    }

    const auto expected = EntityArchetypeId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
    {
        definition.id = expected;
    }
    if (definition.id != expected)
    {
        return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.entity_archetype_id_mismatch", "entity archetype id does not match canonical name"));
    }
    if (archetypes_.contains(definition.id))
    {
        return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.already_registered", "entity archetype is already registered"));
    }

    const auto id = definition.id;
    archetypes_.emplace(id, std::move(definition));
    return foundation::Result<EntityArchetypeId>::Success(id);
}

const EntityArchetypeDefinition* EntityService::FindArchetype(EntityArchetypeId id) const noexcept
{
    const auto found = archetypes_.find(id);
    return found == archetypes_.end() ? nullptr : &found->second;
}

foundation::Result<std::uint32_t> EntityService::AllocateSlot()
{
    if (!free_slots_.empty())
    {
        const auto slot = free_slots_.back();
        free_slots_.pop_back();
        return foundation::Result<std::uint32_t>::Success(slot);
    }
    if (slots_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return foundation::Result<std::uint32_t>::Failure(Error("gameplay.entity_capacity_exhausted", "entity slot capacity exhausted"));
    }
    slots_.push_back(Slot{});
    return foundation::Result<std::uint32_t>::Success(static_cast<std::uint32_t>(slots_.size() - 1));
}

foundation::Result<CreateEntityResult> EntityService::Create(CreateEntityRequest request)
{
    const auto* archetype = FindArchetype(request.archetype);
    if (archetype == nullptr)
    {
        return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_archetype_unknown", "entity archetype is not registered"));
    }

    EntityId id{};
    if (request.requested_id.has_value())
    {
        id = *request.requested_id;
    }
    else
    {
        id = EntityId{ids_.Next()};
    }
    if (!id.IsValid())
    {
        return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_id_exhausted", "entity id is invalid or exhausted"));
    }
    if (id_to_slot_.contains(id))
    {
        return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_duplicate", "entity id already exists"));
    }

    auto slot_result = AllocateSlot();
    if (!slot_result)
    {
        return foundation::Result<CreateEntityResult>::Failure(slot_result.GetError());
    }
    const auto slot_index = slot_result.Value();
    auto& slot = slots_[slot_index];
    if (slot.generation == 0)
    {
        slot.generation = 1;
    }

    EntityRecord record;
    record.id = id;
    record.archetype = request.archetype;
    record.lifecycle = EntityLifecycleState::Alive;
    record.materialization = EntityMaterializationState::Abstract;
    record.instance_tags = std::move(request.additional_tags);
    record.persistence = request.persistence.value_or(archetype->persistence);
    record.revision = Revision{++revision_.value};

    slot.record = std::move(record);
    slot.occupied = true;
    id_to_slot_.emplace(id, slot_index);
    ++creates_;

    RecordChange(EntityChange{0,
                              EntityChangeKind::Created,
                              id,
                              {},
                              request.archetype,
                              EntityLifecycleState::Alive,
                              EntityMaterializationState::Abstract,
                              EntityMaterializationState::Abstract,
                              {},
                              EntityDestroyReason::Destroyed,
                              request.context});

    return foundation::Result<CreateEntityResult>::Success(CreateEntityResult{id, EntityHandle{slot_index, slot.generation}});
}

EntityRecord* EntityService::FindMutable(EntityId id) noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end())
    {
        return nullptr;
    }
    auto& slot = slots_[found->second];
    return slot.occupied ? &slot.record : nullptr;
}

const EntityRecord* EntityService::Find(EntityId id) const noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end())
    {
        return nullptr;
    }
    const auto& slot = slots_[found->second];
    return slot.occupied ? &slot.record : nullptr;
}

EntityService::Slot* EntityService::ResolveSlot(EntityHandle handle) noexcept
{
    if (!handle.IsValid() || handle.slot >= slots_.size())
    {
        return nullptr;
    }
    auto& slot = slots_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const EntityService::Slot* EntityService::ResolveSlot(EntityHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.slot >= slots_.size())
    {
        return nullptr;
    }
    const auto& slot = slots_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const EntityRecord* EntityService::Resolve(EntityHandle handle) const noexcept
{
    const auto* slot = ResolveSlot(handle);
    if (slot == nullptr)
    {
        ++invalid_handle_lookups_;
        return nullptr;
    }
    return &slot->record;
}

EntityHandle EntityService::HandleOf(EntityId id) const noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end())
    {
        return {};
    }
    const auto& slot = slots_[found->second];
    return slot.occupied ? EntityHandle{found->second, slot.generation} : EntityHandle{};
}

void EntityService::BumpRevision(EntityRecord& record) noexcept
{
    ++revision_.value;
    record.revision = revision_;
}

foundation::Result<void> EntityService::RequestDestroy(EntityId id, EntityDestroyReason reason, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    }
    if (record->lifecycle == EntityLifecycleState::PendingDestroy)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_destroy_pending", "entity is already pending destruction"));
    }

    record->lifecycle = EntityLifecycleState::PendingDestroy;
    BumpRevision(*record);
    pending_destroy_.push_back(id);
    pending_destroy_reasons_[id] = reason;
    pending_destroy_contexts_[id] = context;
    RecordChange(EntityChange{0,
                              EntityChangeKind::DestroyRequested,
                              id,
                              {},
                              record->archetype,
                              record->lifecycle,
                              record->materialization,
                              record->materialization,
                              {},
                              reason,
                              context});
    return foundation::Result<void>::Success();
}

std::vector<EntityRecord> EntityService::CommitPendingDestruction()
{
    std::sort(pending_destroy_.begin(), pending_destroy_.end());
    pending_destroy_.erase(std::unique(pending_destroy_.begin(), pending_destroy_.end()), pending_destroy_.end());

    std::vector<EntityRecord> destroyed;
    destroyed.reserve(pending_destroy_.size());
    for (const auto id : pending_destroy_)
    {
        const auto found = id_to_slot_.find(id);
        if (found == id_to_slot_.end())
        {
            continue;
        }
        const auto slot_index = found->second;
        auto& slot = slots_[slot_index];
        if (!slot.occupied || slot.record.lifecycle != EntityLifecycleState::PendingDestroy)
        {
            continue;
        }

        auto final_record = slot.record;
        final_record.lifecycle = EntityLifecycleState::Destroyed;
        ++revision_.value;
        final_record.revision = revision_;
        destroyed.push_back(final_record);

        const auto reason_found = pending_destroy_reasons_.find(id);
        const auto context_found = pending_destroy_contexts_.find(id);
        RecordChange(EntityChange{0,
                                  EntityChangeKind::Destroyed,
                                  id,
                                  {},
                                  final_record.archetype,
                                  EntityLifecycleState::Destroyed,
                                  final_record.materialization,
                                  final_record.materialization,
                                  {},
                                  reason_found == pending_destroy_reasons_.end() ? EntityDestroyReason::Destroyed : reason_found->second,
                                  context_found == pending_destroy_contexts_.end() ? GameplayContext{} : context_found->second});

        slot.occupied = false;
        slot.record = {};
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }
        id_to_slot_.erase(found);
        free_slots_.push_back(slot_index);
        pending_destroy_reasons_.erase(id);
        pending_destroy_contexts_.erase(id);
        ++destroys_;
    }
    pending_destroy_.clear();
    return destroyed;
}

foundation::Result<void> EntityService::Convert(
    EntityId id,
    EntityArchetypeId new_archetype,
    EntityConversionPolicy policy,
    GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    }
    if (record->lifecycle != EntityLifecycleState::Alive)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive entities can be converted"));
    }
    const auto* definition = FindArchetype(new_archetype);
    if (definition == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_archetype_unknown", "target archetype is not registered"));
    }

    const auto previous = record->archetype;
    record->archetype = new_archetype;
    if (!policy.preserve_instance_tags)
    {
        record->instance_tags = {};
    }
    if (!policy.preserve_persistence)
    {
        record->persistence = definition->persistence;
    }
    BumpRevision(*record);
    RecordChange(EntityChange{0,
                              EntityChangeKind::Converted,
                              id,
                              previous,
                              new_archetype,
                              record->lifecycle,
                              record->materialization,
                              record->materialization,
                              {},
                              EntityDestroyReason::Converted,
                              context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::SetMaterializationState(
    EntityId id,
    EntityMaterializationState state,
    GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    }
    if (record->lifecycle == EntityLifecycleState::Destroyed)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "destroyed entity cannot change materialization state"));
    }
    if (record->materialization == state)
    {
        return foundation::Result<void>::Success();
    }

    const auto previous = record->materialization;
    record->materialization = state;
    BumpRevision(*record);
    RecordChange(EntityChange{0,
                              EntityChangeKind::MaterializationChanged,
                              id,
                              {},
                              record->archetype,
                              record->lifecycle,
                              previous,
                              state,
                              {},
                              EntityDestroyReason::Destroyed,
                              context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::AddTag(EntityId id, TagId tag, GameplayContext context)
{
    if (!tag.IsValid())
    {
        return foundation::Result<void>::Failure(Error("gameplay.tag_invalid", "tag must be valid"));
    }
    auto* record = FindMutable(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    }
    if (record->instance_tags.HasExact(tag))
    {
        return foundation::Result<void>::Success();
    }
    record->instance_tags.Add(tag);
    BumpRevision(*record);
    RecordChange(EntityChange{0,
                              EntityChangeKind::TagAdded,
                              id,
                              {},
                              record->archetype,
                              record->lifecycle,
                              record->materialization,
                              record->materialization,
                              tag,
                              EntityDestroyReason::Destroyed,
                              context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::RemoveTag(EntityId id, TagId tag, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    }
    if (!record->instance_tags.HasExact(tag))
    {
        return foundation::Result<void>::Success();
    }
    record->instance_tags.Remove(tag);
    BumpRevision(*record);
    RecordChange(EntityChange{0,
                              EntityChangeKind::TagRemoved,
                              id,
                              {},
                              record->archetype,
                              record->lifecycle,
                              record->materialization,
                              record->materialization,
                              tag,
                              EntityDestroyReason::Destroyed,
                              context});
    return foundation::Result<void>::Success();
}

std::vector<EntityRecord> EntityService::AllEntities() const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_)
    {
        if (slot.occupied)
        {
            result.push_back(slot.record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<EntityRecord> EntityService::FindByArchetype(EntityArchetypeId archetype) const
{
    std::vector<EntityRecord> result;
    for (const auto& slot : slots_)
    {
        if (slot.occupied && slot.record.archetype == archetype)
        {
            result.push_back(slot.record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<EntityRecord> EntityService::FindByTag(TagId tag, const GameplayTagRegistry& registry) const
{
    std::vector<EntityRecord> result;
    for (const auto& slot : slots_)
    {
        if (!slot.occupied)
        {
            continue;
        }
        const auto* archetype = FindArchetype(slot.record.archetype);
        const bool archetype_has = archetype != nullptr && archetype->tags.HasMatching(tag, registry);
        if (archetype_has || slot.record.instance_tags.HasMatching(tag, registry))
        {
            result.push_back(slot.record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<EntityRecord> EntityService::FindByLifecycle(EntityLifecycleState lifecycle) const
{
    std::vector<EntityRecord> result;
    for (const auto& slot : slots_)
    {
        if (slot.occupied && slot.record.lifecycle == lifecycle)
        {
            result.push_back(slot.record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

std::vector<EntityRecord> EntityService::FindByMaterialization(EntityMaterializationState state) const
{
    std::vector<EntityRecord> result;
    for (const auto& slot : slots_)
    {
        if (slot.occupied && slot.record.materialization == state)
        {
            result.push_back(slot.record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}

void EntityService::RecordChange(EntityChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
}

std::vector<EntityChange> EntityService::ChangesSince(std::uint64_t sequence) const
{
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence, [](std::uint64_t value, const EntityChange& change) {
        return value < change.sequence;
    });
    return std::vector<EntityChange>(found, changes_.end());
}

void EntityService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence, [](const EntityChange& change, std::uint64_t value) {
        return change.sequence < value;
    });
    changes_.erase(changes_.begin(), found);
}

EntitySnapshot EntityService::CaptureSnapshot() const
{
    EntitySnapshot snapshot;
    snapshot.id_generator = ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.records.reserve(id_to_slot_.size());
    for (const auto& slot : slots_)
    {
        if (slot.occupied && slot.record.lifecycle != EntityLifecycleState::Destroyed &&
            slot.record.persistence != EntityPersistencePolicy::Transient)
        {
            snapshot.records.push_back(slot.record);
        }
    }
    std::sort(snapshot.records.begin(), snapshot.records.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
    return snapshot;
}

foundation::Result<void> EntityService::RestoreSnapshot(EntitySnapshot snapshot)
{
    std::unordered_map<EntityId, bool, EntityIdHash> seen;
    for (const auto& record : snapshot.records)
    {
        if (!record.id.IsValid() || FindArchetype(record.archetype) == nullptr || seen.contains(record.id))
        {
            return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "entity snapshot contains invalid or duplicate records"));
        }
        seen.emplace(record.id, true);
    }

    slots_.clear();
    free_slots_.clear();
    id_to_slot_.clear();
    pending_destroy_.clear();
    pending_destroy_reasons_.clear();
    pending_destroy_contexts_.clear();
    changes_.clear();
    next_change_sequence_ = 1;

    ids_.Restore(snapshot.id_generator);
    revision_ = snapshot.revision;
    creates_ = 0;
    destroys_ = 0;
    invalid_handle_lookups_ = 0;

    slots_.reserve(snapshot.records.size());
    for (auto& record : snapshot.records)
    {
        const auto slot_index = static_cast<std::uint32_t>(slots_.size());
        Slot slot;
        slot.record = std::move(record);
        if (slot.record.lifecycle == EntityLifecycleState::PendingDestroy)
        {
            pending_destroy_.push_back(slot.record.id);
        }
        slot.generation = 1;
        slot.occupied = true;
        id_to_slot_.emplace(slot.record.id, slot_index);
        slots_.push_back(std::move(slot));
    }
    return foundation::Result<void>::Success();
}

EntitiesDiagnostics EntityService::GetDiagnostics() const noexcept
{
    EntitiesDiagnostics result;
    result.entity_count = id_to_slot_.size();
    result.creates = creates_;
    result.destroys = destroys_;
    result.invalid_handle_lookups = invalid_handle_lookups_;
    for (const auto& slot : slots_)
    {
        if (!slot.occupied)
        {
            continue;
        }
        if (slot.record.persistence == EntityPersistencePolicy::Persistent)
        {
            ++result.persistent_entities;
        }
        if (slot.record.materialization == EntityMaterializationState::Abstract)
        {
            ++result.abstract_entities;
        }
        if (slot.record.materialization == EntityMaterializationState::Materialized)
        {
            ++result.materialized_entities;
        }
        if (slot.record.lifecycle == EntityLifecycleState::PendingDestroy)
        {
            ++result.pending_destruction;
        }
    }
    return result;
}
} // namespace epidemic::gameplay::entities
