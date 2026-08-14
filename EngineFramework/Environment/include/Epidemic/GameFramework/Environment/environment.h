#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::environment
{
struct EnvironmentLayerId { GameplayObjectId value{}; static constexpr EnvironmentLayerId FromRaw(std::uint64_t h,std::uint64_t l) noexcept { return {GameplayObjectId::FromRaw(h,l)}; } [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const EnvironmentLayerId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const EnvironmentLayerId&)const noexcept=default; };
struct EnvironmentLayerTypeId { TypeId value{}; static constexpr EnvironmentLayerTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const EnvironmentLayerTypeId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const EnvironmentLayerTypeId&)const noexcept=default; };
struct EnvironmentBlendHandlerId { TypeId value{}; static constexpr EnvironmentBlendHandlerId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const EnvironmentBlendHandlerId&)const noexcept=default; };
struct EnvironmentPosition { std::int64_t x_mm=0,y_mm=0,z_mm=0; [[nodiscard]] constexpr bool operator==(const EnvironmentPosition&)const noexcept=default; };
struct EnvironmentAabb { EnvironmentPosition min{},max{}; [[nodiscard]] constexpr bool Contains(EnvironmentPosition p)const noexcept{return p.x_mm>=min.x_mm&&p.x_mm<=max.x_mm&&p.y_mm>=min.y_mm&&p.y_mm<=max.y_mm&&p.z_mm>=min.z_mm&&p.z_mm<=max.z_mm;} [[nodiscard]] constexpr bool IsValid()const noexcept{return min.x_mm<=max.x_mm&&min.y_mm<=max.y_mm&&min.z_mm<=max.z_mm;} [[nodiscard]] constexpr bool operator==(const EnvironmentAabb&)const noexcept=default; };
using EnvironmentFixed = std::int32_t; // 1000 == 1.0; temperature uses milli-degrees C.
constexpr EnvironmentFixed kEnvironmentOne=1000;

enum class EnvironmentBlendPolicy { Override, Add, Multiply, Min, Max, CustomRegistered };
enum class EnvironmentPersistence { Transient, Session, Persistent };
struct EnvironmentValues
{
    EnvironmentFixed temperature_milli_c=0;
    EnvironmentFixed humidity=0;
    EnvironmentFixed precipitation=0;
    EnvironmentFixed wind_strength=0;
    EnvironmentFixed wind_x=0, wind_y=0, wind_z=0;
    EnvironmentFixed visibility=kEnvironmentOne;
    [[nodiscard]] constexpr bool operator==(const EnvironmentValues&) const noexcept = default;
};
struct EnvironmentLayer
{
    EnvironmentLayerId id{};
    EnvironmentLayerTypeId type{};
    std::optional<EnvironmentAabb> bounds{};
    GameplayObjectRef area_ref{};
    int priority=0;
    EnvironmentBlendPolicy blend=EnvironmentBlendPolicy::Override;
    EnvironmentBlendHandlerId custom_blend{};
    EnvironmentValues values{};
    GameplayTagSet tags;
    GameplayTimePoint created_at{};
    std::optional<GameplayTimePoint> expires_at{};
    EnvironmentPersistence persistence=EnvironmentPersistence::Session;
    Revision revision{};
    GameplayContext context{};
};
struct EnvironmentSample { EnvironmentPosition position{}; GameplayTimePoint time{}; EnvironmentValues values{}; GameplayTagSet tags; Revision revision{}; std::vector<EnvironmentLayerId> layers; };
enum class EnvironmentChangeKind { Added, Changed, Removed, Expired };
struct EnvironmentChange { std::uint64_t sequence=0; EnvironmentChangeKind kind=EnvironmentChangeKind::Added; EnvironmentLayerId layer{}; Revision revision{}; GameplayContext context{}; };
struct EnvironmentSnapshot { std::vector<EnvironmentLayer> layers; MonotonicIdGenerator<GameplayObjectId>::Snapshot ids{}; Revision revision{}; };
struct EnvironmentDiagnostics { std::uint64_t layers=0,persistent_layers=0,samples=0,expired=0,changes=0; };

class EnvironmentService
{
public:
    EnvironmentService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept { return GameplayDomainId::FromString("framework.environment"); }
    using BlendHandler = std::function<EnvironmentValues(const EnvironmentValues&, const EnvironmentValues&)>;
    [[nodiscard]] foundation::Result<void> RegisterLayerType(EnvironmentLayerTypeId id,std::string canonical_name);
    [[nodiscard]] foundation::Result<void> RegisterBlendHandler(EnvironmentBlendHandlerId id,std::string canonical_name,BlendHandler handler);
    void Freeze() noexcept { frozen_=true; }
    [[nodiscard]] bool IsFrozen()const noexcept{return frozen_;}
    [[nodiscard]] foundation::Result<EnvironmentLayerId> AddLayer(EnvironmentLayer layer);
    [[nodiscard]] foundation::Result<void> UpdateLayer(EnvironmentLayer layer);
    [[nodiscard]] foundation::Result<void> RemoveLayer(EnvironmentLayerId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> ExpireLayer(EnvironmentLayerId id,GameplayTimePoint now,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> SweepExpired(GameplayTimePoint now,GameplayContext context={});
    [[nodiscard]] const EnvironmentLayer* FindLayer(EnvironmentLayerId id)const noexcept;
    [[nodiscard]] std::vector<EnvironmentLayer> FindLayers(EnvironmentPosition position) const;
    [[nodiscard]] EnvironmentSample Sample(EnvironmentPosition position,GameplayTimePoint time) const;
    [[nodiscard]] EnvironmentSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EnvironmentSnapshot snapshot);
    [[nodiscard]] std::vector<EnvironmentChange> ChangesSince(std::uint64_t sequence)const;
    [[nodiscard]] std::uint64_t LatestChangeSequence()const noexcept{return next_change_sequence_-1;}
    [[nodiscard]] Revision CurrentRevision()const noexcept{return revision_;}
    [[nodiscard]] EnvironmentDiagnostics GetDiagnostics()const noexcept;
private:
    struct CellKey { std::int64_t x=0,y=0,z=0; [[nodiscard]] bool operator==(const CellKey&) const noexcept=default; };
    struct CellHash { std::size_t operator()(const CellKey& c) const noexcept { const auto a=std::hash<std::int64_t>{}(c.x); const auto b=std::hash<std::int64_t>{}(c.y); const auto d=std::hash<std::int64_t>{}(c.z); return a^(b+0x9e3779b97f4a7c15ull+(a<<6u)+(a>>2u))^(d<<1u); } };
    static constexpr std::int64_t kSpatialCellMm=100000;
    static constexpr std::uint64_t kMaxIndexedCells=4096;
    [[nodiscard]] CellKey CellFor(EnvironmentPosition p) const noexcept;
    [[nodiscard]] std::vector<CellKey> CellsFor(const EnvironmentAabb& bounds) const;
    void RebuildSpatialIndex();
    struct IdHash { std::size_t operator()(EnvironmentLayerId id)const noexcept{return std::hash<GameplayObjectId>{}(id.value);} };
    struct TypeHash { std::size_t operator()(EnvironmentLayerTypeId id)const noexcept{return std::hash<TypeId>{}(id.value);} };
    void Blend(EnvironmentValues& base,const EnvironmentLayer& layer) const;
    void Record(EnvironmentChange change);
    void Bump() noexcept { ++revision_.value; }
    struct BlendHash { std::size_t operator()(EnvironmentBlendHandlerId id)const noexcept{return std::hash<TypeId>{}(id.value);} };
    std::unordered_map<EnvironmentLayerTypeId,std::string,TypeHash> types_;
    std::unordered_map<EnvironmentBlendHandlerId,std::pair<std::string,BlendHandler>,BlendHash> blend_handlers_;
    std::unordered_map<EnvironmentLayerId,EnvironmentLayer,IdHash> layers_;
    std::unordered_map<CellKey,std::vector<EnvironmentLayerId>,CellHash> spatial_index_;
    std::vector<EnvironmentLayerId> global_layers_;
    std::vector<EnvironmentLayerId> large_layers_;
    MonotonicIdGenerator<GameplayObjectId> ids_;
    Revision revision_{}; bool frozen_=false; mutable std::uint64_t samples_=0; std::uint64_t expired_=0;
    std::vector<EnvironmentChange> changes_; std::uint64_t next_change_sequence_=1;
};
} // namespace epidemic::gameplay::environment
