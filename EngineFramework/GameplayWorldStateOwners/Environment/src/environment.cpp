#include "Epidemic/GameFramework/Environment/environment.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::environment
{
namespace
{
foundation::Error E(std::string_view c, std::string_view m) { return foundation::Error::Create(c, m); }
EnvironmentFixed Sat(std::int64_t v) { return static_cast<EnvironmentFixed>(std::clamp<std::int64_t>(v, std::numeric_limits<EnvironmentFixed>::min(), std::numeric_limits<EnvironmentFixed>::max())); }
bool SameHazard(const EnvironmentHazard& a, const EnvironmentHazard& b) { return a.type == b.type && a.intensity == b.intensity && a.tags.Values() == b.tags.Values(); }
bool SameContext(const GameplayContext& a, const GameplayContext& b)
{
    return a.tick==b.tick&&a.time==b.time&&a.operation==b.operation&&a.correlation==b.correlation&&a.actor==b.actor&&a.instigator==b.instigator&&a.source==b.source&&a.parent_operation==b.parent_operation&&a.cause_event==b.cause_event;
}
bool SameLayer(const EnvironmentLayer& a, const EnvironmentLayer& b)
{
    return a.id==b.id&&a.type==b.type&&a.bounds==b.bounds&&a.area_ref==b.area_ref&&a.priority==b.priority&&a.blend==b.blend&&a.custom_blend==b.custom_blend&&a.values==b.values&&a.tags.Values()==b.tags.Values()&&
           a.created_at==b.created_at&&a.expires_at==b.expires_at&&a.persistence==b.persistence&&SameContext(a.context,b.context)&&a.hazards.size()==b.hazards.size()&&std::equal(a.hazards.begin(),a.hazards.end(),b.hazards.begin(),SameHazard);
}
std::int64_t FloorDiv(std::int64_t value, std::int64_t divisor) noexcept { const auto q=value/divisor;const auto r=value%divisor;return (r!=0&&((r<0)!=(divisor<0)))?q-1:q; }
} // namespace

EnvironmentService::EnvironmentService() : ids_(GameplayObjectId::FromString("framework.environment.layer.seed").High()) {}
EnvironmentService::CellKey EnvironmentService::CellFor(EnvironmentPosition p) const noexcept { return {FloorDiv(p.x_mm,kSpatialCellMm),FloorDiv(p.y_mm,kSpatialCellMm),FloorDiv(p.z_mm,kSpatialCellMm)}; }
std::vector<EnvironmentService::CellKey> EnvironmentService::CellsFor(const EnvironmentAabb& b) const
{
    const auto lo=CellFor(b.min),hi=CellFor(b.max);if(hi.x<lo.x||hi.y<lo.y||hi.z<lo.z)return{};
    const auto nx=static_cast<std::uint64_t>(hi.x-lo.x+1),ny=static_cast<std::uint64_t>(hi.y-lo.y+1),nz=static_cast<std::uint64_t>(hi.z-lo.z+1);
    if(nx==0||ny==0||nz==0||nx>kMaxIndexedCells||ny>kMaxIndexedCells||nz>kMaxIndexedCells||nx>kMaxIndexedCells/ny||nx*ny>kMaxIndexedCells/nz)return{};
    std::vector<CellKey> out;out.reserve(static_cast<std::size_t>(nx*ny*nz));for(auto x=lo.x;x<=hi.x;++x)for(auto y=lo.y;y<=hi.y;++y)for(auto z=lo.z;z<=hi.z;++z)out.push_back({x,y,z});return out;
}
void EnvironmentService::RebuildSpatialIndex()
{
    spatial_index_.clear();global_layers_.clear();large_layers_.clear();
    std::vector<EnvironmentLayerId> ids;for(const auto&[id,_]:layers_)ids.push_back(id);std::sort(ids.begin(),ids.end());
    for(const auto id:ids){const auto&l=layers_.at(id);if(!l.bounds){global_layers_.push_back(id);continue;}auto cells=CellsFor(*l.bounds);if(cells.empty()){large_layers_.push_back(id);continue;}for(const auto&c:cells)spatial_index_[c].push_back(id);}
}
foundation::Result<Revision> EnvironmentService::PrepareRevision() const
{
    const auto next=CheckedNext(revision_);if(!next)return foundation::Result<Revision>::Failure(E("gameplay.revision_exhausted","environment revision counter is exhausted"));return foundation::Result<Revision>::Success(*next);
}
bool EnvironmentService::CanRecordChanges(std::size_t count) const noexcept
{
    if(count==0)return true;if(next_change_sequence_==0)return false;return count-1<=std::numeric_limits<std::uint64_t>::max()-next_change_sequence_;
}
void EnvironmentService::Record(EnvironmentChange c) noexcept
{
    c.sequence=next_change_sequence_;last_change_sequence_=next_change_sequence_;if(next_change_sequence_==std::numeric_limits<std::uint64_t>::max())next_change_sequence_=0;else ++next_change_sequence_;changes_.push_back(std::move(c));
}
foundation::Result<void> EnvironmentService::ValidateLayer(const EnvironmentLayer& l) const
{
    if(!l.id.IsValid()||!l.type.IsValid()||!types_.contains(l.type)||(l.bounds&&!l.bounds->IsValid())||(l.blend==EnvironmentBlendPolicy::CustomRegistered&&!blend_handlers_.contains(l.custom_blend)))
        return foundation::Result<void>::Failure(E("gameplay.environment.invalid_layer","invalid environment layer"));
    if(l.expires_at&&l.expires_at->ticks<=l.created_at.ticks)return foundation::Result<void>::Failure(E("gameplay.environment.invalid_time_range","environment layer expiration must be later than creation"));
    if((l.values.present&~kEnvironmentAllValues)!=0)return foundation::Result<void>::Failure(E("gameplay.environment.invalid_values","environment value mask contains unknown fields"));
    if(l.values.humidity<0||l.values.precipitation<0||l.values.wind_strength<0||l.values.visibility<0||l.values.light_exposure<0)return foundation::Result<void>::Failure(E("gameplay.environment.invalid_values","environment values are outside supported range"));
    std::unordered_set<EnvironmentHazardTypeId,HazardHash> seen_hazards;
    for(const auto&hazard:l.hazards)if(!hazard.type.IsValid()||!hazard_types_.contains(hazard.type)||hazard.intensity<0||!seen_hazards.insert(hazard.type).second)return foundation::Result<void>::Failure(E("gameplay.environment.invalid_hazard","environment hazard is invalid or duplicated"));
    return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::RegisterLayerType(EnvironmentLayerTypeId id,std::string n)
{
    if(frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_frozen","environment registry is frozen"));if(!id.IsValid()||n.empty()||types_.contains(id))return foundation::Result<void>::Failure(E("gameplay.environment.invalid_type","invalid or duplicate environment layer type"));types_.emplace(id,std::move(n));return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::RegisterBlendHandler(EnvironmentBlendHandlerId id,std::string name,BlendHandler handler)
{
    if(frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_frozen","environment registry is frozen"));if(!id.IsValid()||name.empty()||!handler||blend_handlers_.contains(id))return foundation::Result<void>::Failure(E("gameplay.environment.invalid_blend_handler","invalid or duplicate blend handler"));blend_handlers_.emplace(id,std::make_pair(std::move(name),std::move(handler)));return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::RegisterHazardType(EnvironmentHazardTypeId id,std::string name)
{
    if(frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_frozen","environment registry is frozen"));if(!id.IsValid()||name.empty()||hazard_types_.contains(id))return foundation::Result<void>::Failure(E("gameplay.environment.invalid_hazard_type","invalid or duplicate environment hazard type"));hazard_types_.emplace(id,std::move(name));return foundation::Result<void>::Success();
}
foundation::Result<EnvironmentLayerId> EnvironmentService::AddLayer(EnvironmentLayer l)
{
    if(!frozen_)return foundation::Result<EnvironmentLayerId>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before runtime operations"));if(!l.id.IsValid())l.id=EnvironmentLayerId{ids_.Next()};if(!l.id.IsValid())return foundation::Result<EnvironmentLayerId>::Failure(E("gameplay.environment.layer_id_exhausted","environment layer id generator is exhausted"));if(layers_.contains(l.id))return foundation::Result<EnvironmentLayerId>::Failure(E("gameplay.environment.invalid_layer","duplicate environment layer"));if(auto valid=ValidateLayer(l);!valid)return foundation::Result<EnvironmentLayerId>::Failure(valid.GetError());if(!CanRecordChanges(1))return foundation::Result<EnvironmentLayerId>::Failure(E("gameplay.change_sequence_exhausted","environment change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<EnvironmentLayerId>::Failure(rev.GetError());revision_=rev.Value();l.revision=revision_;layers_[l.id]=l;RebuildSpatialIndex();Record({0,EnvironmentChangeKind::Added,l.id,revision_,l.context});return foundation::Result<EnvironmentLayerId>::Success(l.id);
}
foundation::Result<void> EnvironmentService::UpdateLayer(EnvironmentLayer l)
{
    if(!frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before runtime operations"));auto it=layers_.find(l.id);if(it==layers_.end())return foundation::Result<void>::Failure(E("gameplay.environment.layer_missing","invalid environment layer update"));if(auto valid=ValidateLayer(l);!valid)return valid;if(SameLayer(it->second,l))return foundation::Result<void>::Success();if(!CanRecordChanges(1))return foundation::Result<void>::Failure(E("gameplay.change_sequence_exhausted","environment change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());revision_=rev.Value();l.revision=revision_;it->second=std::move(l);RebuildSpatialIndex();Record({0,EnvironmentChangeKind::Changed,it->first,revision_,it->second.context});return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::RemoveLayer(EnvironmentLayerId id,GameplayContext c)
{
    if(!frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before runtime operations"));auto it=layers_.find(id);if(it==layers_.end())return foundation::Result<void>::Failure(E("gameplay.environment.layer_missing","environment layer missing"));if(!CanRecordChanges(1))return foundation::Result<void>::Failure(E("gameplay.change_sequence_exhausted","environment change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());revision_=rev.Value();layers_.erase(it);RebuildSpatialIndex();Record({0,EnvironmentChangeKind::Removed,id,revision_,c});return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::ExpireLayer(EnvironmentLayerId id,GameplayTimePoint now,GameplayContext c)
{
    if(!frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before runtime operations"));auto it=layers_.find(id);if(it==layers_.end())return foundation::Result<void>::Failure(E("gameplay.environment.layer_missing","environment layer missing"));if(!it->second.expires_at||it->second.expires_at->ticks>now.ticks)return foundation::Result<void>::Failure(E("gameplay.environment.layer_not_due","environment layer is not due to expire"));if(!CanRecordChanges(1))return foundation::Result<void>::Failure(E("gameplay.change_sequence_exhausted","environment change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());revision_=rev.Value();layers_.erase(it);RebuildSpatialIndex();Record({0,EnvironmentChangeKind::Expired,id,revision_,c});if(expired_!=std::numeric_limits<std::uint64_t>::max())++expired_;return foundation::Result<void>::Success();
}
foundation::Result<void> EnvironmentService::SweepExpired(GameplayTimePoint now,GameplayContext c)
{
    if(!frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before runtime operations"));std::vector<EnvironmentLayerId> ids;for(const auto&[id,l]:layers_)if(l.expires_at&&l.expires_at->ticks<=now.ticks)ids.push_back(id);std::sort(ids.begin(),ids.end());if(ids.empty())return foundation::Result<void>::Success();if(!CanRecordChanges(ids.size()))return foundation::Result<void>::Failure(E("gameplay.change_sequence_exhausted","environment change sequence is exhausted"));auto rev=PrepareRevision();if(!rev)return foundation::Result<void>::Failure(rev.GetError());revision_=rev.Value();for(auto id:ids){layers_.erase(id);Record({0,EnvironmentChangeKind::Expired,id,revision_,c});if(expired_!=std::numeric_limits<std::uint64_t>::max())++expired_;}RebuildSpatialIndex();return foundation::Result<void>::Success();
}
std::optional<EnvironmentLayer> EnvironmentService::GetLayer(EnvironmentLayerId id) const noexcept {auto i=layers_.find(id);return i==layers_.end()?std::nullopt:std::optional<EnvironmentLayer>{i->second};}
std::vector<EnvironmentLayer> EnvironmentService::FindLayers(EnvironmentPosition p) const
{
    std::unordered_set<EnvironmentLayerId,IdHash> ids(global_layers_.begin(),global_layers_.end());ids.insert(large_layers_.begin(),large_layers_.end());const auto cell=CellFor(p);if(auto i=spatial_index_.find(cell);i!=spatial_index_.end())ids.insert(i->second.begin(),i->second.end());std::vector<EnvironmentLayer> out;for(auto id:ids){const auto&l=layers_.at(id);if(!l.bounds||l.bounds->Contains(p))out.push_back(l);}std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){if(a.priority!=b.priority)return a.priority<b.priority;return a.id<b.id;});return out;
}
void EnvironmentService::Blend(EnvironmentValues& b,const EnvironmentLayer& layer) const
{
    const auto&a=layer.values;if(layer.blend==EnvironmentBlendPolicy::CustomRegistered){if(auto i=blend_handlers_.find(layer.custom_blend);i!=blend_handlers_.end())b=i->second.second(b,a);return;}
    auto apply=[p=layer.blend](EnvironmentFixed x,EnvironmentFixed y){switch(p){case EnvironmentBlendPolicy::Override:return y;case EnvironmentBlendPolicy::Add:return Sat(static_cast<std::int64_t>(x)+y);case EnvironmentBlendPolicy::Multiply:return Sat((static_cast<std::int64_t>(x)*y)/kEnvironmentOne);case EnvironmentBlendPolicy::Min:return std::min(x,y);case EnvironmentBlendPolicy::Max:return std::max(x,y);case EnvironmentBlendPolicy::CustomRegistered:return y;}return y;};
    auto has=[&](EnvironmentValueField f){return(a.present&static_cast<EnvironmentValueMask>(f))!=0;};if(has(EnvironmentValueField::Temperature))b.temperature_milli_c=apply(b.temperature_milli_c,a.temperature_milli_c);if(has(EnvironmentValueField::Humidity))b.humidity=apply(b.humidity,a.humidity);if(has(EnvironmentValueField::Precipitation))b.precipitation=apply(b.precipitation,a.precipitation);if(has(EnvironmentValueField::WindStrength))b.wind_strength=apply(b.wind_strength,a.wind_strength);if(has(EnvironmentValueField::WindDirection)){b.wind_x=apply(b.wind_x,a.wind_x);b.wind_y=apply(b.wind_y,a.wind_y);b.wind_z=apply(b.wind_z,a.wind_z);}if(has(EnvironmentValueField::Visibility))b.visibility=apply(b.visibility,a.visibility);if(has(EnvironmentValueField::LightExposure))b.light_exposure=apply(b.light_exposure,a.light_exposure);
}
EnvironmentSample EnvironmentService::Sample(EnvironmentPosition p,GameplayTimePoint t,std::span<const GameplayObjectRef> scopes) const
{
    if(samples_!=std::numeric_limits<std::uint64_t>::max())++samples_;EnvironmentSample s;s.position=p;s.time=t;s.revision=revision_;const auto ls=FindLayers(p);
    for(const auto&l:ls){if(l.created_at.ticks>t.ticks||(l.expires_at&&l.expires_at->ticks<=t.ticks))continue;if(l.area_ref.IsValid()&&std::find(scopes.begin(),scopes.end(),l.area_ref)==scopes.end())continue;Blend(s.values,l);for(auto tag:l.tags.Values())s.tags.Add(tag);s.hazards.insert(s.hazards.end(),l.hazards.begin(),l.hazards.end());s.layers.push_back(l.id);}
    std::sort(s.hazards.begin(),s.hazards.end(),[](const auto&a,const auto&b){if(a.type!=b.type)return a.type<b.type;return a.intensity<b.intensity;});return s;
}
EnvironmentSnapshot EnvironmentService::CaptureSnapshot() const
{
    EnvironmentSnapshot s;for(const auto&[_,l]:layers_)if(l.persistence==EnvironmentPersistence::Persistent)s.layers.push_back(l);std::sort(s.layers.begin(),s.layers.end(),[](const auto&a,const auto&b){return a.id<b.id;});s.ids=ids_.GetSnapshot();s.revision=revision_;return s;
}
foundation::Result<void> EnvironmentService::RestoreSnapshot(EnvironmentSnapshot s)
{
    if(!frozen_)return foundation::Result<void>::Failure(E("gameplay.registry_not_frozen","environment service must be frozen before restore"));if(!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(s.ids)||s.ids.scope!=ids_.GetSnapshot().scope)return foundation::Result<void>::Failure(E("gameplay.environment.restore_invalid","invalid environment id generator snapshot"));
    std::unordered_map<EnvironmentLayerId,EnvironmentLayer,IdHash> rebuilt;std::uint64_t max_low=0;for(auto&l:s.layers){if(l.persistence!=EnvironmentPersistence::Persistent||l.revision>s.revision)return foundation::Result<void>::Failure(E("gameplay.environment.restore_invalid","snapshot contains non-persistent or future-revision layer"));if(auto valid=ValidateLayer(l);!valid)return foundation::Result<void>::Failure(E("gameplay.environment.restore_invalid","invalid layer in snapshot"));if(!rebuilt.emplace(l.id,l).second)return foundation::Result<void>::Failure(E("gameplay.environment.restore_invalid","duplicate layer in snapshot"));if(l.id.value.High()==s.ids.scope&&l.id.value.Low()>max_low)max_low=l.id.value.Low();}
    if(s.ids.next!=0&&s.ids.next<=max_low)return foundation::Result<void>::Failure(E("gameplay.environment.restore_invalid","environment id generator can reproduce restored ids"));layers_=std::move(rebuilt);ids_.Restore(s.ids);revision_=s.revision;changes_.clear();next_change_sequence_=1;last_change_sequence_=0;RebuildSpatialIndex();return foundation::Result<void>::Success();
}
std::vector<EnvironmentChange> EnvironmentService::ChangesSince(std::uint64_t x) const {std::vector<EnvironmentChange> out;std::copy_if(changes_.begin(),changes_.end(),std::back_inserter(out),[x](const auto&c){return c.sequence>x;});return out;}
EnvironmentDiagnostics EnvironmentService::GetDiagnostics() const noexcept {EnvironmentDiagnostics d;d.layers=layers_.size();for(const auto&[_,l]:layers_)if(l.persistence==EnvironmentPersistence::Persistent)++d.persistent_layers;d.samples=samples_;d.expired=expired_;d.changes=changes_.size();return d;}
} // namespace epidemic::gameplay::environment
