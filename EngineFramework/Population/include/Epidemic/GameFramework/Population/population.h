#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::population
{
struct PopulationGroupId{GameplayObjectId value{}; static constexpr PopulationGroupId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PopulationGroupId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationGroupId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationGroupId&) const noexcept=default;};
struct PopulationUnitId{GameplayObjectId value{}; static constexpr PopulationUnitId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PopulationUnitId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationUnitId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationUnitId&) const noexcept=default;};
struct PopulationProfileId{TypeId value{}; static constexpr PopulationProfileId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationProfileId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationProfileId&) const noexcept=default;};
struct PopulationTemplateId{TypeId value{}; static constexpr PopulationTemplateId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationTemplateId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationTemplateId&) const noexcept=default;};
struct PopulationResidenceId{GameplayObjectId value{}; static constexpr PopulationResidenceId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PopulationResidenceId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationResidenceId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationResidenceId&) const noexcept=default;};
struct PopulationMigrationId{GameplayObjectId value{}; static constexpr PopulationMigrationId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PopulationMigrationId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PopulationMigrationId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PopulationMigrationId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};

enum class PopulationGroupState{Active,Dormant,Depleted,Removed};
enum class PopulationUnitState{Latent,Abstract,Materializing,Materialized,Dematerializing,Dead,Removed,Migrated};
enum class ResidenceState{Assigned,Unavailable,Abandoned,Destroyed};
enum class MigrationState{Planned,Active,Completed,Cancelled,Failed};
enum class PopulationChangeKind{GroupCreated,GroupChanged,UnitCreated,UnitMaterialized,UnitDematerialized,UnitDied,UnitMigrated,UnitRetired,ResidenceAssigned,MigrationStarted,MigrationCompleted};

struct PopulationTemplate{PopulationTemplateId id{};TypeId entity_archetype{};GameplayTagSet tags{};std::vector<std::byte> generation_payload;bool persistent=true;Revision revision{};};
struct PopulationGroup{PopulationGroupId id{};GameplayObjectRef area{};GameplayObjectRef society_group{};PopulationProfileId profile{};PopulationGroupState state=PopulationGroupState::Active;std::uint32_t desired_count=0;std::uint32_t current_known_count=0;std::uint32_t materialized_count=0;Revision revision{};};
struct PopulationUnit{PopulationUnitId id{};PopulationGroupId group{};std::optional<GameplayObjectRef> entity;PopulationTemplateId template_id{};PopulationUnitState state=PopulationUnitState::Latent;GameplayObjectRef home_area{};GameplayObjectRef current_area{};GameplayTimePoint created_at{};GameplayTimePoint last_simulated_at{};GameplayTagSet tags{};std::vector<std::byte> payload;Revision revision{};};
struct PopulationResidence{PopulationResidenceId id{};PopulationUnitId unit{};GameplayObjectRef home_area{};GameplayObjectRef home_property{};ResidenceState state=ResidenceState::Assigned;Revision revision{};};
struct PopulationMigration{PopulationMigrationId id{};PopulationUnitId unit{};GameplayObjectRef from{};GameplayObjectRef to{};TypeId reason{};MigrationState state=MigrationState::Planned;GameplayTimePoint started_at{};Revision revision{};};
struct PopulationChange{std::uint64_t sequence=0;PopulationChangeKind kind=PopulationChangeKind::GroupCreated;PopulationGroupId group{};PopulationUnitId unit{};GameplayObjectRef entity{};GameplayObjectRef area{};GameplayContext context{};Revision revision{};};
struct PopulationCounts{std::uint32_t latent=0,abstract_units=0,materializing=0,materialized=0,dematerializing=0,dead=0,removed=0,migrated=0;};
struct PopulationSnapshot{std::vector<PopulationTemplate> templates;std::vector<PopulationGroup> groups;std::vector<PopulationUnit> units;std::vector<PopulationResidence> residences;std::vector<PopulationMigration> migrations;MonotonicIdGenerator<GameplayObjectId>::Snapshot group_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot unit_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot residence_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot migration_ids{};Revision revision{};};
struct PopulationDiagnostics{std::uint64_t groups=0,units=0,latent=0,abstract_units=0,materialized=0,migrations=0,residences=0,materialization_requests=0,dematerialization_requests=0;};

class PopulationService
{
public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.population");}
    [[nodiscard]] foundation::Result<void> RegisterTemplate(PopulationTemplate definition);
    [[nodiscard]] foundation::Result<PopulationGroupId> CreateGroup(PopulationGroup group);
    [[nodiscard]] foundation::Result<PopulationUnitId> CreateUnit(PopulationUnit unit,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SetUnitEntity(PopulationUnitId unit,GameplayObjectRef entity,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> MaterializeUnit(PopulationUnitId unit,GameplayObjectRef entity,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> DematerializeUnit(PopulationUnitId unit,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> MarkUnitDead(PopulationUnitId unit,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> RetireUnit(PopulationUnitId unit,GameplayContext context={});
    [[nodiscard]] foundation::Result<PopulationResidenceId> AssignResidence(PopulationResidence residence,GameplayContext context={});
    [[nodiscard]] foundation::Result<PopulationMigrationId> StartMigration(PopulationMigration migration,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> CompleteMigration(PopulationMigrationId migration,GameplayContext context={});
    [[nodiscard]] const PopulationGroup* GetGroup(PopulationGroupId id) const noexcept;
    [[nodiscard]] const PopulationUnit* GetUnit(PopulationUnitId id) const noexcept;
    [[nodiscard]] std::vector<PopulationGroup> FindGroupsInArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PopulationUnit> FindUnitsByGroup(PopulationGroupId group) const;
    [[nodiscard]] std::vector<PopulationUnit> FindUnitsByState(PopulationUnitState state) const;
    [[nodiscard]] std::vector<PopulationUnit> FindResidentsOfArea(GameplayObjectRef area) const;
    [[nodiscard]] std::vector<PopulationUnit> FindMaterializationCandidates(GameplayObjectRef area,std::size_t limit) const;
    [[nodiscard]] PopulationCounts GetPopulationCounts(PopulationGroupId group={}) const;
    [[nodiscard]] std::vector<PopulationChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] PopulationSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(PopulationSnapshot snapshot);
    [[nodiscard]] PopulationDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(PopulationChange change);
    void RecountGroup(PopulationGroupId group);
    [[nodiscard]] PopulationUnit* FindMutableUnit(PopulationUnitId id) noexcept;
    [[nodiscard]] PopulationGroup* FindMutableGroup(PopulationGroupId id) noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> group_ids_{0x2600};
    MonotonicIdGenerator<GameplayObjectId> unit_ids_{0x2601};
    MonotonicIdGenerator<GameplayObjectId> residence_ids_{0x2602};
    MonotonicIdGenerator<GameplayObjectId> migration_ids_{0x2603};
    std::unordered_map<PopulationTemplateId,PopulationTemplate,IdHash> templates_;
    std::unordered_map<PopulationGroupId,PopulationGroup,IdHash> groups_;
    std::unordered_map<PopulationUnitId,PopulationUnit,IdHash> units_;
    std::unordered_map<PopulationResidenceId,PopulationResidence,IdHash> residences_;
    std::unordered_map<PopulationMigrationId,PopulationMigration,IdHash> migrations_;
    std::vector<PopulationChange> changes_;
    std::uint64_t next_change_sequence_=1;
    mutable PopulationDiagnostics diagnostics_{};
};
}
