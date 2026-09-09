#include "Epidemic/GameFramework/Entities/entities.h"

#include "Epidemic/Foundation/error.h"

#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::entities
{
namespace
{
[[nodiscard]] constexpr std::uint64_t EntityInstanceIdScope() noexcept
{
    return GameplayObjectId::FromString("framework.entities.instances").High();
}

void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId>& generator, EntityId id) noexcept
{
    if (!id.IsValid()) return;

    auto snapshot = generator.GetSnapshot();
    if (id.High() != snapshot.scope || snapshot.next == 0 || id.Low() < snapshot.next) return;

    snapshot.next = id.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.Low() + 1;
    generator.Restore(snapshot);
}

[[nodiscard]] foundation::Error Error(std::string code, std::string message)
{
    return foundation::Error::Create(std::move(code), std::move(message));
}

[[nodiscard]] bool IsMutableLifecycle(EntityLifecycleState state) noexcept
{
    return state == EntityLifecycleState::Alive || state == EntityLifecycleState::Dormant;
}
} // namespace

EntityService::EntityService()
    : ids_(EntityInstanceIdScope())
{
}

foundation::Result<void> EntityService::ValidateArchetypeParts(EntityArchetypeDefinition& definition) const
{
    std::unordered_map<EntityPartId, std::size_t, EntityPartIdHash> by_id;
    by_id.reserve(definition.parts.size());
    for (std::size_t index = 0; index < definition.parts.size(); ++index)
    {
        auto& part = definition.parts[index];
        if (part.canonical_name.empty())
        {
            return foundation::Result<void>::Failure(Error("gameplay.entity_part_invalid", "entity part canonical name must not be empty"));
        }
        const auto expected = EntityPartId::FromString(part.canonical_name);
        if (!part.id.IsValid()) part.id = expected;
        if (part.id != expected)
        {
            return foundation::Result<void>::Failure(Error("gameplay.entity_part_id_mismatch", "entity part id does not match canonical name"));
        }
        if (!by_id.emplace(part.id, index).second)
        {
            return foundation::Result<void>::Failure(Error("gameplay.entity_part_duplicate", "entity archetype contains duplicate part ids"));
        }
    }
    for (const auto& part : definition.parts)
    {
        if (part.parent.IsValid() && (part.parent == part.id || !by_id.contains(part.parent)))
        {
            return foundation::Result<void>::Failure(Error("gameplay.entity_part_parent_invalid", "entity part parent must reference another part in the same archetype"));
        }
        std::unordered_set<EntityPartId, EntityPartIdHash> seen;
        auto current = part.parent;
        while (current.IsValid())
        {
            if (!seen.insert(current).second)
            {
                return foundation::Result<void>::Failure(Error("gameplay.entity_part_cycle", "entity part hierarchy contains a cycle"));
            }
            current = definition.parts[by_id.at(current)].parent;
        }
    }
    std::sort(definition.parts.begin(), definition.parts.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return foundation::Result<void>::Success();
}

foundation::Result<EntityArchetypeId> EntityService::RegisterArchetype(EntityArchetypeDefinition definition)
{
    if (frozen_) return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.registry_frozen", "entity archetype registry is frozen"));
    if (definition.canonical_name.empty()) return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.entity_archetype_invalid", "entity archetype name must not be empty"));
    const auto expected = EntityArchetypeId::FromString(definition.canonical_name);
    if (!definition.id.IsValid()) definition.id = expected;
    if (definition.id != expected) return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.entity_archetype_id_mismatch", "entity archetype id does not match canonical name"));
    if (archetypes_.contains(definition.id)) return foundation::Result<EntityArchetypeId>::Failure(Error("gameplay.already_registered", "entity archetype is already registered"));
    if (auto parts = ValidateArchetypeParts(definition); !parts) return foundation::Result<EntityArchetypeId>::Failure(parts.GetError());
    const auto id = definition.id;
    archetypes_.emplace(id, std::move(definition));
    return foundation::Result<EntityArchetypeId>::Success(id);
}

const EntityArchetypeDefinition* EntityService::FindArchetype(EntityArchetypeId id) const noexcept
{
    const auto found = archetypes_.find(id);
    return found == archetypes_.end() ? nullptr : &found->second;
}

const EntityPartDefinition* EntityService::FindPartDefinition(EntityArchetypeId archetype, EntityPartId part) const noexcept
{
    const auto* definition = FindArchetype(archetype);
    if (definition == nullptr) return nullptr;
    const auto found = std::lower_bound(definition->parts.begin(), definition->parts.end(), part, [](const auto& value, EntityPartId id) { return value.id < id; });
    return found != definition->parts.end() && found->id == part ? &*found : nullptr;
}

std::optional<EntityMaterializationPolicy> EntityService::MaterializationPolicyOf(EntityId id) const noexcept
{
    const auto* record = FindInternal(id);
    if (record == nullptr) return std::nullopt;
    const auto* archetype = FindArchetype(record->archetype);
    return archetype == nullptr ? std::nullopt : std::optional<EntityMaterializationPolicy>{archetype->materialization};
}

bool EntityService::RequiresMaterialization(EntityId id) const noexcept
{
    const auto policy = MaterializationPolicyOf(id);
    return policy.has_value() && *policy == EntityMaterializationPolicy::RequireMaterialized;
}

std::optional<EntityPartRef> EntityService::GetPart(EntityId entity, EntityPartId part) const
{
    const auto* record = FindInternal(entity);
    if (record == nullptr || FindPartDefinition(record->archetype, part) == nullptr) return std::nullopt;
    return EntityPartRef{entity, part};
}

std::vector<EntityPartRef> EntityService::GetParts(EntityId entity) const
{
    std::vector<EntityPartRef> result;
    const auto* record = FindInternal(entity);
    if (record == nullptr) return result;
    const auto* definition = FindArchetype(record->archetype);
    if (definition == nullptr) return result;
    result.reserve(definition->parts.size());
    for (const auto& part : definition->parts) result.push_back(EntityPartRef{entity, part.id});
    return result;
}

std::vector<EntityPartRef> EntityService::GetPartAncestors(EntityPartRef ref) const
{
    std::vector<EntityPartRef> result;
    const auto* record = FindInternal(ref.entity);
    if (record == nullptr) return result;
    const auto* part = FindPartDefinition(record->archetype, ref.part);
    while (part != nullptr && part->parent.IsValid())
    {
        result.push_back(EntityPartRef{ref.entity, part->parent});
        part = FindPartDefinition(record->archetype, part->parent);
    }
    return result;
}

foundation::Result<Revision> EntityService::PrepareRevision() const
{
    const auto next = CheckedNext(revision_);
    if (!next) return foundation::Result<Revision>::Failure(Error("gameplay.revision_exhausted", "entity revision counter is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}

bool EntityService::CanRecordChanges(std::size_t count) const noexcept
{
    if (count == 0) return true;
    if (next_change_sequence_ == 0) return false;
    return count - 1 <= std::numeric_limits<std::uint64_t>::max() - next_change_sequence_;
}

void EntityService::RecordChange(EntityChange change) noexcept
{
    change.sequence = next_change_sequence_;
    last_change_sequence_ = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max()) next_change_sequence_ = 0;
    else ++next_change_sequence_;
    changes_.push_back(std::move(change));
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
        return foundation::Result<std::uint32_t>::Failure(Error("gameplay.entity_capacity_exhausted", "entity slot capacity exhausted"));
    slots_.push_back(Slot{});
    return foundation::Result<std::uint32_t>::Success(static_cast<std::uint32_t>(slots_.size() - 1));
}

foundation::Result<CreateEntityResult> EntityService::Create(CreateEntityRequest request)
{
    if (!frozen_) return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.registry_not_frozen", "entity service must be frozen before runtime operations"));
    const auto* archetype = FindArchetype(request.archetype);
    if (archetype == nullptr) return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_archetype_unknown", "entity archetype is not registered"));
    if (!CanRecordChanges(1)) return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto next_revision = PrepareRevision();
    if (!next_revision) return foundation::Result<CreateEntityResult>::Failure(next_revision.GetError());

    auto staged_ids = ids_;
    EntityId id = request.requested_id.value_or(EntityId{});
    if (!id.IsValid())
    {
        id = EntityId{staged_ids.Next()};
    }
    else
    {
        AdvanceGeneratorPastAcceptedId(staged_ids, id);
    }
    if (!id.IsValid()) return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_id_exhausted", "entity id is invalid or exhausted"));
    if (id_to_slot_.contains(id)) return foundation::Result<CreateEntityResult>::Failure(Error("gameplay.entity_duplicate", "entity id already exists"));

    auto slot_result = AllocateSlot();
    if (!slot_result) return foundation::Result<CreateEntityResult>::Failure(slot_result.GetError());
    const auto slot_index = slot_result.Value();
    auto& slot = slots_[slot_index];
    if (slot.generation == 0) slot.generation = 1;

    EntityRecord record;
    record.id = id;
    record.archetype = request.archetype;
    record.lifecycle = EntityLifecycleState::Alive;
    record.materialization = EntityMaterializationState::Abstract;
    record.instance_tags = std::move(request.additional_tags);
    record.persistence = request.persistence.value_or(archetype->persistence);
    record.revision = next_revision.Value();
    revision_ = next_revision.Value();
    slot.record = record;
    slot.occupied = true;
    id_to_slot_.emplace(id, slot_index);
    ids_ = staged_ids;
    if (creates_ != std::numeric_limits<std::uint64_t>::max()) ++creates_;

    RecordChange(EntityChange{0, EntityChangeKind::Created, id, {}, request.archetype, EntityLifecycleState::Creating,
                              EntityLifecycleState::Alive, EntityMaterializationState::Abstract, EntityMaterializationState::Abstract,
                              {}, EntityDestroyReason::Destroyed, request.context, revision_});
    return foundation::Result<CreateEntityResult>::Success(CreateEntityResult{id, EntityHandle{slot_index, slot.generation}});
}

EntityRecord* EntityService::FindMutable(EntityId id) noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end()) return nullptr;
    auto& slot = slots_[found->second];
    return slot.occupied ? &slot.record : nullptr;
}

const EntityRecord* EntityService::FindInternal(EntityId id) const noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end()) return nullptr;
    const auto& slot = slots_[found->second];
    return slot.occupied ? &slot.record : nullptr;
}

std::optional<EntityRecord> EntityService::Find(EntityId id) const
{
    const auto* value = FindInternal(id);
    return value == nullptr ? std::nullopt : std::optional<EntityRecord>{*value};
}

EntityService::Slot* EntityService::ResolveSlot(EntityHandle handle) noexcept
{
    if (!handle.IsValid() || handle.slot >= slots_.size()) return nullptr;
    auto& slot = slots_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

const EntityService::Slot* EntityService::ResolveSlot(EntityHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.slot >= slots_.size()) return nullptr;
    const auto& slot = slots_[handle.slot];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

std::optional<EntityRecord> EntityService::Resolve(EntityHandle handle) const
{
    const auto* slot = ResolveSlot(handle);
    if (slot == nullptr)
    {
        if (invalid_handle_lookups_ != std::numeric_limits<std::uint64_t>::max()) ++invalid_handle_lookups_;
        return std::nullopt;
    }
    return slot->record;
}

EntityHandle EntityService::HandleOf(EntityId id) const noexcept
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end()) return {};
    const auto& slot = slots_[found->second];
    return slot.occupied ? EntityHandle{found->second, slot.generation} : EntityHandle{};
}

foundation::Result<void> EntityService::Activate(EntityId id, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (record->lifecycle == EntityLifecycleState::Alive) return foundation::Result<void>::Success();
    if (record->lifecycle != EntityLifecycleState::Dormant) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only dormant entities can be activated"));
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    const auto previous = record->lifecycle;
    record->lifecycle = EntityLifecycleState::Alive;
    record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::Activated, id, {}, record->archetype, previous, record->lifecycle,
                              record->materialization, record->materialization, {}, EntityDestroyReason::Destroyed, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::Deactivate(EntityId id, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (record->lifecycle == EntityLifecycleState::Dormant) return foundation::Result<void>::Success();
    if (record->lifecycle != EntityLifecycleState::Alive) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive entities can be deactivated"));
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    const auto previous_lifecycle = record->lifecycle;
    const auto previous_materialization = record->materialization;
    record->lifecycle = EntityLifecycleState::Dormant;
    record->materialization = EntityMaterializationState::Abstract;
    record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::Deactivated, id, {}, record->archetype, previous_lifecycle, record->lifecycle,
                              previous_materialization, record->materialization, {}, EntityDestroyReason::Destroyed, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::RequestDestroy(EntityId id, EntityDestroyReason reason, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (record->lifecycle == EntityLifecycleState::PendingDestroy) return foundation::Result<void>::Failure(Error("gameplay.entity_destroy_pending", "entity is already pending destruction"));
    if (!IsMutableLifecycle(record->lifecycle)) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive or dormant entities can be requested for destruction"));
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    const auto previous = record->lifecycle;
    record->lifecycle = EntityLifecycleState::PendingDestroy;
    record->revision = rev.Value(); revision_ = rev.Value();
    pending_destroy_.push_back(id);
    pending_destroy_reasons_[id] = reason;
    pending_destroy_contexts_[id] = context;
    RecordChange(EntityChange{0, EntityChangeKind::DestroyRequested, id, {}, record->archetype, previous, record->lifecycle,
                              record->materialization, record->materialization, {}, reason, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<EntityRecord>> EntityService::CommitPendingDestruction()
{
    std::sort(pending_destroy_.begin(), pending_destroy_.end());
    pending_destroy_.erase(std::unique(pending_destroy_.begin(), pending_destroy_.end()), pending_destroy_.end());
    std::vector<EntityId> valid;
    for (const auto id : pending_destroy_)
    {
        const auto* record = FindInternal(id);
        if (record != nullptr && record->lifecycle == EntityLifecycleState::PendingDestroy) valid.push_back(id);
    }
    if (valid.empty())
    {
        pending_destroy_.clear();
        return foundation::Result<std::vector<EntityRecord>>::Success({});
    }
    if (!CanRecordChanges(valid.size())) return foundation::Result<std::vector<EntityRecord>>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<std::vector<EntityRecord>>::Failure(rev.GetError());
    revision_ = rev.Value();
    std::vector<EntityRecord> destroyed;
    destroyed.reserve(valid.size());
    for (const auto id : valid)
    {
        auto* record = FindMutable(id);
        const auto previous_lifecycle = record->lifecycle;
        const auto previous_materialization = record->materialization;
        record->lifecycle = EntityLifecycleState::Destroyed;
        record->materialization = EntityMaterializationState::Abstract;
        record->revision = revision_;
        destroyed.push_back(*record);
        const auto reason_it = pending_destroy_reasons_.find(id);
        const auto context_it = pending_destroy_contexts_.find(id);
        const auto reason = reason_it == pending_destroy_reasons_.end() ? EntityDestroyReason::Destroyed : reason_it->second;
        const auto context = context_it == pending_destroy_contexts_.end() ? GameplayContext{} : context_it->second;
        RecordChange(EntityChange{0, EntityChangeKind::Destroyed, id, {}, record->archetype, previous_lifecycle, record->lifecycle,
                                  previous_materialization, record->materialization, {}, reason, context, revision_});
        pending_destroy_reasons_.erase(id);
        pending_destroy_contexts_.erase(id);
        if (destroys_ != std::numeric_limits<std::uint64_t>::max()) ++destroys_;
    }
    pending_destroy_.clear();
    return foundation::Result<std::vector<EntityRecord>>::Success(std::move(destroyed));
}

foundation::Result<void> EntityService::Remove(EntityId id, GameplayContext context)
{
    const auto found = id_to_slot_.find(id);
    if (found == id_to_slot_.end()) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    const auto slot_index = found->second;
    auto& slot = slots_[slot_index];
    if (!slot.occupied || slot.record.lifecycle != EntityLifecycleState::Destroyed)
        return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only destroyed entities can be permanently removed"));
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    auto final = slot.record;
    final.lifecycle = EntityLifecycleState::Removed;
    final.revision = rev.Value();
    revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::Removed, id, {}, final.archetype, EntityLifecycleState::Destroyed, EntityLifecycleState::Removed,
                              slot.record.materialization, EntityMaterializationState::Abstract, {}, EntityDestroyReason::Removed, context, revision_});
    id_to_slot_.erase(found);
    pending_destroy_reasons_.erase(id);
    pending_destroy_contexts_.erase(id);
    slot.record = {};
    slot.occupied = false;
    if (slot.generation != std::numeric_limits<std::uint32_t>::max())
    {
        ++slot.generation;
        if (slot.generation == 0) ++slot.generation;
        free_slots_.push_back(slot_index);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::Convert(EntityId id, EntityArchetypeId new_archetype, EntityConversionPolicy policy, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (record->lifecycle != EntityLifecycleState::Alive) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive entities can be converted"));
    const auto* definition = FindArchetype(new_archetype);
    if (definition == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_archetype_unknown", "target archetype is not registered"));
    if (record->archetype == new_archetype && policy.preserve_instance_tags && policy.preserve_persistence) return foundation::Result<void>::Success();
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    const auto previous = record->archetype;
    record->archetype = new_archetype;
    if (!policy.preserve_instance_tags) record->instance_tags = {};
    if (!policy.preserve_persistence) record->persistence = definition->persistence;
    record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::Converted, id, previous, new_archetype, record->lifecycle, record->lifecycle,
                              record->materialization, record->materialization, {}, EntityDestroyReason::Converted, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::SetMaterializationState(EntityId id, EntityMaterializationState state, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (record->lifecycle != EntityLifecycleState::Alive) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive entities can change materialization state"));
    if (record->materialization == state) return foundation::Result<void>::Success();
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    const auto previous = record->materialization;
    record->materialization = state; record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::MaterializationChanged, id, {}, record->archetype, record->lifecycle, record->lifecycle,
                              previous, state, {}, EntityDestroyReason::Destroyed, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::AddTag(EntityId id, TagId tag, GameplayContext context)
{
    if (!tag.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.tag_invalid", "tag must be valid"));
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (!IsMutableLifecycle(record->lifecycle)) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive or dormant entities may change instance tags"));
    if (record->instance_tags.HasExact(tag)) return foundation::Result<void>::Success();
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    record->instance_tags.Add(tag); record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::TagAdded, id, {}, record->archetype, record->lifecycle, record->lifecycle,
                              record->materialization, record->materialization, tag, EntityDestroyReason::Destroyed, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EntityService::RemoveTag(EntityId id, TagId tag, GameplayContext context)
{
    auto* record = FindMutable(id);
    if (record == nullptr) return foundation::Result<void>::Failure(Error("gameplay.entity_unknown", "entity does not exist"));
    if (!IsMutableLifecycle(record->lifecycle)) return foundation::Result<void>::Failure(Error("gameplay.entity_invalid_state", "only alive or dormant entities may change instance tags"));
    if (!record->instance_tags.HasExact(tag)) return foundation::Result<void>::Success();
    if (!CanRecordChanges(1)) return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted", "entity change sequence is exhausted"));
    auto rev = PrepareRevision(); if (!rev) return foundation::Result<void>::Failure(rev.GetError());
    record->instance_tags.Remove(tag); record->revision = rev.Value(); revision_ = rev.Value();
    RecordChange(EntityChange{0, EntityChangeKind::TagRemoved, id, {}, record->archetype, record->lifecycle, record->lifecycle,
                              record->materialization, record->materialization, tag, EntityDestroyReason::Destroyed, context, revision_});
    return foundation::Result<void>::Success();
}

std::vector<EntityRecord> EntityService::AllEntities() const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_) if (slot.occupied) result.push_back(slot.record);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); return result;
}
std::vector<EntityRecord> EntityService::FindByArchetype(EntityArchetypeId archetype) const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_) if (slot.occupied && slot.record.archetype == archetype) result.push_back(slot.record);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); return result;
}
std::vector<EntityRecord> EntityService::FindByTag(TagId tag, const GameplayTagRegistry& registry) const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_)
    {
        if (!slot.occupied) continue;
        const auto* archetype = FindArchetype(slot.record.archetype);
        if ((archetype != nullptr && archetype->tags.HasMatching(tag, registry)) || slot.record.instance_tags.HasMatching(tag, registry)) result.push_back(slot.record);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); return result;
}
std::vector<EntityRecord> EntityService::FindByLifecycle(EntityLifecycleState lifecycle) const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_) if (slot.occupied && slot.record.lifecycle == lifecycle) result.push_back(slot.record);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); return result;
}
std::vector<EntityRecord> EntityService::FindByMaterialization(EntityMaterializationState state) const
{
    std::vector<EntityRecord> result;
    result.reserve(id_to_slot_.size());
    for (const auto& slot : slots_) if (slot.occupied && slot.record.materialization == state) result.push_back(slot.record);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; }); return result;
}

std::vector<EntityChange> EntityService::ChangesSinceSequence(std::uint64_t sequence) const
{
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence, [](std::uint64_t value, const EntityChange& change) { return value < change.sequence; });
    return {found, changes_.end()};
}
EntityChangeBatch EntityService::ReadChangesSince(ChangeCursor cursor) const
{
    EntityChangeBatch batch;
    batch.oldest_available_cursor = {journal_epoch_, changes_.empty() ? next_change_sequence_ : changes_.front().sequence};
    batch.latest_cursor = {journal_epoch_, last_change_sequence_};
    if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
    {
        batch.snapshot_required = true;
        return batch;
    }
    batch.changes = ChangesSinceSequence(cursor.sequence);
    if (!batch.snapshot_required && batch.changes.empty() && cursor.sequence < batch.latest_cursor.sequence)
        batch.snapshot_required = true;
    if (!changes_.empty() && changes_.front().sequence > 1 && cursor.sequence < changes_.front().sequence - 1)
    {
        batch.changes.clear();
        batch.snapshot_required = true;
    }
    return batch;
}
void EntityService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence, [](const EntityChange& change, std::uint64_t value) { return change.sequence < value; });
    changes_.erase(changes_.begin(), found);
}

EntitySnapshot EntityService::CaptureSnapshot() const
{
    EntitySnapshot snapshot;
    snapshot.id_generator = ids_.GetSnapshot(); snapshot.revision = revision_; snapshot.change_epoch = journal_epoch_;
    for (const auto& slot : slots_)
    {
        if (!slot.occupied || slot.record.persistence != EntityPersistencePolicy::Persistent) continue;
        auto record = slot.record; record.materialization = EntityMaterializationState::Abstract; snapshot.records.push_back(std::move(record));
    }
    for (const auto id : pending_destroy_)
    {
        const auto* record = FindInternal(id);
        if (record == nullptr || record->persistence != EntityPersistencePolicy::Persistent || record->lifecycle != EntityLifecycleState::PendingDestroy) continue;
        PendingEntityDestruction pending; pending.entity = id;
        if (const auto it = pending_destroy_reasons_.find(id); it != pending_destroy_reasons_.end()) pending.reason = it->second;
        if (const auto it = pending_destroy_contexts_.find(id); it != pending_destroy_contexts_.end()) pending.context = it->second;
        snapshot.pending_destruction.push_back(std::move(pending));
    }
    std::sort(snapshot.records.begin(), snapshot.records.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.pending_destruction.begin(), snapshot.pending_destruction.end(), [](const auto& a, const auto& b) { return a.entity < b.entity; });
    return snapshot;
}

foundation::Result<void> EntityService::RestoreSnapshot(EntitySnapshot snapshot)
{
    const auto next_journal_epoch =
        CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "entity service must be frozen before restore"));
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.id_generator) || snapshot.id_generator.scope != ids_.GetSnapshot().scope)
        return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "entity id generator snapshot is invalid"));

    std::unordered_set<EntityId, EntityIdHash> seen;
    std::uint64_t max_generated_low = 0;
    std::uint32_t max_old_generation = 0;
    for (const auto& slot : slots_) if (slot.generation > max_old_generation) max_old_generation = slot.generation;
    if (max_old_generation == std::numeric_limits<std::uint32_t>::max())
        return foundation::Result<void>::Failure(Error("gameplay.entity_handle_generation_exhausted", "cannot invalidate all pre-restore entity handles"));
    const auto restored_generation = static_cast<std::uint32_t>(max_old_generation + 1);

    for (const auto& record : snapshot.records)
    {
        if (!record.id.IsValid() || FindArchetype(record.archetype) == nullptr || !seen.insert(record.id).second || record.revision > snapshot.revision ||
            record.persistence != EntityPersistencePolicy::Persistent || record.lifecycle == EntityLifecycleState::Removed)
            return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "entity snapshot contains invalid or duplicate records"));
        if (record.id.High() == snapshot.id_generator.scope && record.id.Low() > max_generated_low) max_generated_low = record.id.Low();
    }
    if (snapshot.id_generator.next != 0 && snapshot.id_generator.next <= max_generated_low)
        return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "entity id generator can reproduce a restored entity id"));

    std::unordered_map<EntityId, PendingEntityDestruction, EntityIdHash> pending_by_id;
    for (const auto& pending : snapshot.pending_destruction)
    {
        if (!pending.entity.IsValid() || !pending_by_id.emplace(pending.entity, pending).second)
            return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "pending destruction records are invalid or duplicated"));
        const auto record = std::find_if(snapshot.records.begin(), snapshot.records.end(), [&](const auto& r) { return r.id == pending.entity; });
        if (record == snapshot.records.end() || record->lifecycle != EntityLifecycleState::PendingDestroy)
            return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "pending destruction metadata does not reference a pending entity"));
    }
    for (const auto& record : snapshot.records)
        if (record.lifecycle == EntityLifecycleState::PendingDestroy && !pending_by_id.contains(record.id))
            return foundation::Result<void>::Failure(Error("gameplay.entity_snapshot_invalid", "pending entity is missing destruction provenance"));

    std::vector<Slot> rebuilt_slots;
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> rebuilt_index;
    std::vector<EntityId> rebuilt_pending;
    std::unordered_map<EntityId, EntityDestroyReason, EntityIdHash> rebuilt_reasons;
    std::unordered_map<EntityId, GameplayContext, EntityIdHash> rebuilt_contexts;
    rebuilt_slots.reserve(snapshot.records.size());
    for (auto& record : snapshot.records)
    {
        const auto slot_index = static_cast<std::uint32_t>(rebuilt_slots.size());
        record.materialization = EntityMaterializationState::Abstract;
        Slot slot; slot.record = std::move(record); slot.generation = restored_generation; slot.occupied = true;
        rebuilt_index.emplace(slot.record.id, slot_index);
        if (slot.record.lifecycle == EntityLifecycleState::PendingDestroy)
        {
            rebuilt_pending.push_back(slot.record.id);
            const auto& metadata = pending_by_id.at(slot.record.id);
            rebuilt_reasons.emplace(slot.record.id, metadata.reason);
            rebuilt_contexts.emplace(slot.record.id, metadata.context);
        }
        rebuilt_slots.push_back(std::move(slot));
    }

    slots_ = std::move(rebuilt_slots); free_slots_.clear(); id_to_slot_ = std::move(rebuilt_index);
    pending_destroy_ = std::move(rebuilt_pending); pending_destroy_reasons_ = std::move(rebuilt_reasons); pending_destroy_contexts_ = std::move(rebuilt_contexts);
    ids_.Restore(snapshot.id_generator); revision_ = snapshot.revision;
    changes_.clear(); next_change_sequence_ = 1; last_change_sequence_ = 0; journal_epoch_ = *next_journal_epoch;
    creates_ = 0; destroys_ = 0; invalid_handle_lookups_ = 0;
    return foundation::Result<void>::Success();
}

EntitiesDiagnostics EntityService::GetDiagnostics() const noexcept
{
    EntitiesDiagnostics result;
    result.entity_count = id_to_slot_.size(); result.creates = creates_; result.destroys = destroys_; result.invalid_handle_lookups = invalid_handle_lookups_;
    for (const auto& slot : slots_)
    {
        if (!slot.occupied) continue;
        if (slot.record.persistence == EntityPersistencePolicy::Persistent) ++result.persistent_entities;
        if (slot.record.materialization == EntityMaterializationState::Abstract) ++result.abstract_entities;
        if (slot.record.materialization == EntityMaterializationState::Materialized) ++result.materialized_entities;
        if (slot.record.lifecycle == EntityLifecycleState::PendingDestroy) ++result.pending_destruction;
    }
    return result;
}
} // namespace epidemic::gameplay::entities
