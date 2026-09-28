#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::encounters
{
struct EncounterDefinitionId
{
    TypeId value{};
    static constexpr EncounterDefinitionId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const EncounterDefinitionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EncounterDefinitionId &) const noexcept = default;
};
struct EncounterInstanceId
{
    GameplayObjectId value{};
    static constexpr EncounterInstanceId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr EncounterInstanceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const EncounterInstanceId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const EncounterInstanceId &) const noexcept = default;
};
struct SpawnTableId
{
    TypeId value{};
    static constexpr SpawnTableId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SpawnTableId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SpawnTableId &) const noexcept = default;
};
struct SpawnEntryId
{
    TypeId value{};
    static constexpr SpawnEntryId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SpawnEntryId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SpawnEntryId &) const noexcept = default;
};
struct SpawnPointId
{
    GameplayObjectId value{};
    static constexpr SpawnPointId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr SpawnPointId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SpawnPointId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SpawnPointId &) const noexcept = default;
};
struct SpawnRequestId
{
    GameplayObjectId value{};
    static constexpr SpawnRequestId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr SpawnRequestId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SpawnRequestId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SpawnRequestId &) const noexcept = default;
};
struct SpawnedEntityRecordId
{
    GameplayObjectId value{};
    static constexpr SpawnedEntityRecordId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr SpawnedEntityRecordId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SpawnedEntityRecordId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SpawnedEntityRecordId &) const noexcept = default;
};
struct RespawnRuleId
{
    GameplayObjectId value{};
    static constexpr RespawnRuleId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr RespawnRuleId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const RespawnRuleId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RespawnRuleId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class EncounterState
{
    AwaitingPopulationBinding,
    Active,
    Completed,
    Failed,
    Despawning,
    Expired
};
enum class SpawnPointState
{
    Available,
    Reserved,
    Disabled,
    Occupied,
    Consumed
};
enum class SpawnRollPolicy
{
    GuaranteedAll,
    WeightedOne,
    WeightedMany,
    IndependentChance,
    PickNWithoutReplacement,
    PopulationBacked
};
enum class SpawnPersistencePolicy
{
    Transient,
    Session,
    Persistent,
    PopulationBacked
};
enum class SpawnResultState
{
    Succeeded,
    Deferred,
    Rejected,
    Failed
};
enum class EncounterChangeKind
{
    EncounterCreated,
    EncounterActivated,
    EncounterCompleted,
    EncounterFailed,
    EncounterExpired,
    SpawnRequested,
    SpawnSucceeded,
    SpawnFailed,
    EntitySpawned,
    EntityDespawned,
    RespawnScheduled,
    RespawnTriggered,
    SpawnPointAdded
};

struct SpawnEntry
{
    SpawnEntryId id{};
    TypeId archetype{};
    std::uint64_t weight = 1;
    std::uint32_t min_count = 1;
    std::uint32_t max_count = 1;
    GameplayTagSet required_area_tags{};
    GameplayTagSet blocked_area_tags{};
    GameplayTagSet required_request_tags{};
    GameplayTagSet blocked_request_tags{};
    std::vector<std::byte> payload;
};
struct SpawnTable
{
    SpawnTableId id{};
    std::vector<SpawnEntry> entries;
    SpawnRollPolicy roll_policy = SpawnRollPolicy::GuaranteedAll;
    std::vector<std::byte> payload;
    Revision revision{};
    std::uint32_t selection_min_count = 1;
    std::uint32_t selection_max_count = 1;
};
struct EncounterDefinition
{
    EncounterDefinitionId id{};
    GameplayTagSet tags{};
    SpawnTableId spawn_table{};
    SpawnPersistencePolicy persistence = SpawnPersistencePolicy::Transient;
    GameplayTagSet required_area_tags{};
    GameplayTagSet blocked_area_tags{};
    GameplayTagSet required_spawn_point_tags{};
    GameplayTagSet blocked_spawn_point_tags{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct SpawnPoint
{
    SpawnPointId id{};
    GameplayObjectRef area{};
    GameplayObjectRef position{};
    GameplayTagSet tags{};
    SpawnPointState state = SpawnPointState::Available;
    Revision revision{};
};
struct SpawnedEntityRecord
{
    SpawnedEntityRecordId id{};
    EncounterInstanceId encounter{};
    GameplayObjectRef entity{};
    TypeId archetype{};
    GameplayObjectRef population_unit{};
    Revision revision{};
};
struct EncounterInstance
{
    EncounterInstanceId id{};
    EncounterDefinitionId definition{};
    GameplayObjectRef area{};
    GameplayObjectRef origin{};
    std::optional<SpawnPointId> spawn_point;
    EncounterState state = EncounterState::Active;
    std::vector<SpawnedEntityRecordId> spawned_entities;
    GameplayTimePoint created_at{};
    GameplayTimePoint last_updated_at{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct RespawnRule
{
    RespawnRuleId id{};
    EncounterDefinitionId encounter{};
    GameplayDuration delay{};
    std::uint32_t remaining_limit = 0;
    GameplayObjectRef area{};
    std::optional<GameplayObjectRef> origin;
    std::optional<SpawnPointId> spawn_point;
    GameplayTimePoint next_due_at{};
    random::RandomSeed seed{};
    std::uint32_t trigger_count = 0;
    Revision revision{};
};
struct SpawnRequest
{
    SpawnRequestId id{};
    EncounterDefinitionId encounter{};
    GameplayObjectRef area{};
    std::optional<GameplayObjectRef> origin;
    std::optional<SpawnPointId> spawn_point;
    random::RandomSeed seed{};
    SpawnPersistencePolicy persistence = SpawnPersistencePolicy::Transient;
    GameplayTagSet area_tags{};
    GameplayTagSet request_tags{};
    GameplayContext context{};
};
struct SpawnResult
{
    SpawnRequestId request_id{};
    EncounterInstanceId encounter_instance{};
    std::vector<SpawnedEntityRecordId> spawned_records;
    std::vector<GameplayObjectRef> created_entities;
    std::vector<GameplayObjectRef> linked_population_units;
    SpawnResultState state = SpawnResultState::Failed;
    Revision revision{};
};
struct EncounterChange
{
    std::uint64_t sequence = 0;
    EncounterChangeKind kind = EncounterChangeKind::EncounterCreated;
    EncounterInstanceId encounter{};
    GameplayObjectRef entity{};
    GameplayObjectRef area{};
    GameplayContext context{};
    Revision revision{};
    SpawnPointId spawn_point{};
};

struct SpawnRequestRecord
{
    SpawnRequest request{};
    SpawnResult result{};
    Revision revision{};
};
struct EncounterChangeBatch
{
    std::vector<EncounterChange> changes;
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};
struct EncountersSnapshot
{
    std::vector<EncounterDefinition> definitions;
    std::vector<SpawnTable> tables;
    std::vector<SpawnPoint> points;
    std::vector<EncounterInstance> instances;
    std::vector<SpawnedEntityRecord> spawned;
    std::vector<RespawnRule> respawn_rules;
    std::vector<SpawnRequestRecord> requests;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot instance_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot point_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot request_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot spawned_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot respawn_ids{};
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct EncounterBudgets
{
    std::uint32_t max_active_global = 1000;
    std::uint32_t max_active_per_area = 128;
    std::uint32_t max_spawn_operations_per_tick = 64;
    std::uint32_t max_spawned_entities_per_encounter = 256;
    std::size_t change_journal_capacity = 4096;
};
struct EncounterDiagnostics
{
    std::uint64_t definitions = 0, tables = 0, active_encounters = 0, spawn_requests = 0, spawn_successes = 0,
                  spawn_failures = 0, spawn_budget_rejects = 0, spawned_entities = 0, respawn_schedules = 0;
};

class EncountersService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.encounters");
    }
    [[nodiscard]] foundation::Result<void> RegisterSpawnTable(SpawnTable table);
    [[nodiscard]] foundation::Result<void> RegisterEncounterDefinition(EncounterDefinition definition);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] bool DefinitionsFrozen() const noexcept { return definitions_frozen_; }
    [[nodiscard]] foundation::Result<SpawnPointId> AddSpawnPoint(SpawnPoint point);
    void SetBudgets(EncounterBudgets budgets) noexcept
    {
        budgets_ = budgets;
    }
    [[nodiscard]] SpawnResult SpawnEncounter(SpawnRequest request);
    [[nodiscard]] foundation::Result<std::vector<TypeId>> PreviewSpawnArchetypes(SpawnRequest request) const;
    [[nodiscard]] foundation::Result<void> BindSpawnedEntity(SpawnedEntityRecordId record, GameplayObjectRef entity,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> BindPopulationUnit(SpawnedEntityRecordId record, GameplayObjectRef unit,
                                                              GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ActivatePopulationBackedEncounter(EncounterInstanceId id,
                                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteEncounter(EncounterInstanceId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> FailEncounter(EncounterInstanceId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> BeginDespawningEncounter(EncounterInstanceId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ExpireEncounter(EncounterInstanceId id, GameplayContext context = {});
    [[nodiscard]] std::size_t PruneTerminalEncounters(std::size_t max_to_prune);
    [[nodiscard]] foundation::Result<RespawnRuleId> ScheduleRespawn(RespawnRule rule, GameplayContext context = {});
    [[nodiscard]] std::vector<SpawnResult> ProcessDueRespawns(GameplayTimePoint now, std::size_t max_rules,
                                                              GameplayContext context = {});
    [[nodiscard]] std::vector<EncounterDefinition> FindDefinitionsByTag(TagId tag) const;
    [[nodiscard]] const EncounterInstance *GetEncounterInstance(EncounterInstanceId id) const noexcept;
    [[nodiscard]] const SpawnedEntityRecord *GetSpawnedEntityRecord(SpawnedEntityRecordId id) const noexcept;
    [[nodiscard]] const SpawnTable *GetSpawnTable(SpawnTableId id) const noexcept;
    [[nodiscard]] std::vector<EncounterInstance> FindActiveEncountersInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<EncounterInstance> FindEncountersByState(EncounterState state) const;
    [[nodiscard]] std::vector<SpawnPoint> FindSpawnPointsInArea(GameplayObjectRef area) const;
    [[nodiscard]] bool CanSpawnEncounter(SpawnRequest request) const;
    private:
        [[nodiscard]] EncounterChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] EncounterChangeBatch ReadChangesSince(ChangeCursor cursor) const
    {
        auto batch = ReadChangesSinceSequence(cursor.sequence);
        batch.oldest_available_cursor = {journal_epoch_, batch.oldest_available_sequence};
        batch.latest_cursor = {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                    : next_change_sequence_ - 1};
        if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
        {
            batch.changes.clear();
            batch.snapshot_required = true;
        }
        return batch;
    }
    [[nodiscard]] ChangeCursor LatestChangeCursor() const noexcept
    {
        return {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                          : next_change_sequence_ - 1};
    }
  private:
    [[nodiscard]] std::vector<EncounterChange> ChangesSinceSequence(std::uint64_t sequence) const
    {
        return ReadChangesSinceSequence(sequence).changes;
    }

  public:
    [[nodiscard]] EncountersSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EncountersSnapshot snapshot);
    [[nodiscard]] EncounterDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    [[nodiscard]] foundation::Result<Revision> PrepareRevision() const;
    void Record(EncounterChange change) noexcept;
    [[nodiscard]] foundation::Result<std::vector<SpawnEntry>> ResolveEntries(const SpawnTable &table,
                                                                               const SpawnRequest &request) const;
    [[nodiscard]] bool EntryMatches(const SpawnEntry &entry, const SpawnRequest &request) const noexcept;
    [[nodiscard]] bool RequestMatches(const SpawnRequest &a, const SpawnRequest &b) const noexcept;
    void RebuildIndexes();
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> instance_ids_{0x2700};
    MonotonicIdGenerator<GameplayObjectId> point_ids_{0x2701};
    MonotonicIdGenerator<GameplayObjectId> request_ids_{0x2702};
    MonotonicIdGenerator<GameplayObjectId> spawned_ids_{0x2703};
    MonotonicIdGenerator<GameplayObjectId> respawn_ids_{0x2704};
    std::unordered_map<EncounterDefinitionId, EncounterDefinition, IdHash> definitions_;
    std::unordered_map<SpawnTableId, SpawnTable, IdHash> tables_;
    std::unordered_map<SpawnPointId, SpawnPoint, IdHash> points_;
    std::unordered_map<EncounterInstanceId, EncounterInstance, IdHash> instances_;
    std::unordered_map<SpawnedEntityRecordId, SpawnedEntityRecord, IdHash> spawned_;
    std::unordered_map<RespawnRuleId, RespawnRule, IdHash> respawns_;
    std::unordered_map<SpawnRequestId, SpawnRequestRecord, IdHash> requests_;
    std::unordered_map<GameplayObjectRef, std::vector<EncounterInstanceId>> instances_by_area_;
    std::unordered_map<EncounterState, std::vector<EncounterInstanceId>> instances_by_state_;
    std::deque<EncounterChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    EncounterBudgets budgets_{};
    bool definitions_frozen_ = false;
    GameplayTickId spawn_budget_tick_{};
    std::uint32_t spawn_operations_used_ = 0;
    mutable EncounterDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::encounters
