#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::gameplay::population
{
struct PopulationGroupId
{
    GameplayObjectId value{};
    static constexpr PopulationGroupId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PopulationGroupId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationGroupId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationGroupId &) const noexcept = default;
};
struct PopulationUnitId
{
    GameplayObjectId value{};
    static constexpr PopulationUnitId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PopulationUnitId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationUnitId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationUnitId &) const noexcept = default;
};
struct PopulationProfileId
{
    TypeId value{};
    static constexpr PopulationProfileId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationProfileId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationProfileId &) const noexcept = default;
};
struct PopulationTemplateId
{
    TypeId value{};
    static constexpr PopulationTemplateId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationTemplateId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationTemplateId &) const noexcept = default;
};
struct PopulationResidenceId
{
    GameplayObjectId value{};
    static constexpr PopulationResidenceId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PopulationResidenceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationResidenceId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationResidenceId &) const noexcept = default;
};
struct PopulationMigrationId
{
    GameplayObjectId value{};
    static constexpr PopulationMigrationId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PopulationMigrationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationMigrationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationMigrationId &) const noexcept = default;
};
struct PopulationAllocationId
{
    GameplayObjectId value{};
    static constexpr PopulationAllocationId FromString(std::string_view s) noexcept
    {
        return {GameplayObjectId::FromString(s)};
    }
    static constexpr PopulationAllocationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const PopulationAllocationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const PopulationAllocationId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class PopulationGroupState
{
    Active,
    Dormant,
    Depleted,
    Removed
};
enum class PopulationUnitState
{
    Latent,
    Abstract,
    Materialized,
    Dead,
    Removed
};
enum class ResidenceState
{
    Assigned,
    Unavailable,
    Abandoned,
    Destroyed
};
enum class MigrationState
{
    Planned,
    Active,
    Completed,
    Cancelled,
    Failed
};
enum class PopulationAllocationState
{
    Active,
    Committed,
    Released
};
enum class PopulationChangeKind
{
    GroupCreated,
    GroupChanged,
    UnitCreated,
    UnitMaterialized,
    UnitDematerialized,
    UnitDied,
    UnitMigrated,
    UnitRetired,
    ResidenceAssigned,
    MigrationStarted,
    MigrationCompleted,
    MigrationCancelled,
    MigrationFailed,
    EntityBindingChanged,
    ResidenceChanged,
    AllocationReserved,
    AllocationCommitted,
    AllocationReleased
};

struct PopulationTemplate
{
    PopulationTemplateId id{};
    TypeId entity_archetype{};
    GameplayTagSet tags{};
    std::vector<std::byte> generation_payload;
    bool persistent = true;
    Revision revision{};
};
struct PopulationGroup
{
    PopulationGroupId id{};
    GameplayObjectRef area{};
    GameplayObjectRef society_group{};
    PopulationProfileId profile{};
    PopulationGroupState state = PopulationGroupState::Active;
    std::uint32_t desired_count = 0;
    std::uint32_t current_known_count = 0;
    std::uint32_t materialized_count = 0;
    Revision revision{};
};
struct PopulationUnit
{
    PopulationUnitId id{};
    PopulationGroupId group{};
    std::optional<GameplayObjectRef> entity;
    PopulationTemplateId template_id{};
    PopulationUnitState state = PopulationUnitState::Latent;
    GameplayObjectRef home_area{};
    GameplayObjectRef current_area{};
    GameplayTimePoint created_at{};
    GameplayTimePoint last_simulated_at{};
    GameplayTagSet tags{};
    std::vector<std::byte> payload;
    Revision revision{};
};
struct PopulationResidence
{
    PopulationResidenceId id{};
    PopulationUnitId unit{};
    GameplayObjectRef home_area{};
    GameplayObjectRef home_property{};
    ResidenceState state = ResidenceState::Assigned;
    Revision revision{};
};
struct PopulationMigration
{
    PopulationMigrationId id{};
    PopulationUnitId unit{};
    GameplayObjectRef from{};
    GameplayObjectRef to{};
    TypeId reason{};
    MigrationState state = MigrationState::Planned;
    GameplayTimePoint started_at{};
    Revision revision{};
};
struct PopulationAllocation
{
    PopulationAllocationId id{};
    PopulationUnitId unit{};
    TypeId purpose{};
    GameplayObjectId correlation{};
    PopulationAllocationState state = PopulationAllocationState::Active;
    std::optional<GameplayTimePoint> expires_at;
    GameplayObjectRef bound_entity{};
    Revision revision{};
};
struct PopulationAllocationRequest
{
    std::vector<PopulationUnitId> units;
    TypeId purpose{};
    GameplayObjectId correlation{};
    std::optional<GameplayTimePoint> expires_at;
};
struct PopulationAllocationToken
{
    PopulationAllocationId allocation{};
    PopulationUnitId unit{};
};
struct PopulationAllocationBatch
{
    GameplayObjectId correlation{};
    std::vector<PopulationAllocationToken> tokens;
};
struct PopulationChange
{
    std::uint64_t sequence = 0;
    PopulationChangeKind kind = PopulationChangeKind::GroupCreated;
    PopulationGroupId group{};
    PopulationUnitId unit{};
    GameplayObjectRef entity{};
    GameplayObjectRef area{};
    GameplayContext context{};
    Revision revision{};
};
struct PopulationCounts
{
    std::uint32_t latent = 0, abstract_units = 0, materialized = 0, dead = 0, removed = 0;
};
struct PopulationChangeBatch
{
    std::vector<PopulationChange> changes;
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};

struct PopulationSnapshot
{
    std::vector<PopulationGroup> groups;
    std::vector<PopulationUnit> units;
    std::vector<PopulationResidence> residences;
    std::vector<PopulationMigration> migrations;
    std::vector<PopulationAllocation> allocations;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot group_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot unit_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot residence_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot migration_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot allocation_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot allocation_correlation_ids{};
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct PopulationDiagnostics
{
    std::uint64_t groups = 0, units = 0, latent = 0, abstract_units = 0, materialized = 0, migrations = 0,
                  residences = 0, allocations = 0, active_allocations = 0, materialization_requests = 0,
                  dematerialization_requests = 0;
};

class PopulationService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.population");
    }
    [[nodiscard]] foundation::Result<void> RegisterTemplate(PopulationTemplate definition);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] bool DefinitionsFrozen() const noexcept { return definitions_frozen_; }
    [[nodiscard]] foundation::Result<PopulationGroupId> CreateGroup(PopulationGroup group);
    [[nodiscard]] foundation::Result<PopulationUnitId> CreateUnit(PopulationUnit unit, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SetUnitEntity(PopulationUnitId unit, GameplayObjectRef entity,
                                                         GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MaterializeUnit(PopulationUnitId unit, GameplayObjectRef entity,
                                                           GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> DematerializeUnit(PopulationUnitId unit, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MarkUnitDead(PopulationUnitId unit, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RetireUnit(PopulationUnitId unit, GameplayContext context = {});
    [[nodiscard]] foundation::Result<PopulationResidenceId> AssignResidence(PopulationResidence residence,
                                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<PopulationMigrationId> StartMigration(PopulationMigration migration,
                                                                           GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompleteMigration(PopulationMigrationId migration,
                                                             GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CancelMigration(PopulationMigrationId migration,
                                                           GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> FailMigration(PopulationMigrationId migration,
                                                         GameplayContext context = {});
    [[nodiscard]] foundation::Result<PopulationAllocationBatch> ReserveAllocations(
        PopulationAllocationRequest request, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CommitAllocation(PopulationAllocationId allocation,
                                                            GameplayObjectRef entity,
                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ReleaseAllocation(PopulationAllocationId allocation,
                                                             GameplayContext context = {});
    void PruneTerminalAllocations(GameplayObjectId correlation);
    [[nodiscard]] const PopulationGroup *GetGroup(PopulationGroupId id) const noexcept;
    [[nodiscard]] const PopulationUnit *GetUnit(PopulationUnitId id) const noexcept;
    [[nodiscard]] const PopulationTemplate *GetTemplate(PopulationTemplateId id) const noexcept;
    [[nodiscard]] const PopulationAllocation *GetAllocation(PopulationAllocationId id) const noexcept;
    [[nodiscard]] const PopulationAllocation *GetActiveAllocationForUnit(PopulationUnitId unit) const noexcept;
    [[nodiscard]] const PopulationUnit *FindUnitByEntity(GameplayObjectRef entity) const noexcept;
    [[nodiscard]] std::vector<PopulationAllocation> FindAllocationsByCorrelation(GameplayObjectId correlation) const;
    [[nodiscard]] static constexpr GameplayObjectRef ResidentRef(PopulationUnitId unit) noexcept
    {
        return {Domain(), unit.value};
    }
    [[nodiscard]] std::vector<PopulationGroup> FindGroupsInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PopulationUnit> FindUnitsByGroup(PopulationGroupId group) const;
    [[nodiscard]] std::vector<PopulationUnit> FindUnitsByState(PopulationUnitState state) const;
    [[nodiscard]] std::vector<PopulationUnit> FindUnitsInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PopulationUnit> FindResidentsOfArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PopulationUnit> FindMaterializationCandidates(GameplayObjectRef area,
                                                                            std::size_t limit) const;
    [[nodiscard]] PopulationCounts GetPopulationCounts(PopulationGroupId group = {}) const;
    private:
        [[nodiscard]] PopulationChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] PopulationChangeBatch ReadChangesSince(ChangeCursor cursor) const
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
        [[nodiscard]] std::vector<PopulationChange> ChangesSinceSequence(std::uint64_t sequence) const;
    public:
    void PruneChangesThrough(std::uint64_t sequence);
    [[nodiscard]] PopulationSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(PopulationSnapshot snapshot);
    [[nodiscard]] PopulationDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        revision_.value++;
    }
    void Record(PopulationChange change);
    void RecountGroup(PopulationGroupId group);
    void RebuildIndexes();
    void IndexUnit(const PopulationUnit &unit);
    void UnindexUnit(const PopulationUnit &unit);
    void RemoveEntityBinding(PopulationUnit &unit) noexcept;
    [[nodiscard]] foundation::Result<void> BindEntity(PopulationUnit &unit, GameplayObjectRef entity);
    [[nodiscard]] foundation::Result<void> FinishMigration(PopulationMigrationId migration, MigrationState state,
                                                            GameplayContext context);
    [[nodiscard]] PopulationUnit *FindMutableUnit(PopulationUnitId id) noexcept;
    [[nodiscard]] PopulationGroup *FindMutableGroup(PopulationGroupId id) noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> group_ids_{0x2600};
    MonotonicIdGenerator<GameplayObjectId> unit_ids_{0x2601};
    MonotonicIdGenerator<GameplayObjectId> residence_ids_{0x2602};
    MonotonicIdGenerator<GameplayObjectId> migration_ids_{0x2603};
    MonotonicIdGenerator<GameplayObjectId> allocation_ids_{0x2604};
    MonotonicIdGenerator<GameplayObjectId> allocation_correlation_ids_{0x2605};
    std::unordered_map<PopulationTemplateId, PopulationTemplate, IdHash> templates_;
    std::unordered_map<PopulationGroupId, PopulationGroup, IdHash> groups_;
    std::unordered_map<PopulationUnitId, PopulationUnit, IdHash> units_;
    std::unordered_map<PopulationResidenceId, PopulationResidence, IdHash> residences_;
    std::unordered_map<PopulationMigrationId, PopulationMigration, IdHash> migrations_;
    std::unordered_map<PopulationAllocationId, PopulationAllocation, IdHash> allocations_;
    std::unordered_map<GameplayObjectRef, PopulationUnitId> unit_by_entity_;
    std::unordered_map<PopulationUnitId, PopulationMigrationId, IdHash> active_migration_by_unit_;
    std::unordered_map<PopulationUnitId, PopulationResidenceId, IdHash> active_residence_by_unit_;
    std::unordered_map<PopulationUnitId, PopulationAllocationId, IdHash> active_allocation_by_unit_;
    std::unordered_map<GameplayObjectId, std::vector<PopulationAllocationId>> allocations_by_correlation_;
    std::unordered_map<PopulationGroupId, std::unordered_set<PopulationUnitId, IdHash>, IdHash> units_by_group_;
    std::unordered_map<GameplayObjectRef, std::unordered_set<PopulationUnitId, IdHash>> units_by_area_;
    std::unordered_map<PopulationTemplateId, std::unordered_set<PopulationUnitId, IdHash>, IdHash> units_by_template_;
    std::array<std::unordered_set<PopulationUnitId, IdHash>, 5> units_by_state_;
    std::deque<PopulationChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    std::size_t change_journal_capacity_ = 4096;
    bool definitions_frozen_ = false;
    mutable PopulationDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::population
