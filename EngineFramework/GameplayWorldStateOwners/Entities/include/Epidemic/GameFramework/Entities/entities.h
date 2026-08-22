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
    [[nodiscard]] static constexpr EntityId FromRaw(std::uint64_t high, std::uint64_t low) noexcept { return EntityId{GameplayObjectId::FromRaw(high, low)}; }
    [[nodiscard]] static constexpr EntityId FromString(std::string_view value) noexcept { return EntityId{GameplayObjectId::FromString(value)}; }
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
    [[nodiscard]] static constexpr EntityArchetypeId FromString(std::string_view value) noexcept { return EntityArchetypeId{TypeId::FromString(value)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityArchetypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityArchetypeId&) const noexcept = default;
};

struct EntityPartId
{
    TypeId value{};
    [[nodiscard]] static constexpr EntityPartId FromString(std::string_view value) noexcept { return EntityPartId{TypeId::FromString(value)}; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value.Raw(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityPartId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityPartId&) const noexcept = default;
};

struct EntityPartRef
{
    EntityId entity{};
    EntityPartId part{};
    [[nodiscard]] constexpr bool IsValid() const noexcept { return entity.IsValid() && part.IsValid(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const EntityPartRef&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EntityPartRef&) const noexcept = default;
};

struct EntityIdHash { [[nodiscard]] std::size_t operator()(const EntityId& id) const noexcept { return std::hash<GameplayObjectId>{}(id.value); } };
struct EntityArchetypeIdHash { [[nodiscard]] std::size_t operator()(EntityArchetypeId id) const noexcept { return std::hash<TypeId>{}(id.value); } };
struct EntityPartIdHash { [[nodiscard]] std::size_t operator()(EntityPartId id) const noexcept { return std::hash<TypeId>{}(id.value); } };

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
    Transient, // runtime-only entity; omitted from SaveGame snapshots
    Session,   // authoritative for the current session, omitted from SaveGame snapshots
    Persistent // restored by SaveGame
};
enum class EntityMaterializationPolicy { AbstractAllowed, PreferMaterialized, RequireMaterialized };
enum class EntityLifecycleState { Creating, Alive, Dormant, PendingDestroy, Destroyed, Removed };
enum class EntityMaterializationState { Abstract, Materializing, Materialized, Dematerializing };
enum class EntityDestroyReason { Destroyed, Removed, Consumed, Expired, Converted, OwnerRequest };

struct EntityPartDefinition
{
    EntityPartId id{};
    std::string canonical_name;
    EntityPartId parent{};
    GameplayTagSet tags;
};

struct EntityArchetypeDefinition
{
    EntityArchetypeId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    std::vector<EntityPartDefinition> parts;
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

struct PendingEntityDestruction
{
    EntityId entity{};
    EntityDestroyReason reason = EntityDestroyReason::Destroyed;
    GameplayContext context{};
};

struct CreateEntityRequest
{
    EntityArchetypeId archetype{};
    std::optional<EntityId> requested_id{};
    GameplayTagSet additional_tags;
    std::optional<EntityPersistencePolicy> persistence{};
    GameplayContext context{};
};

struct CreateEntityResult { EntityId id{}; EntityHandle handle{}; };
struct EntityConversionPolicy { bool preserve_instance_tags = true; bool preserve_persistence = true; };

enum class EntityChangeKind
{
    Created,
    Activated,
    Deactivated,
    DestroyRequested,
    Destroyed,
    Removed,
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
    EntityLifecycleState previous_lifecycle = EntityLifecycleState::Creating;
    EntityLifecycleState lifecycle = EntityLifecycleState::Alive;
    EntityMaterializationState previous_materialization = EntityMaterializationState::Abstract;
    EntityMaterializationState materialization = EntityMaterializationState::Abstract;
    TagId tag{};
    EntityDestroyReason destroy_reason = EntityDestroyReason::Destroyed;
    GameplayContext context{};
    Revision revision{};
};

struct EntitySnapshot
{
    std::vector<EntityRecord> records;
    std::vector<PendingEntityDestruction> pending_destruction;
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

    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.entities"); }
    [[nodiscard]] static constexpr GameplayObjectRef ToGameplayObjectRef(EntityId id) noexcept { return GameplayObjectRef{Domain(), id.value}; }
    [[nodiscard]] static constexpr EntityId FromGameplayObjectRef(GameplayObjectRef ref) noexcept { return ref.domain == Domain() ? EntityId{ref.id} : EntityId{}; }
    [[nodiscard]] static constexpr GameplayObjectPartRef ToGameplayObjectPartRef(EntityPartRef ref) noexcept
    {
        return ref.IsValid() ? GameplayObjectPartRef{ToGameplayObjectRef(ref.entity), ref.part.value} : GameplayObjectPartRef{};
    }
    [[nodiscard]] static constexpr EntityPartRef FromGameplayObjectPartRef(GameplayObjectPartRef ref) noexcept
    {
        const auto entity = FromGameplayObjectRef(ref.object);
        return entity.IsValid() && ref.part.IsValid() ? EntityPartRef{entity, EntityPartId{ref.part}} : EntityPartRef{};
    }

    [[nodiscard]] foundation::Result<EntityArchetypeId> RegisterArchetype(EntityArchetypeDefinition definition);
    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const EntityArchetypeDefinition* FindArchetype(EntityArchetypeId id) const noexcept;
    [[nodiscard]] const EntityPartDefinition* FindPartDefinition(EntityArchetypeId archetype, EntityPartId part) const noexcept;
    [[nodiscard]] std::optional<EntityPartRef> GetPart(EntityId entity, EntityPartId part) const;
    [[nodiscard]] std::vector<EntityPartRef> GetParts(EntityId entity) const;
    [[nodiscard]] std::vector<EntityPartRef> GetPartAncestors(EntityPartRef part) const;

    [[nodiscard]] foundation::Result<CreateEntityResult> Create(CreateEntityRequest request);
    [[nodiscard]] foundation::Result<void> Activate(EntityId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Deactivate(EntityId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RequestDestroy(EntityId id, EntityDestroyReason reason, GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::vector<EntityRecord>> CommitPendingDestruction();
    [[nodiscard]] foundation::Result<void> Remove(EntityId id, GameplayContext context = {});

    [[nodiscard]] foundation::Result<void> Convert(EntityId id, EntityArchetypeId new_archetype, EntityConversionPolicy policy = {}, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetMaterializationState(EntityId id, EntityMaterializationState state, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> AddTag(EntityId id, TagId tag, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveTag(EntityId id, TagId tag, GameplayContext context = {});

    [[nodiscard]] std::optional<EntityRecord> Find(EntityId id) const;
    [[nodiscard]] std::optional<EntityRecord> Resolve(EntityHandle handle) const;
    [[nodiscard]] EntityHandle HandleOf(EntityId id) const noexcept;
    [[nodiscard]] bool Exists(EntityId id) const noexcept { return id_to_slot_.contains(id); }

    [[nodiscard]] std::vector<EntityRecord> AllEntities() const;
    [[nodiscard]] std::vector<EntityRecord> FindByArchetype(EntityArchetypeId archetype) const;
    [[nodiscard]] std::vector<EntityRecord> FindByTag(TagId tag, const GameplayTagRegistry& registry) const;
    [[nodiscard]] std::vector<EntityRecord> FindByLifecycle(EntityLifecycleState lifecycle) const;
    [[nodiscard]] std::vector<EntityRecord> FindByMaterialization(EntityMaterializationState state) const;

    [[nodiscard]] std::vector<EntityChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept;
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] EntitySnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EntitySnapshot snapshot);
    [[nodiscard]] EntitiesDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }

  private:
    struct Slot { EntityRecord record{}; std::uint32_t generation = 1; bool occupied = false; };

    [[nodiscard]] foundation::Result<std::uint32_t> AllocateSlot();
    [[nodiscard]] EntityRecord* FindMutable(EntityId id) noexcept;
    [[nodiscard]] const EntityRecord* FindInternal(EntityId id) const noexcept;
    [[nodiscard]] Slot* ResolveSlot(EntityHandle handle) noexcept;
    [[nodiscard]] const Slot* ResolveSlot(EntityHandle handle) const noexcept;
    [[nodiscard]] foundation::Result<Revision> PrepareRevision() const;
    [[nodiscard]] bool CanRecordChanges(std::size_t count) const noexcept;
    void RecordChange(EntityChange change) noexcept;
    [[nodiscard]] foundation::Result<void> ValidateArchetypeParts(EntityArchetypeDefinition& definition) const;

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
    std::uint64_t last_change_sequence_ = 0;

    std::uint64_t creates_ = 0;
    std::uint64_t destroys_ = 0;
    mutable std::uint64_t invalid_handle_lookups_ = 0;
};
} // namespace epidemic::gameplay::entities

namespace std
{
template <> struct hash<epidemic::gameplay::entities::EntityId> { [[nodiscard]] size_t operator()(const epidemic::gameplay::entities::EntityId& id) const noexcept { return epidemic::gameplay::entities::EntityIdHash{}(id); } };
template <> struct hash<epidemic::gameplay::entities::EntityArchetypeId> { [[nodiscard]] size_t operator()(epidemic::gameplay::entities::EntityArchetypeId id) const noexcept { return epidemic::gameplay::entities::EntityArchetypeIdHash{}(id); } };
template <> struct hash<epidemic::gameplay::entities::EntityPartId> { [[nodiscard]] size_t operator()(epidemic::gameplay::entities::EntityPartId id) const noexcept { return epidemic::gameplay::entities::EntityPartIdHash{}(id); } };
} // namespace std
