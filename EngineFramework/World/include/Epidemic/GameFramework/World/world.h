#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::world
{
struct WorldRegionId { GameplayObjectId value{}; static constexpr WorldRegionId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldRegionId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldRegionId&) const noexcept = default; };
struct WorldAreaId { GameplayObjectId value{}; static constexpr WorldAreaId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldAreaId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldAreaId&) const noexcept = default; };
struct LocationId { GameplayObjectId value{}; static constexpr LocationId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const LocationId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const LocationId&) const noexcept = default; };
struct WorldFeatureId { GameplayObjectId value{}; static constexpr WorldFeatureId FromString(std::string_view s) noexcept { return {GameplayObjectId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldFeatureId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldFeatureId&) const noexcept = default; };
struct WorldAlterationId { GameplayObjectId value{}; static constexpr WorldAlterationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldAlterationId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldAlterationId&) const noexcept = default; };
struct WorldAlterationTypeId { TypeId value{}; static constexpr WorldAlterationTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldAlterationTypeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldAlterationTypeId&) const noexcept = default; };
struct WorldFeatureTypeId { TypeId value{}; static constexpr WorldFeatureTypeId FromString(std::string_view s) noexcept { return {TypeId::FromString(s)}; } [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); } [[nodiscard]] constexpr bool operator==(const WorldFeatureTypeId&) const noexcept = default; [[nodiscard]] constexpr auto operator<=>(const WorldFeatureTypeId&) const noexcept = default; };

struct WorldPosition { std::int64_t x_mm=0, y_mm=0, z_mm=0; [[nodiscard]] constexpr bool operator==(const WorldPosition&) const noexcept = default; };
struct WorldAabb { WorldPosition min{}; WorldPosition max{}; [[nodiscard]] constexpr bool Contains(WorldPosition p) const noexcept { return p.x_mm>=min.x_mm&&p.x_mm<=max.x_mm&&p.y_mm>=min.y_mm&&p.y_mm<=max.y_mm&&p.z_mm>=min.z_mm&&p.z_mm<=max.z_mm; } [[nodiscard]] constexpr bool IsValid() const noexcept { return min.x_mm<=max.x_mm&&min.y_mm<=max.y_mm&&min.z_mm<=max.z_mm; } [[nodiscard]] constexpr bool operator==(const WorldAabb&) const noexcept = default; };

struct WorldRegionDefinition { WorldRegionId id{}; std::string canonical_name; GameplayTagSet tags; Revision revision{}; };
struct WorldAreaDefinition { WorldAreaId id{}; std::string canonical_name; WorldAabb bounds{}; std::vector<WorldRegionId> regions; std::vector<WorldAreaId> adjacent; GameplayTagSet tags; Revision revision{}; };
struct LocationDefinition { LocationId id{}; std::string canonical_name; WorldPosition position{}; std::vector<WorldRegionId> regions; std::vector<WorldAreaId> areas; GameplayTagSet tags; Revision revision{}; };
struct WorldFeatureRecord { WorldFeatureId id{}; WorldFeatureTypeId type{}; WorldAabb bounds{}; GameplayTagSet tags; Revision revision{}; bool dynamic=false; };

enum class WorldAlterationState { Active, Superseded, Expired, Removed, Compacted };
enum class WorldAlterationPersistence { Transient, Session, Persistent };
struct WorldAlterationRecord
{
    WorldAlterationId id{};
    WorldAlterationTypeId type{};
    GameplayObjectRef subject{};
    WorldAabb affected_area{};
    GameplayContext context{};
    GameplayTimePoint created_at{};
    WorldAlterationPersistence persistence=WorldAlterationPersistence::Persistent;
    std::optional<GameplayTimePoint> expires_at{};
    WorldAlterationState state=WorldAlterationState::Active;
    std::vector<std::byte> payload;
    Revision revision{};
};

struct WorldSnapshot
{
    std::vector<WorldFeatureRecord> dynamic_features;
    std::vector<WorldAlterationRecord> alterations;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot alteration_ids{};
    Revision revision{};
};

enum class WorldChangeKind { AlterationCreated, AlterationUpdated, AlterationExpired, AlterationRemoved, FeatureChanged };
struct WorldChange { std::uint64_t sequence=0; WorldChangeKind kind=WorldChangeKind::AlterationCreated; WorldAlterationId alteration{}; WorldFeatureId feature{}; Revision revision{}; GameplayContext context{}; };
struct WorldDiagnostics { std::uint64_t regions=0, areas=0, locations=0, features=0, active_alterations=0, persistent_alterations=0, transactions=0; };

class WorldService;
class WorldTransaction
{
public:
    WorldTransaction(WorldService& owner, GameplayContext context);
    [[nodiscard]] foundation::Result<WorldAlterationId> Create(WorldAlterationRecord record);
    [[nodiscard]] foundation::Result<void> Update(WorldAlterationRecord record);
    [[nodiscard]] foundation::Result<void> Remove(WorldAlterationId id);
    [[nodiscard]] foundation::Result<void> Commit();
    void Cancel() noexcept { cancelled_=true; }
public:
    enum class Kind { Create, Update, Remove };
    struct Mutation { Kind kind; WorldAlterationRecord record{}; WorldAlterationId id{}; };
private:
    WorldService* owner_{}; GameplayContext context_{}; std::vector<Mutation> mutations_; bool committed_=false, cancelled_=false;
};

class WorldService
{
public:
    WorldService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.world"); }
    [[nodiscard]] foundation::Result<void> RegisterRegion(WorldRegionDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterArea(WorldAreaDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterLocation(LocationDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterFeatureType(WorldFeatureTypeId id, std::string canonical_name);
    [[nodiscard]] foundation::Result<void> RegisterAlterationType(WorldAlterationTypeId id, std::string canonical_name);
    [[nodiscard]] foundation::Result<void> AddStaticFeature(WorldFeatureRecord feature);
    void Freeze() noexcept { frozen_=true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    [[nodiscard]] const WorldRegionDefinition* FindRegion(WorldRegionId id) const noexcept;
    [[nodiscard]] const WorldAreaDefinition* FindArea(WorldAreaId id) const noexcept;
    [[nodiscard]] const LocationDefinition* FindLocation(LocationId id) const noexcept;
    [[nodiscard]] const WorldFeatureRecord* FindFeature(WorldFeatureId id) const noexcept;
    [[nodiscard]] const WorldAlterationRecord* FindAlteration(WorldAlterationId id) const noexcept;
    [[nodiscard]] bool IsAlterationTypeRegistered(WorldAlterationTypeId id) const noexcept { return alteration_types_.contains(id); }

    [[nodiscard]] std::vector<WorldAreaDefinition> FindAreasAt(WorldPosition position) const;
    [[nodiscard]] std::vector<LocationDefinition> FindLocationsInArea(WorldAreaId area) const;
    [[nodiscard]] std::vector<WorldAlterationRecord> FindAlterations(const WorldAabb& bounds, std::optional<WorldAlterationTypeId> type=std::nullopt) const;

    [[nodiscard]] foundation::Result<WorldFeatureId> AddDynamicFeature(WorldFeatureRecord feature);
    [[nodiscard]] WorldTransaction BeginTransaction(GameplayContext context={}) { return WorldTransaction(*this, context); }
    [[nodiscard]] foundation::Result<void> ExpireAlteration(WorldAlterationId id, GameplayTimePoint now, GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SweepExpired(GameplayTimePoint now, GameplayContext context={});

    [[nodiscard]] WorldSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(WorldSnapshot snapshot);
    [[nodiscard]] std::vector<WorldChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] std::uint64_t LatestChangeSequence() const noexcept { return next_change_sequence_-1; }
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }
    [[nodiscard]] WorldDiagnostics GetDiagnostics() const noexcept;

private:
    friend class WorldTransaction;
    [[nodiscard]] foundation::Result<void> CommitMutations(std::span<const WorldTransaction::Mutation> mutations, GameplayContext context);
    void Record(WorldChange change);
    void Bump() noexcept { ++revision_.value; }

    struct CellKey { std::int64_t x=0,y=0,z=0; [[nodiscard]] bool operator==(const CellKey&) const noexcept=default; };
    struct CellHash { std::size_t operator()(const CellKey& c) const noexcept { const auto a=std::hash<std::int64_t>{}(c.x); const auto b=std::hash<std::int64_t>{}(c.y); const auto d=std::hash<std::int64_t>{}(c.z); return a^(b+0x9e3779b97f4a7c15ull+(a<<6u)+(a>>2u))^(d<<1u); } };
    static constexpr std::int64_t kSpatialCellMm=100000;
    static constexpr std::uint64_t kMaxIndexedCells=4096;
    void IndexArea(const WorldAreaDefinition& area);
    void IndexLocation(const LocationDefinition& location);
    void RebuildAlterationIndex();
    [[nodiscard]] std::vector<CellKey> CellsFor(const WorldAabb& bounds) const;
    [[nodiscard]] CellKey CellFor(WorldPosition position) const noexcept;
    struct IdHash { template<class T> std::size_t operator()(const T& id) const noexcept { return std::hash<GameplayObjectId>{}(id.value); } };
    struct TypeHash { template<class T> std::size_t operator()(const T& id) const noexcept { return std::hash<TypeId>{}(id.value); } };
    std::unordered_map<WorldRegionId,WorldRegionDefinition,IdHash> regions_;
    std::unordered_map<WorldAreaId,WorldAreaDefinition,IdHash> areas_;
    std::unordered_map<LocationId,LocationDefinition,IdHash> locations_;
    std::unordered_map<WorldFeatureId,WorldFeatureRecord,IdHash> features_;
    std::unordered_map<WorldFeatureTypeId,std::string,TypeHash> feature_types_;
    std::unordered_map<WorldAlterationTypeId,std::string,TypeHash> alteration_types_;
    std::unordered_map<WorldAlterationId,WorldAlterationRecord,IdHash> alterations_;
    std::unordered_map<CellKey,std::vector<WorldAreaId>,CellHash> area_index_;
    std::unordered_map<WorldAreaId,std::vector<LocationId>,IdHash> locations_by_area_;
    std::unordered_map<CellKey,std::vector<WorldAlterationId>,CellHash> alteration_index_;
    std::vector<WorldAreaId> large_areas_;
    std::vector<WorldAlterationId> large_alterations_;
    MonotonicIdGenerator<GameplayObjectId> alteration_ids_;
    Revision revision_{}; bool frozen_=false;
    std::vector<WorldChange> changes_; std::uint64_t next_change_sequence_=1; std::uint64_t transactions_=0;
};
} // namespace epidemic::gameplay::world
