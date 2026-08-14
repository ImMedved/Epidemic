#include "Epidemic/GameFramework/World/world.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace epidemic::gameplay::world
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message) { return foundation::Error::Create(code, message); }
template<class T> bool Contains(const std::vector<T>& values, const T& value) { return std::find(values.begin(), values.end(), value) != values.end(); }
bool Overlaps(const WorldAabb& a, const WorldAabb& b) noexcept
{
    return a.min.x_mm <= b.max.x_mm && a.max.x_mm >= b.min.x_mm && a.min.y_mm <= b.max.y_mm &&
           a.max.y_mm >= b.min.y_mm && a.min.z_mm <= b.max.z_mm && a.max.z_mm >= b.min.z_mm;
}
std::int64_t FloorDiv(std::int64_t value, std::int64_t divisor) noexcept
{
    const auto q = value / divisor;
    const auto r = value % divisor;
    return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}
}

WorldTransaction::WorldTransaction(WorldService& owner, GameplayContext context) : owner_(&owner), context_(context) {}
foundation::Result<WorldAlterationId> WorldTransaction::Create(WorldAlterationRecord record)
{
    if (!owner_ || committed_ || cancelled_) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.transaction_closed", "world transaction is closed"));
    if (!record.id.IsValid()) record.id = WorldAlterationId{owner_->alteration_ids_.Next()};
    mutations_.push_back({Kind::Create, record, {}});
    return foundation::Result<WorldAlterationId>::Success(record.id);
}
foundation::Result<void> WorldTransaction::Update(WorldAlterationRecord record)
{
    if (!owner_ || committed_ || cancelled_ || !record.id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world update is invalid"));
    mutations_.push_back({Kind::Update, std::move(record), {}});
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldTransaction::Remove(WorldAlterationId id)
{
    if (!owner_ || committed_ || cancelled_ || !id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world remove is invalid"));
    mutations_.push_back({Kind::Remove, {}, id});
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldTransaction::Commit()
{
    if (!owner_ || committed_ || cancelled_) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_closed", "world transaction is closed"));
    auto result = owner_->CommitMutations(mutations_, context_);
    if (result) committed_ = true;
    return result;
}

WorldService::WorldService() : alteration_ids_(GameplayObjectId::FromString("framework.world.alteration.seed").High()) {}
WorldService::CellKey WorldService::CellFor(WorldPosition p) const noexcept { return {FloorDiv(p.x_mm, kSpatialCellMm), FloorDiv(p.y_mm, kSpatialCellMm), FloorDiv(p.z_mm, kSpatialCellMm)}; }
std::vector<WorldService::CellKey> WorldService::CellsFor(const WorldAabb& b) const
{
    const auto lo = CellFor(b.min), hi = CellFor(b.max);
    const auto nx = static_cast<std::uint64_t>(hi.x - lo.x + 1), ny = static_cast<std::uint64_t>(hi.y - lo.y + 1), nz = static_cast<std::uint64_t>(hi.z - lo.z + 1);
    if (nx == 0 || ny == 0 || nz == 0 || nx > kMaxIndexedCells || ny > kMaxIndexedCells || nz > kMaxIndexedCells || nx * ny > kMaxIndexedCells || nx * ny * nz > kMaxIndexedCells) return {};
    std::vector<CellKey> cells; cells.reserve(static_cast<std::size_t>(nx * ny * nz));
    for (auto x = lo.x; x <= hi.x; ++x) for (auto y = lo.y; y <= hi.y; ++y) for (auto z = lo.z; z <= hi.z; ++z) cells.push_back({x,y,z});
    return cells;
}
void WorldService::IndexArea(const WorldAreaDefinition& area)
{
    auto cells = CellsFor(area.bounds);
    if (cells.empty()) { large_areas_.push_back(area.id); return; }
    for (const auto& cell : cells) area_index_[cell].push_back(area.id);
}
void WorldService::IndexLocation(const LocationDefinition& location)
{
    for (auto area : location.areas) locations_by_area_[area].push_back(location.id);
}
void WorldService::RebuildAlterationIndex()
{
    alteration_index_.clear(); large_alterations_.clear();
    for (const auto& [id, alteration] : alterations_)
    {
        if (alteration.state != WorldAlterationState::Active) continue;
        auto cells = CellsFor(alteration.affected_area);
        if (cells.empty()) { large_alterations_.push_back(id); continue; }
        for (const auto& cell : cells) alteration_index_[cell].push_back(id);
    }
}

foundation::Result<void> WorldService::RegisterRegion(WorldRegionDefinition d)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty()) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_region", "invalid region"));
    if (regions_.contains(d.id)) return foundation::Result<void>::Failure(Error("gameplay.already_registered", "region already registered"));
    regions_.emplace(d.id, std::move(d)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RegisterArea(WorldAreaDefinition d)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || !d.bounds.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_area", "invalid area"));
    if (areas_.contains(d.id)) return foundation::Result<void>::Failure(Error("gameplay.already_registered", "area already registered"));
    const auto id = d.id; areas_.emplace(id, std::move(d)); IndexArea(areas_.at(id)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RegisterLocation(LocationDefinition d)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty()) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_location", "invalid location"));
    if (locations_.contains(d.id)) return foundation::Result<void>::Failure(Error("gameplay.already_registered", "location already registered"));
    const auto id = d.id; locations_.emplace(id, std::move(d)); IndexLocation(locations_.at(id)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RegisterFeatureType(WorldFeatureTypeId id, std::string name)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!id.IsValid() || name.empty() || feature_types_.contains(id)) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_feature_type", "invalid or duplicate feature type"));
    feature_types_.emplace(id, std::move(name)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RegisterAlterationType(WorldAlterationTypeId id, std::string name)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!id.IsValid() || name.empty() || alteration_types_.contains(id)) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration_type", "invalid or duplicate alteration type"));
    alteration_types_.emplace(id, std::move(name)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::AddStaticFeature(WorldFeatureRecord f)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!f.id.IsValid() || !f.type.IsValid() || !f.bounds.IsValid() || !feature_types_.contains(f.type) || features_.contains(f.id)) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_feature", "invalid feature"));
    f.dynamic = false; features_.emplace(f.id, std::move(f)); return foundation::Result<void>::Success();
}
const WorldRegionDefinition* WorldService::FindRegion(WorldRegionId id) const noexcept { auto i=regions_.find(id); return i==regions_.end()?nullptr:&i->second; }
const WorldAreaDefinition* WorldService::FindArea(WorldAreaId id) const noexcept { auto i=areas_.find(id); return i==areas_.end()?nullptr:&i->second; }
const LocationDefinition* WorldService::FindLocation(LocationId id) const noexcept { auto i=locations_.find(id); return i==locations_.end()?nullptr:&i->second; }
const WorldFeatureRecord* WorldService::FindFeature(WorldFeatureId id) const noexcept { auto i=features_.find(id); return i==features_.end()?nullptr:&i->second; }
const WorldAlterationRecord* WorldService::FindAlteration(WorldAlterationId id) const noexcept { auto i=alterations_.find(id); return i==alterations_.end()?nullptr:&i->second; }
std::vector<WorldAreaDefinition> WorldService::FindAreasAt(WorldPosition p) const
{
    std::vector<WorldAreaDefinition> out; std::unordered_set<WorldAreaId,IdHash> seen;
    const auto cell = CellFor(p); auto found = area_index_.find(cell);
    if (found != area_index_.end()) for (auto id : found->second) if (seen.insert(id).second) { const auto& area=areas_.at(id); if (area.bounds.Contains(p)) out.push_back(area); }
    for (auto id : large_areas_) if (seen.insert(id).second) { const auto& area=areas_.at(id); if (area.bounds.Contains(p)) out.push_back(area); }
    std::sort(out.begin(), out.end(), [](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<LocationDefinition> WorldService::FindLocationsInArea(WorldAreaId area) const
{
    std::vector<LocationDefinition> out; auto found=locations_by_area_.find(area); if(found!=locations_by_area_.end()) for(auto id:found->second) if(auto it=locations_.find(id);it!=locations_.end()) out.push_back(it->second);
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<WorldAlterationRecord> WorldService::FindAlterations(const WorldAabb& bounds, std::optional<WorldAlterationTypeId> type) const
{
    std::unordered_set<WorldAlterationId,IdHash> ids; auto cells=CellsFor(bounds);
    if(cells.empty()) { for(const auto&[id,a]:alterations_) if(a.state==WorldAlterationState::Active) ids.insert(id); }
    else { for(const auto&cell:cells) if(auto it=alteration_index_.find(cell);it!=alteration_index_.end()) ids.insert(it->second.begin(),it->second.end()); ids.insert(large_alterations_.begin(),large_alterations_.end()); }
    std::vector<WorldAlterationRecord> out; for(auto id:ids){const auto&a=alterations_.at(id);if(a.state==WorldAlterationState::Active&&Overlaps(a.affected_area,bounds)&&(!type||a.type==*type))out.push_back(a);}
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;});return out;
}
foundation::Result<WorldFeatureId> WorldService::AddDynamicFeature(WorldFeatureRecord f)
{
    if (!f.id.IsValid() || !f.type.IsValid() || !f.bounds.IsValid() || !feature_types_.contains(f.type) || features_.contains(f.id)) return foundation::Result<WorldFeatureId>::Failure(Error("gameplay.world.invalid_feature", "invalid dynamic feature"));
    f.dynamic=true;Bump();f.revision=revision_;features_.emplace(f.id,f);Record({0,WorldChangeKind::FeatureChanged,{},f.id,revision_,{}});return foundation::Result<WorldFeatureId>::Success(f.id);
}
foundation::Result<void> WorldService::CommitMutations(std::span<const WorldTransaction::Mutation> mutations, GameplayContext context)
{
    auto copy=alterations_;
    for(const auto&m:mutations){if(m.kind==WorldTransaction::Kind::Create){if(!m.record.id.IsValid()||!m.record.type.IsValid()||!m.record.affected_area.IsValid()||!alteration_types_.contains(m.record.type)||copy.contains(m.record.id))return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","invalid world alteration create"));copy.emplace(m.record.id,m.record);}else if(m.kind==WorldTransaction::Kind::Update){auto it=copy.find(m.record.id);if(it==copy.end()||!alteration_types_.contains(m.record.type))return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration update target missing"));it->second=m.record;}else{auto it=copy.find(m.id);if(it==copy.end())return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration remove target missing"));it->second.state=WorldAlterationState::Removed;}}
    for(const auto&m:mutations){Bump();if(m.kind==WorldTransaction::Kind::Create){auto r=m.record;r.revision=revision_;alterations_[r.id]=r;Record({0,WorldChangeKind::AlterationCreated,r.id,{},revision_,context});}else if(m.kind==WorldTransaction::Kind::Update){auto r=m.record;r.revision=revision_;alterations_[r.id]=r;Record({0,r.state==WorldAlterationState::Expired?WorldChangeKind::AlterationExpired:WorldChangeKind::AlterationUpdated,r.id,{},revision_,context});}else{auto&r=alterations_.at(m.id);r.state=WorldAlterationState::Removed;r.revision=revision_;Record({0,WorldChangeKind::AlterationRemoved,m.id,{},revision_,context});}}
    RebuildAlterationIndex();++transactions_;return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::ExpireAlteration(WorldAlterationId id, GameplayTimePoint now, GameplayContext context)
{
    const auto* current = FindAlteration(id);
    if (current == nullptr) return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing", "alteration expiration target missing"));
    if (current->state != WorldAlterationState::Active) return foundation::Result<void>::Success();
    if (!current->expires_at || current->expires_at->ticks > now.ticks) return foundation::Result<void>::Failure(Error("gameplay.world.alteration_not_due", "alteration is not due to expire"));
    auto updated = *current;
    updated.state = WorldAlterationState::Expired;
    auto tx = BeginTransaction(context);
    auto queued = tx.Update(std::move(updated));
    if (!queued) return queued;
    return tx.Commit();
}
foundation::Result<void> WorldService::SweepExpired(GameplayTimePoint now,GameplayContext context)
{
    auto tx=BeginTransaction(context);bool any=false;for(const auto&[id,a]:alterations_){(void)id;if(a.state==WorldAlterationState::Active&&a.expires_at&&a.expires_at->ticks<=now.ticks){auto r=a;r.state=WorldAlterationState::Expired;auto u=tx.Update(std::move(r));if(!u)return u;any=true;}}
    return any?tx.Commit():foundation::Result<void>::Success();
}
WorldSnapshot WorldService::CaptureSnapshot() const
{
    WorldSnapshot s;for(const auto&[id,f]:features_){(void)id;if(f.dynamic)s.dynamic_features.push_back(f);}for(const auto&[id,a]:alterations_){(void)id;if(a.persistence==WorldAlterationPersistence::Persistent)s.alterations.push_back(a);}std::sort(s.dynamic_features.begin(),s.dynamic_features.end(),[](auto&a,auto&b){return a.id<b.id;});std::sort(s.alterations.begin(),s.alterations.end(),[](auto&a,auto&b){return a.id<b.id;});s.alteration_ids=alteration_ids_.GetSnapshot();s.revision=revision_;return s;
}
foundation::Result<void> WorldService::RestoreSnapshot(WorldSnapshot s)
{
    for (auto it = features_.begin(); it != features_.end();)
    {
        if (it->second.dynamic) it = features_.erase(it);
        else ++it;
    }
    alterations_.clear();
    for(auto&f:s.dynamic_features){if(!f.id.IsValid()||!f.type.IsValid()||!feature_types_.contains(f.type))return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid feature in snapshot"));features_.emplace(f.id,std::move(f));}
    for(auto&a:s.alterations){if(!a.id.IsValid()||!alteration_types_.contains(a.type))return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid alteration in snapshot"));alterations_.emplace(a.id,std::move(a));}
    alteration_ids_.Restore(s.alteration_ids);revision_=s.revision;changes_.clear();next_change_sequence_=1;RebuildAlterationIndex();return foundation::Result<void>::Success();
}
std::vector<WorldChange> WorldService::ChangesSince(std::uint64_t sequence) const { std::vector<WorldChange> out;std::copy_if(changes_.begin(),changes_.end(),std::back_inserter(out),[sequence](const auto&c){return c.sequence>sequence;});return out; }
void WorldService::Record(WorldChange c){c.sequence=next_change_sequence_++;changes_.push_back(std::move(c));}
WorldDiagnostics WorldService::GetDiagnostics() const noexcept
{
    WorldDiagnostics d;d.regions=regions_.size();d.areas=areas_.size();d.locations=locations_.size();d.features=features_.size();d.transactions=transactions_;for(const auto&[id,a]:alterations_){(void)id;if(a.state==WorldAlterationState::Active)++d.active_alterations;if(a.persistence==WorldAlterationPersistence::Persistent)++d.persistent_alterations;}return d;
}
} // namespace epidemic::gameplay::world
