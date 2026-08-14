#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::entities
{
struct EntityId
{
    GameplayObjectId value{};

    [[nodiscard]] static constexpr EntityId FromRaw(std::uint64_t high, std::uint64_t low) noexcept
    {
        return EntityId{GameplayObjectId::FromRaw(high, low)};
    }
    [[nodiscard]] static constexpr EntityId FromString(std::string_view value) noexcept
    {
        return EntityId{GameplayObjectId::FromString(value)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t High() const noexcept { return value.High(); }
    [[nodiscard]] constexpr std::uint64_t Low() const noexcept { return value.Low(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityId&) const noexcept = default;
};

struct EntityArchetypeId
{
    TypeId value{};

    [[nodiscard]] static constexpr EntityArchetypeId FromString(std::string_view value) noexcept
    {
        return EntityArchetypeId{TypeId::FromString(value)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityArchetypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityArchetypeId&) const noexcept = default;
};

struct EntityIdHash
{
    [[nodiscard]] std::size_t operator()(const EntityId& id) const noexcept
    {
        return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct EntityArchetypeIdHash
{
    [[nodiscard]] std::size_t operator()(EntityArchetypeId id) const noexcept
    {
        return std::hash<TypeId>{}(id.value);
    }
};

struct EntityHandle
{
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityHandle&) const noexcept = default;
};

enum class EntityPersistencePolicy
{
    Transient,
    Session,
    Persistent,
};

enum class EntityMaterializationPolicy
{
    AbstractAllowed,
    PreferMaterialized,
    RequireMaterialized,
};

enum class EntityLifecycleState
{
    Creating,
    Alive,
    PendingDestroy,
    Destroyed,
};

enum class EntityMaterializationState
{
    Abstract,
    Materializing,
    Materialized,
    Dematerializing,
};

enum class EntityDestroyReason
{
    Destroyed,
    Removed,
    Consumed,
    Expired,
    Converted,
    OwnerRequest,
};

struct EntityArchetypeDefinition
{
    EntityArchetypeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    EntityPersistencePolicy persistence = EntityPersistencePolicy::Persistent;
    EntityMaterializationPolicy materialization = EntityMaterializationPolicy::AbstractAllowed;
};

struct EntityRecord
{
    EntityId id{};
    EntityArchetypeId archetype{};
    EntityLifecycleState lifecycle = EntityLifecycleState::Creating;
    EntityMaterializationState materialization = EntityMaterializationState::Abstract;
    GameplayTagSet instance_tags;
    Revision revision{};
    EntityPersistencePolicy persistence = EntityPersistencePolicy::Persistent;
};

struct CreateEntityRequest
{
    EntityArchetypeId archetype{};
    std::optional<EntityId> requested_id{};
    GameplayTagSet additional_tags;
    std::optional<EntityPersistencePolicy> persistence{};
    GameplayContext context{};
};

struct CreateEntityResult
{
    EntityId id{};
    EntityHandle handle{};
};

struct EntityConversionPolicy
{
    bool preserve_instance_tags = true;
    bool preserve_persistence = true;
};

enum class EntityChangeKind
{
    Created,
    DestroyRequested,
    Destroyed,
    Converted,
    MaterializationChanged,
    TagAdded,
    TagRemoved,
};

struct EntityChange
{
    std::uint64_t sequence = 0;
    EntityChangeKind kind = EntityChangeKind::Created;
    EntityId entity{};
    EntityArchetypeId previous_archetype{};
    EntityArchetypeId archetype{};
    EntityLifecycleState lifecycle = EntityLifecycleState::Alive;
    EntityMaterializationState previous_materialization = EntityMaterializationState::Abstract;
    EntityMaterializationState materialization = EntityMaterializationState::Abstract;
    TagId tag{};
    EntityDestroyReason destroy_reason = EntityDestroyReason::Destroyed;
    GameplayContext context{};
};

struct EntitySnapshot
{
    std::vector<EntityRecord> records;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot id_generator{};
    Revision revision{};
};

struct EntitiesDiagnostics
{
    std::uint64_t entity_count = 0;
    std::uint64_t persistent_entities = 0;
    std::uint64_t abstract_entities = 0;
    std::uint64_t materialized_entities = 0;
    std::uint64_t pending_destruction = 0;
    std::uint64_t creates = 0;
    std::uint64_t destroys = 0;
    std::uint64_t invalid_handle_lookups = 0;
};

class EntityService
{
  public:
    EntityService();

    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.entities");
    }

    [[nodiscard]] static constexpr GameplayObjectRef ToGameplayObjectRef(EntityId id) noexcept
    {
        return GameplayObjectRef{Domain(), id.value};
    }

    [[nodiscard]] static constexpr EntityId FromGameplayObjectRef(GameplayObjectRef ref) noexcept
    {
        return ref.domain == Domain() ? EntityId{ref.id} : EntityId{};
    }

    [[nodiscard]] foundation::Result<EntityArchetypeId> RegisterArchetype(EntityArchetypeDefinition definition);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const EntityArchetypeDefinition* FindArchetype(EntityArchetypeId id) const noexcept;

    [[nodiscard]] foundation::Result<CreateEntityResult> Create(CreateEntityRequest request);
    [[nodiscard]] foundation::Result<void> RequestDestroy(EntityId id, EntityDestroyReason reason, GameplayContext context = {});
    [[nodiscard]] std::vector<EntityRecord> CommitPendingDestruction();

    [[nodiscard]] foundation::Result<void> Convert(
        EntityId id,
        EntityArchetypeId new_archetype,
        EntityConversionPolicy policy = {},
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> SetMaterializationState(
        EntityId id,
        EntityMaterializationState state,
        GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> AddTag(EntityId id, TagId tag, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveTag(EntityId id, TagId tag, GameplayContext context = {});

    [[nodiscard]] const EntityRecord* Find(EntityId id) const noexcept;
    [[nodiscard]] const EntityRecord* Resolve(EntityHandle handle) const noexcept;
    [[nodiscard]] EntityHandle HandleOf(EntityId id) const noexcept;
    [[nodiscard]] bool Exists(EntityId id) const noexcept { return Find(id) != nullptr; }

    [[nodiscard]] std::vector<EntityRecord> AllEntities() const;
    [[nodiscard]] std::vector<EntityRecord> FindByArchetype(EntityArchetypeId archetype) const;
    [[nodiscard]] std::vector<EntityRecord> FindByTag(TagId tag, const GameplayTagRegistry& registry) const;
    [[nodiscard]] std::vector<EntityRecord> FindByLifecycle(EntityLifecycleState lifecycle) const;
    [[nodiscard]] std::vector<EntityRecord> FindByMaterialization(EntityMaterializationState state) const;

    [[nodiscard]] std::vector<EntityChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_ - 1; }
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] EntitySnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EntitySnapshot snapshot);
    [[nodiscard]] EntitiesDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    struct Slot
    {
        EntityRecord record{};
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    [[nodiscard]] foundation::Result<std::uint32_t> AllocateSlot();
    [[nodiscard]] EntityRecord* FindMutable(EntityId id) noexcept;
    [[nodiscard]] Slot* ResolveSlot(EntityHandle handle) noexcept;
    [[nodiscard]] const Slot* ResolveSlot(EntityHandle handle) const noexcept;
    void RecordChange(EntityChange change);
    void BumpRevision(EntityRecord& record) noexcept;

    std::unordered_map<EntityArchetypeId, EntityArchetypeDefinition, EntityArchetypeIdHash> archetypes_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_slots_;
    std::unordered_map<EntityId, std::uint32_t, EntityIdHash> id_to_slot_;
    std::vector<EntityId> pending_destroy_;
    std::unordered_map<EntityId, EntityDestroyReason, EntityIdHash> pending_destroy_reasons_;
    std::unordered_map<EntityId, GameplayContext, EntityIdHash> pending_destroy_contexts_;

    MonotonicIdGenerator<GameplayObjectId> ids_;
    Revision revision_{};
    bool frozen_ = false;

    std::vector<EntityChange> changes_;
    std::uint64_t next_change_sequence_ = 1;

    std::uint64_t creates_ = 0;
    std::uint64_t destroys_ = 0;
    mutable std::uint64_t invalid_handle_lookups_ = 0;
};
} // namespace epidemic::gameplay::entities

