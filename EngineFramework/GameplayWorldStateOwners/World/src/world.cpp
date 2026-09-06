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
    if (!record.id.IsValid())
        record.id = WorldAlterationId{owner_->alteration_ids_.Next()};
    else
    {
        if (owner_->alterations_.contains(record.id))
            return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.invalid_alteration", "alteration already exists"));
        if (auto sync = owner_->SynchronizeRequestedAlterationId(record.id); !sync)
            return foundation::Result<WorldAlterationId>::Failure(sync.GetError());
    }
    if (!record.id.IsValid()) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.alteration_id_exhausted", "world alteration id generator is exhausted"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.id.IsValid() ? mutation.id : mutation.record.id;
        if (existing == record.id) return foundation::Result<WorldAlterationId>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    Mutation mutation; mutation.kind = Kind::Create; mutation.id = record.id; mutation.record = std::move(record);
    mutations_.push_back(std::move(mutation));
    return foundation::Result<WorldAlterationId>::Success(mutations_.back().record.id);
}
foundation::Result<void> WorldTransaction::Update(WorldAlterationId id, WorldAlterationUpdate update)
{
    if (!owner_ || committed_ || cancelled_ || !id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world update is invalid"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.id.IsValid() ? mutation.id : mutation.record.id;
        if (existing == id) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    Mutation mutation; mutation.kind = Kind::Update; mutation.id = id; mutation.update = std::move(update);
    mutations_.push_back(std::move(mutation)); return foundation::Result<void>::Success();
}
foundation::Result<void> WorldTransaction::Remove(WorldAlterationId id)
{
    if (!owner_ || committed_ || cancelled_ || !id.IsValid()) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_invalid", "world remove is invalid"));
    for (const auto& mutation : mutations_)
    {
        const auto existing = mutation.id.IsValid() ? mutation.id : mutation.record.id;
        if (existing == id) return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target", "world transaction may mutate an alteration only once"));
    }
    Mutation mutation; mutation.kind = Kind::Remove; mutation.id = id; mutations_.push_back(std::move(mutation)); return foundation::Result<void>::Success();
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
void WorldService::IndexAlteration(const WorldAlterationRecord& alteration)
{
    if (alteration.state != WorldAlterationState::Active) return;
    auto cells = CellsFor(alteration.affected_area);
    if (cells.empty())
    {
        large_alterations_.push_back(alteration.id);
        Canonicalize(large_alterations_);
        return;
    }
    for (const auto& cell : cells)
    {
        auto& ids = alteration_index_[cell];
        ids.push_back(alteration.id);
        Canonicalize(ids);
    }
}
void WorldService::UnindexAlteration(const WorldAlterationRecord& alteration)
{
    if (alteration.state != WorldAlterationState::Active) return;
    auto cells = CellsFor(alteration.affected_area);
    if (cells.empty())
    {
        std::erase(large_alterations_, alteration.id);
        return;
    }
    for (const auto& cell : cells)
    {
        auto found = alteration_index_.find(cell);
        if (found == alteration_index_.end()) continue;
        std::erase(found->second, alteration.id);
        if (found->second.empty()) alteration_index_.erase(found);
    }
}
void WorldService::RebuildAlterationIndex()
{
    alteration_index_.clear(); large_alterations_.clear();
    std::vector<WorldAlterationId> ids; for (const auto& [id, _] : alterations_) ids.push_back(id); std::sort(ids.begin(), ids.end());
    for (const auto id : ids) IndexAlteration(alterations_.at(id));
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
    while (changes_.size() > kChangeJournalCapacity) changes_.pop_front();
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
    std::vector<WorldAlterationId> ids; auto cells=CellsFor(bounds);
    if(cells.empty())
    {
        for(const auto&[id,a]:alterations_) if(a.state==WorldAlterationState::Active) ids.push_back(id);
    }
    else
    {
        for(const auto&cell:cells) if(auto i=alteration_index_.find(cell);i!=alteration_index_.end()) ids.insert(ids.end(),i->second.begin(),i->second.end());
        ids.insert(ids.end(),large_alterations_.begin(),large_alterations_.end());
    }
    Canonicalize(ids);
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
foundation::Result<void> WorldService::UpdateDynamicFeature(WorldFeatureId id, WorldAabb bounds, GameplayTagSet tags, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    auto found=features_.find(id);if(found==features_.end()||!found->second.dynamic)return foundation::Result<void>::Failure(Error("gameplay.world.dynamic_feature_missing","dynamic feature is missing"));
    if(!bounds.IsValid())return foundation::Result<void>::Failure(Error("gameplay.world.invalid_feature","dynamic feature bounds are invalid"));
    if(!CanRecordChanges(1))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    found->second.bounds=bounds;found->second.tags=std::move(tags);found->second.revision=rev.Value();revision_=rev.Value();Record(WorldChange{0,WorldChangeKind::FeatureChanged,{},id,{},revision_,context});return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::RemoveDynamicFeature(WorldFeatureId id, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    auto found=features_.find(id);if(found==features_.end()||!found->second.dynamic)return foundation::Result<void>::Failure(Error("gameplay.world.dynamic_feature_missing","dynamic feature is missing"));
    if(!CanRecordChanges(1))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    features_.erase(found);revision_=rev.Value();Record(WorldChange{0,WorldChangeKind::FeatureRemoved,{},id,{},revision_,context});return foundation::Result<void>::Success();
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
    if(record.state==WorldAlterationState::Compacted) return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","compacted alterations are not stored records"));
    return foundation::Result<void>::Success();
}
foundation::Result<WorldAlterationRecord> WorldService::ApplyAlterationUpdate(const WorldAlterationRecord& current, const WorldAlterationUpdate& update) const
{
    if(current.state!=WorldAlterationState::Active) return foundation::Result<WorldAlterationRecord>::Failure(Error("gameplay.world.alteration_terminal","terminal world alteration cannot be updated"));
    auto result=current;
    if(update.affected_area) result.affected_area=*update.affected_area;
    if(update.update_expires_at) result.expires_at=update.expires_at;
    if(update.payload) result.payload=*update.payload;
    if(update.context) result.context=*update.context;
    if(update.state)
    {
        if(*update.state!=WorldAlterationState::Active&&*update.state!=WorldAlterationState::Superseded&&*update.state!=WorldAlterationState::Expired)
            return foundation::Result<WorldAlterationRecord>::Failure(Error("gameplay.world.invalid_alteration_transition","unsupported world alteration state transition"));
        result.state=*update.state;
    }
    if(auto validation=ValidateAlteration(result);!validation)return foundation::Result<WorldAlterationRecord>::Failure(validation.GetError());
    return foundation::Result<WorldAlterationRecord>::Success(std::move(result));
}
foundation::Result<void> WorldService::SynchronizeRequestedAlterationId(WorldAlterationId id)
{
    if(!id.IsValid())return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","requested world alteration id is invalid"));
    auto snapshot=alteration_ids_.GetSnapshot();
    if(id.value.High()!=snapshot.scope||snapshot.next==0||id.value.Low()<snapshot.next)return foundation::Result<void>::Success();
    snapshot.next=id.value.Low()==std::numeric_limits<std::uint64_t>::max()?0:id.value.Low()+1;
    alteration_ids_.Restore(snapshot);
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::CommitMutations(std::span<const WorldTransaction::Mutation> mutations, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    if(mutations.empty())return foundation::Result<void>::Success();
    if(!CanRecordChanges(mutations.size()))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));
    auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());

    struct StagedMutation { WorldTransaction::Kind kind{}; WorldAlterationId id{}; WorldAlterationRecord before{}; WorldAlterationRecord after{}; };
    std::vector<StagedMutation> staged;staged.reserve(mutations.size());
    std::unordered_set<WorldAlterationId,IdHash> targets;targets.reserve(mutations.size());
    for(const auto&m:mutations)
    {
        const auto id=m.kind==WorldTransaction::Kind::Create?m.record.id:m.id;
        if(!targets.insert(id).second)return foundation::Result<void>::Failure(Error("gameplay.world.transaction_duplicate_target","world transaction may mutate an alteration only once"));
        if(m.kind==WorldTransaction::Kind::Create)
        {
            if(auto v=ValidateAlteration(m.record);!v)return v;
            if(m.record.state!=WorldAlterationState::Active)return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","new world alteration must start active"));
            if(alterations_.contains(id))return foundation::Result<void>::Failure(Error("gameplay.world.invalid_alteration","alteration already exists"));
            auto after=m.record;after.revision=rev.Value();staged.push_back({m.kind,id,{},std::move(after)});
        }
        else
        {
            auto found=alterations_.find(id);if(found==alterations_.end())return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing",m.kind==WorldTransaction::Kind::Update?"alteration update target missing":"alteration remove target missing"));
            if(m.kind==WorldTransaction::Kind::Update)
            {
                auto updated=ApplyAlterationUpdate(found->second,m.update);if(!updated)return foundation::Result<void>::Failure(updated.GetError());auto after=std::move(updated.Value());after.revision=rev.Value();staged.push_back({m.kind,id,found->second,std::move(after)});
            }
            else
            {
                if(found->second.state!=WorldAlterationState::Active)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_terminal","terminal world alteration cannot be removed again"));
                auto after=found->second;after.state=WorldAlterationState::Removed;after.revision=rev.Value();staged.push_back({m.kind,id,found->second,std::move(after)});
            }
        }
    }

    for(const auto& staged_mutation:staged)
    {
        if(staged_mutation.kind!=WorldTransaction::Kind::Create)UnindexAlteration(staged_mutation.before);
        if(staged_mutation.kind==WorldTransaction::Kind::Create)alterations_.emplace(staged_mutation.id,staged_mutation.after);
        else alterations_.at(staged_mutation.id)=staged_mutation.after;
        IndexAlteration(staged_mutation.after);
    }
    revision_=rev.Value();
    for(const auto& staged_mutation:staged)
    {
        auto kind=WorldChangeKind::AlterationUpdated;
        if(staged_mutation.kind==WorldTransaction::Kind::Create)kind=WorldChangeKind::AlterationCreated;
        else if(staged_mutation.kind==WorldTransaction::Kind::Remove)kind=WorldChangeKind::AlterationRemoved;
        else if(staged_mutation.after.state==WorldAlterationState::Expired)kind=WorldChangeKind::AlterationExpired;
        Record(WorldChange{0,kind,staged_mutation.id,{},{},revision_,context});
    }
    if(transactions_!=std::numeric_limits<std::uint64_t>::max())++transactions_;
    return foundation::Result<void>::Success();
}
foundation::Result<void> WorldService::ExpireAlteration(WorldAlterationId id, GameplayTimePoint now, GameplayContext context)
{
    auto current=FindAlteration(id);if(!current)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration expiration target missing"));if(current->state!=WorldAlterationState::Active)return foundation::Result<void>::Success();if(!current->expires_at||current->expires_at->ticks>now.ticks)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_not_due","alteration is not due to expire"));
    WorldAlterationUpdate update;update.state=WorldAlterationState::Expired;update.context=context;auto tx=BeginTransaction(context);auto q=tx.Update(id,std::move(update));if(!q)return q;return tx.Commit();
}
foundation::Result<void> WorldService::SweepExpired(GameplayTimePoint now, GameplayContext context)
{
    auto tx=BeginTransaction(context);bool any=false;std::vector<WorldAlterationId> ids;for(const auto&[id,a]:alterations_)if(a.state==WorldAlterationState::Active&&a.expires_at&&a.expires_at->ticks<=now.ticks)ids.push_back(id);std::sort(ids.begin(),ids.end());
    for(auto id:ids){WorldAlterationUpdate update;update.state=WorldAlterationState::Expired;update.context=context;auto u=tx.Update(id,std::move(update));if(!u)return u;any=true;}return any?tx.Commit():foundation::Result<void>::Success();
}

foundation::Result<void> WorldService::CompactAlteration(WorldAlterationId id, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "world service must be frozen before runtime operations"));
    auto found=alterations_.find(id);if(found==alterations_.end())return foundation::Result<void>::Failure(Error("gameplay.world.alteration_missing","alteration compaction target missing"));
    if(found->second.state!=WorldAlterationState::Removed&&found->second.state!=WorldAlterationState::Expired)return foundation::Result<void>::Failure(Error("gameplay.world.alteration_not_compactable","only removed or expired alterations may be compacted"));
    if(!CanRecordChanges(1))return foundation::Result<void>::Failure(Error("gameplay.change_sequence_exhausted","world change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());
    UnindexAlteration(found->second);alterations_.erase(found);revision_=rev.Value();Record(WorldChange{0,WorldChangeKind::AlterationCompacted,id,{},{},revision_,context});return foundation::Result<void>::Success();
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
std::vector<WorldChange> WorldService::ChangesSince(std::uint64_t sequence) const {return ReadChangesSince(sequence).changes;}
WorldChangeBatch WorldService::ReadChangesSince(std::uint64_t sequence) const
{
    WorldChangeBatch batch;batch.oldest_available_sequence=changes_.empty()?next_change_sequence_:changes_.front().sequence;
    if(!changes_.empty()&&sequence<changes_.front().sequence-1){batch.snapshot_required=true;return batch;}
    const auto found=std::upper_bound(changes_.begin(),changes_.end(),sequence,[](std::uint64_t value,const WorldChange& change){return value<change.sequence;});batch.changes.assign(found,changes_.end());return batch;
}
void WorldService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found=std::lower_bound(changes_.begin(),changes_.end(),sequence,[](const WorldChange& change,std::uint64_t value){return change.sequence<value;});changes_.erase(changes_.begin(),found);
}
WorldDiagnostics WorldService::GetDiagnostics() const noexcept
{
    WorldDiagnostics d;d.regions=regions_.size();d.areas=areas_.size();d.locations=locations_.size();d.features=features_.size();d.transactions=transactions_;for(const auto&[_,a]:alterations_){if(a.state==WorldAlterationState::Active)++d.active_alterations;if(a.persistence==WorldAlterationPersistence::Persistent)++d.persistent_alterations;}return d;
}
} // namespace epidemic::gameplay::world
