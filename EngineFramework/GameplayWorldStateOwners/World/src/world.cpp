#include "Epidemic/GameFramework/World/world.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::world
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message) { return foundation::Error::Create(code, message); }
template <class T> bool Contains(const std::vector<T>& values, const T& value) { return std::find(values.begin(), values.end(), value) != values.end(); }
template <class T> void Canonicalize(std::vector<T>& values) { std::sort(values.begin(), values.end()); values.erase(std::unique(values.begin(), values.end()), values.end()); }
bool Overlaps(const WorldAabb& a, const WorldAabb& b) noexcept
{
    return a.min.x_mm <= b.max.x_mm && a.max.x_mm >= b.min.x_mm && a.min.y_mm <= b.max.y_mm && a.max.y_mm >= b.min.y_mm &&
           a.min.z_mm <= b.max.z_mm && a.max.z_mm >= b.min.z_mm;
}
std::int64_t FloorDiv(std::int64_t value, std::int64_t divisor) noexcept
{
    const auto q = value / divisor; const auto r = value % divisor; return (r != 0 && ((r < 0) != (divisor < 0))) ? q - 1 : q;
}
} // namespace

WorldTransaction::WorldTransaction(WorldService& owner, GameplayContext context) : owner_(&owner), context_(context) {}
foundation::Result<WorldAlterationId> WorldTransaction::Create(WorldAlterationRecord record)
{
    if (!owner_ || committed_ || cancelled_) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.transaction_closed", "world transaction is closed"));
    if (!record.id.IsValid()) record.id = WorldAlterationId{owner_->alteration_ids_.Next()};
    if (!record.id.IsValid()) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.alteration_id_exhausted", "world alteration id generator is exhausted"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.kind == Kind::Remove ? mutation.id : mutation.record.id;
        if (existing == record.id) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    mutations_.push_back({Kind::Create, std::move(record), {}});
    return foundation::Result<WorldAlterationId>::Success(mutations_.back().record.id);
}
foundation::Result<void> WorldTransaction::Update(WorldAlterationRecord record)
{
    if (!owner_ || committed_ || cancelled_ || !record.id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world update is invalid"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.kind == Kind::Remove ? mutation.id : mutation.record.id;
        if (existing == record.id) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    mutations_.push_back({Kind::Update, std::move(record), {}}); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldTransaction::Remove(WorldAlterationId id)
{
    if (!owner_ || committed_ || cancelled_ || !id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world remove is invalid"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.kind == Kind::Remove ? mutation.id : mutation.record.id;
        if (existing == id) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    mutations_.push_back({Kind::Remove, {}, id}); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldTransaction::Commit()
{
    if (!owner_ || committed_ || cancelled_) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_closed", "world transaction is closed"));
    auto result = owner_->CommitMutations(mutations_, context_); if (result) committed_ = true; return result;
}

WorldService::WorldService() : alteration_ids_(GameplayObjectId::FromString("framework.world.alteration.seed").High()) {}
WorldService::CellKey WorldService::CellFor(WorldPosition p) const noexcept
{
    return {FloorDiv(p.x_mm, kSpatialCellMm), FloorDiv(p.y_mm, kSpatialCellMm), FloorDiv(p.z_mm, kSpatialCellMm)};
}
std::vector<WorldService::CellKey> WorldService::CellsFor(const WorldAabb& b) const
{
    const auto lo = CellFor(b.min), hi = CellFor(b.max);
    if (hi.x < lo.x || hi.y < lo.y || hi.z < lo.z) return {};
    const auto nx = static_cast<std::uint64_t>(hi.x - lo.x + 1), ny = static_cast<std::uint64_t>(hi.y - lo.y + 1), nz = static_cast<std::uint64_t>(hi.z - lo.z + 1);
    if (nx == 0 || ny == 0 || nz == 0 || nx > kMaxIndexedCells || ny > kMaxIndexedCells || nz > kMaxIndexedCells ||
        nx > kMaxIndexedCells / ny || nx * ny > kMaxIndexedCells / nz) return {};
    std::vector<CellKey> cells; cells.reserve(static_cast<std::size_t>(nx * ny * nz));
    for (auto x = lo.x; x <= hi.x; ++x) for (auto y = lo.y; y <= hi.y; ++y) for (auto z = lo.z; z <= hi.z; ++z) cells.push_back({x, y, z});
    return cells;
}
void WorldService::IndexArea(const WorldAreaDefinition& area)
{
    auto cells = CellsFor(area.bounds); if (cells.empty()) { large_areas_.push_back(area.id); return; }
    for (const auto& cell : cells) area_index_[cell].push_back(area.id);
}
void WorldService::IndexLocation(const LocationDefinition& location)
{
    for (auto area : location.areas) locations_by_area_[area].push_back(location.id);
    if (location.parent_location) child_locations_[*location.parent_location].push_back(location.id);
}
void WorldService::RebuildTopologyIndexes()
{
    area_index_.clear(); locations_by_area_.clear(); child_locations_.clear(); large_areas_.clear();
    std::vector<WorldAreaId> area_ids; for (const auto& [id, _] : areas_) area_ids.push_back(id); std::sort(area_ids.begin(), area_ids.end());
    for (const auto id : area_ids) IndexArea(areas_.at(id));
    std::vector<LocationId> location_ids; for (const auto& [id, _] : locations_) location_ids.push_back(id); std::sort(location_ids.begin(), location_ids.end());
    for (const auto id : location_ids) IndexLocation(locations_.at(id));
    for (auto& [_, values] : locations_by_area_) Canonicalize(values);
    for (auto& [_, values] : child_locations_) Canonicalize(values);
}
void WorldService::RebuildAlterationIndex()
{
    alteration_index_.clear(); large_alterations_.clear();
    std::vector<WorldAlterationId> ids; for (const auto& [id, _] : alterations_) ids.push_back(id); std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
    {
        const auto& alteration = alterations_.at(id); if (alteration.state != WorldAlterationState::Active) continue;
        auto cells = CellsFor(alteration.affected_area); if (cells.empty()) { large_alterations_.push_back(id); continue; }
        for (const auto& cell : cells) alteration_index_[cell].push_back(id);
    }
}

foundation::Result<Revision> WorldService::PrepareRevision() const
{
    const auto next = CheckedNext(revision_); if (!next) return foundation::Result<Revision>::Failure(Error("gameplay.revision_exhausted", "world revision counter is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}
bool WorldService::CanRecordChanges(std::size_t count) const noexcept
{
    if (count == 0) return true; if (next_change_sequence_ == 0) return false;
    return count - 1 <= std::numeric_limits<std::uint64_t>::max() - next_change_sequence_;
}
void WorldService::Record(WorldChange c) noexcept
{
    c.sequence = next_change_sequence_; last_change_sequence_ = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max()) next_change_sequence_ = 0; else ++next_change_sequence_;
    changes_.push_back(std::move(c));
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
    Canonicalize(d.regions); Canonicalize(d.adjacent); areas_.emplace(d.id, std::move(d)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RegisterLocation(LocationDefinition d)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "world registry is frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty()) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_location", "invalid location"));
    if (locations_.contains(d.id)) return foundation::Result<void>::Failure(Error("gameplay.already_registered", "location already registered"));
    Canonicalize(d.regions); Canonicalize(d.areas); locations_.emplace(d.id, std::move(d)); return foundation::Result<void>::Success();
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
    if (!f.id.IsValid() || !f.type.IsValid() || !f.bounds.IsValid() || !feature_types_.contains(f.type) || features_.contains(f.id))
        return foundation::Result<void>::Failure(Error("gameplay.world.invalid_feature", "invalid feature"));
    f.dynamic = false; features_.emplace(f.id, std::move(f)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::ValidateTopology() const
{
    for (const auto& [id, area] : areas_)
    {
        for (const auto region : area.regions) if (!regions_.contains(region)) return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "area references an unknown region"));
        for (const auto adjacent : area.adjacent)
        {
            if (adjacent == id || !areas_.contains(adjacent)) return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "area adjacency is invalid"));
            const auto& other = areas_.at(adjacent); if (!Contains(other.adjacent, id)) return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "area adjacency must be symmetric"));
        }
    }
    for (const auto& [id, location] : locations_)
    {
        for (const auto region : location.regions) if (!regions_.contains(region)) return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "location references an unknown region"));
        for (const auto area : location.areas) if (!areas_.contains(area)) return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "location references an unknown area"));
        if (location.parent_location && (!locations_.contains(*location.parent_location) || *location.parent_location == id))
            return foundation::Result<void>::Failure(Error("gameplay.world.topology_invalid", "location parent is invalid"));
        std::unordered_set<LocationId, IdHash> seen; auto current = location.parent_location;
        while (current)
        {
            if (!seen.insert(*current).second) return foundation::Result<void>::Failure(Error("gameplay.world.topology_cycle", "location hierarchy contains a cycle"));
            current = locations_.at(*current).parent_location;
        }
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::Freeze()
{
    if (frozen_) return foundation::Result<void>::Success();
    if (auto validation = ValidateTopology(); !validation) return validation;
    RebuildTopologyIndexes(); frozen_ = true; return foundation::Result<void>::Success();
}

const WorldRegionDefinition* WorldService::FindRegion(WorldRegionId id) const noexcept { auto i=regions_.find(id); return i==regions_.end()?nullptr:&i->second; }
const WorldAreaDefinition* WorldService::FindArea(WorldAreaId id) const noexcept { auto i=areas_.find(id); return i==areas_.end()?nullptr:&i->second; }
const LocationDefinition* WorldService::FindLocation(LocationId id) const noexcept { auto i=locations_.find(id); return i==locations_.end()?nullptr:&i->second; }
std::optional<WorldFeatureRecord> WorldService::FindFeature(WorldFeatureId id) const { auto i=features_.find(id); return i==features_.end()?std::nullopt:std::optional<WorldFeatureRecord>{i->second}; }
std::optional<WorldAlterationRecord> WorldService::FindAlteration(WorldAlterationId id) const { auto i=alterations_.find(id); return i==alterations_.end()?std::nullopt:std::optional<WorldAlterationRecord>{i->second}; }
std::optional<ObjectPlacementRecord> WorldService::FindObjectPlacement(GameplayObjectRef object) const noexcept { auto i=object_placements_.find(object); return i==object_placements_.end()?std::nullopt:std::optional<ObjectPlacementRecord>{i->second}; }

std::vector<WorldAreaDefinition> WorldService::FindAreasAt(WorldPosition p) const
{
    std::vector<WorldAreaDefinition> out; std::unordered_set<WorldAreaId, IdHash> seen; const auto cell=CellFor(p);
    if (auto found=area_index_.find(cell); found!=area_index_.end()) for (auto id:found->second) if (seen.insert(id).second && areas_.at(id).bounds.Contains(p)) out.push_back(areas_.at(id));
    for (auto id:large_areas_) if (seen.insert(id).second && areas_.at(id).bounds.Contains(p)) out.push_back(areas_.at(id));
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<LocationDefinition> WorldService::FindLocationsInArea(WorldAreaId area) const
{
    std::vector<LocationDefinition> out; if(auto f=locations_by_area_.find(area);f!=locations_by_area_.end()) for(auto id:f->second) if(auto i=locations_.find(id);i!=locations_.end()) out.push_back(i->second);
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<LocationDefinition> WorldService::FindChildLocations(LocationId parent) const
{
    std::vector<LocationDefinition> out; if(auto f=child_locations_.find(parent);f!=child_locations_.end()) for(auto id:f->second) if(auto i=locations_.find(id);i!=locations_.end()) out.push_back(i->second);
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}
std::vector<WorldAlterationRecord> WorldService::FindAlterations(const WorldAabb& bounds, std::optional<WorldAlterationTypeId> type) const
{
    std::unordered_set<WorldAlterationId, IdHash> ids; auto cells=CellsFor(bounds);
    if(cells.empty()) for(const auto&[id,a]:alterations_) if(a.state==WorldAlterationState::Active) ids.insert(id);
    else { for(const auto&cell:cells) if(auto i=alteration_index_.find(cell);i!=alteration_index_.end()) ids.insert(i->second.begin(),i->second.end()); ids.insert(large_alterations_.begin(),large_alterations_.end()); }
    std::vector<WorldAlterationRecord> out; for(auto id:ids){const auto&a=alterations_.at(id);if(a.state==WorldAlterationState::Active&&Overlaps(a.affected_area,bounds)&&(!type||a.type==*type))out.push_back(a);}
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.id<b.id;}); return out;
}

foundation::Result<WorldFeatureId> WorldService::AddDynamicFeature(WorldFeatureRecord f)
{
    if (!frozen_) return foundation::Result<WorldFeatureId>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    if (!f.id.IsValid() || !f.type.IsValid() || !f.bounds.IsValid() || !feature_types_.contains(f.type) || features_.contains(f.id)) return foundation::Result<WorldFeatureId>::Failure(Error("gameplay.world.invalid_feature", "invalid dynamic feature"));
    if (!CanRecordChanges(1)) return foundation::Result<WorldFeatureId>::Failure(Error("gameplay.change_sequence_exhausted", "world change sequence is exhausted")); auto rev=PrepareRevision(); if(!rev)return foundation::Result<WorldFeatureId>::Failure(rev.GetError());
    f.dynamic=true; f.revision=rev.Value(); revision_=rev.Value(); features_.emplace(f.id,f); Record(WorldChange{0,WorldChangeKind::FeatureChanged,{},f.id,{},revision_,{}}); return foundation::Result<WorldFeatureId>::Success(f.id);
}
foundation::Result<void> WorldService::PlaceObject(ObjectPlacementRecord placement)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    if(!placement.object.IsValid()||(placement.location&&!locations_.contains(*placement.location))||(placement.area&&!areas_.contains(*placement.area))) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_object_placement","invalid object placement"));
    if(!CanRecordChanges(1))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    placement.revision=rev.Value();revision_=rev.Value();const auto object=placement.object;const auto ctx=placement.context;object_placements_[object]=std::move(placement);Record(WorldChange{0,WorldChangeKind::ObjectPlaced,{},{},object,revision_,ctx});return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RemoveObjectPlacement(GameplayObjectRef object, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    const auto found=object_placements_.find(object);if(found==object_placements_.end())return foundation::Result<void>::Failure(Error("gameplay.world.object_placement_missing","object placement is missing"));
    if(!CanRecordChanges(1))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    revision_=rev.Value();object_placements_.erase(found);Record(WorldChange{0,WorldChangeKind::ObjectPlacementRemoved,{},{},object,revision_,context});return foundation::Result<void>::Success();
}
std::vector<ObjectPlacementRecord> WorldService::FindObjectsInArea(WorldAreaId area) const {std::vector<ObjectPlacementRecord> out;for(const auto&[_,p]:object_placements_)if(p.area&&*p.area==area)out.push_back(p);std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.object<b.object;});return out;}
std::vector<ObjectPlacementRecord> WorldService::FindObjectsInLocation(LocationId location) const {std::vector<ObjectPlacementRecord> out;for(const auto&[_,p]:object_placements_)if(p.location&&*p.location==location)out.push_back(p);std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){return a.object<b.object;});return out;}

foundation::Result<void> WorldService::ValidateAlteration(const WorldAlterationRecord& record) const
{
    if(!record.id.IsValid()||!record.type.IsValid()||!record.affected_area.IsValid()||!alteration_types_.contains(record.type)) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","world alteration is structurally invalid"));
    if(record.expires_at && record.expires_at->ticks <= record.created_at.ticks) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","world alteration expiration must be later than creation"));
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::CommitMutations(std::span<const WorldTransaction::Mutation> mutations, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    if(mutations.empty())return foundation::Result<void>::Success();
    if(!CanRecordChanges(mutations.size()))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));
    auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    auto rebuilt=alterations_;std::unordered_set<WorldAlterationId,IdHash> targets;
    for(const auto&m:mutations)
    {
        const auto id=m.kind==WorldTransaction::Kind::Remove?m.id:m.record.id;if(!targets.insert(id).second)return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target","world transaction may mutate an alteration only once"));
        if(m.kind==WorldTransaction::Kind::Create){if(auto v=ValidateAlteration(m.record);!v)return v;if(rebuilt.contains(m.record.id))return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","alteration already exists"));auto r=m.record;r.revision=rev.Value();rebuilt.emplace(r.id,std::move(r));}
        else if(m.kind==WorldTransaction::Kind::Update){if(!rebuilt.contains(m.record.id))return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration update target missing"));if(auto v=ValidateAlteration(m.record);!v)return v;auto r=m.record;r.revision=rev.Value();rebuilt[r.id]=std::move(r);}
        else{auto i=rebuilt.find(m.id);if(i==rebuilt.end())return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration remove target missing"));i->second.state=WorldAlterationState::Removed;i->second.revision=rev.Value();}
    }
    revision_=rev.Value();alterations_=std::move(rebuilt);
    for(const auto&m:mutations)
    {
        if(m.kind==WorldTransaction::Kind::Create)Record(WorldChange{0,WorldChangeKind::AlterationCreated,m.record.id,{},{},revision_,context});
        else if(m.kind==WorldTransaction::Kind::Update)Record(WorldChange{0,m.record.state==WorldAlterationState::Expired?WorldChangeKind::AlterationExpired:WorldChangeKind::AlterationUpdated,m.record.id,{},{},revision_,context});
        else Record(WorldChange{0,WorldChangeKind::AlterationRemoved,m.id,{},{},revision_,context});
    }
    RebuildAlterationIndex();if(transactions_!=std::numeric_limits<std::uint64_t>::max())++transactions_;return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::ExpireAlteration(WorldAlterationId id, GameplayTimePoint now, GameplayContext context)
{
    auto current=FindAlteration(id);if(!current)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration expiration target missing"));if(current->state!=WorldAlterationState::Active)return foundation::Result<void>::Success();if(!current->expires_at||current->expires_at->ticks>now.ticks)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_not_due","alteration is not due to expire"));
    auto updated=*current;updated.state=WorldAlterationState::Expired;auto tx=BeginTransaction(context);auto q=tx.Update(std::move(updated));if(!q)return q;return tx.Commit();
}
foundation::Result<void> WorldService::SweepExpired(GameplayTimePoint now, GameplayContext context)
{
    auto tx=BeginTransaction(context);bool any=false;std::vector<WorldAlterationId> ids;for(const auto&[id,a]:alterations_)if(a.state==WorldAlterationState::Active&&a.expires_at&&a.expires_at->ticks<=now.ticks)ids.push_back(id);std::sort(ids.begin(),ids.end());
    for(auto id:ids){auto r=alterations_.at(id);r.state=WorldAlterationState::Expired;auto u=tx.Update(std::move(r));if(!u)return u;any=true;}return any?tx.Commit():foundation::Result<void>::Success();
}

WorldSnapshot WorldService::CaptureSnapshot() const
{
    WorldSnapshot s;for(const auto&[_,f]:features_)if(f.dynamic)s.dynamic_features.push_back(f);for(const auto&[_,p]:object_placements_)s.object_placements.push_back(p);for(const auto&[_,a]:alterations_)if(a.persistence==WorldAlterationPersistence::Persistent)s.alterations.push_back(a);
    std::sort(s.dynamic_features.begin(),s.dynamic_features.end(),[](const auto&a,const auto&b){return a.id<b.id;});std::sort(s.object_placements.begin(),s.object_placements.end(),[](const auto&a,const auto&b){return a.object<b.object;});std::sort(s.alterations.begin(),s.alterations.end(),[](const auto&a,const auto&b){return a.id<b.id;});s.alteration_ids=alteration_ids_.GetSnapshot();s.revision=revision_;return s;
}
foundation::Result<void> WorldService::RestoreSnapshot(WorldSnapshot s)
{
    if(!frozen_)return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen","world service must be frozen before restore"));
    if(!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(s.alteration_ids)||s.alteration_ids.scope!=alteration_ids_.GetSnapshot().scope)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid alteration id generator snapshot"));
    auto rebuilt_features=features_;for(auto i=rebuilt_features.begin();i!=rebuilt_features.end();)if(i->second.dynamic)i=rebuilt_features.erase(i);else ++i;
    std::unordered_set<WorldFeatureId,IdHash> feature_ids;for(const auto&[id,_]:rebuilt_features)feature_ids.insert(id);
    for(auto&f:s.dynamic_features){if(!f.id.IsValid()||!f.dynamic||!f.type.IsValid()||!f.bounds.IsValid()||!feature_types_.contains(f.type)||f.revision>s.revision||!feature_ids.insert(f.id).second)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid or duplicate dynamic feature in snapshot"));rebuilt_features.emplace(f.id,f);}
    std::unordered_map<GameplayObjectRef,ObjectPlacementRecord> rebuilt_placements;for(auto&p:s.object_placements){if(!p.object.IsValid()||(p.location&&!locations_.contains(*p.location))||(p.area&&!areas_.contains(*p.area))||p.revision>s.revision||!rebuilt_placements.emplace(p.object,p).second)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid or duplicate object placement in snapshot"));}
    std::unordered_map<WorldAlterationId,WorldAlterationRecord,IdHash> rebuilt_alterations;std::uint64_t max_generated_low=0;
    for(auto&a:s.alterations){if(auto v=ValidateAlteration(a);!v)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid alteration in snapshot"));if(a.persistence!=WorldAlterationPersistence::Persistent||a.revision>s.revision||!rebuilt_alterations.emplace(a.id,a).second)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","invalid or duplicate alteration in snapshot"));if(a.id.value.High()==s.alteration_ids.scope&&a.id.value.Low()>max_generated_low)max_generated_low=a.id.value.Low();}
    if(s.alteration_ids.next!=0&&s.alteration_ids.next<=max_generated_low)return foundation::Result<void>::Failure(Error("gameplay.world.restore_invalid","alteration id generator can reproduce a restored id"));
    features_=std::move(rebuilt_features);object_placements_=std::move(rebuilt_placements);alterations_=std::move(rebuilt_alterations);alteration_ids_.Restore(s.alteration_ids);revision_=s.revision;changes_.clear();next_change_sequence_=1;last_change_sequence_=0;RebuildAlterationIndex();return foundation::Result<void>::Success();
}
std::vector<WorldChange> WorldService::ChangesSince(std::uint64_t sequence) const {std::vector<WorldChange> out;std::copy_if(changes_.begin(),changes_.end(),std::back_inserter(out),[sequence](const auto&c){return c.sequence>sequence;});return out;}
WorldDiagnostics WorldService::GetDiagnostics() const noexcept
{
    WorldDiagnostics d;d.regions=regions_.size();d.areas=areas_.size();d.locations=locations_.size();d.features=features_.size();d.transactions=transactions_;for(const auto&[_,a]:alterations_){if(a.state==WorldAlterationState::Active)++d.active_alterations;if(a.persistence==WorldAlterationPersistence::Persistent)++d.persistent_alterations;}return d;
}
} // namespace epidemic::gameplay::world
