#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/World/world_commands.h"

#include <algorithm>
#include <cmath>

namespace epidemic::gameplay::runtime_bridge
{
namespace
{
foundation::Error E(std::string_view c,std::string_view m){return foundation::Error::Create(c,m);}
std::int64_t Milli(float value) noexcept { return static_cast<std::int64_t>(std::llround(static_cast<double>(value)*1000.0)); }
}
foundation::Result<runtime::RuntimeObjectId> EngineRuntimeBridgeBackend::Materialize(runtime::PersistentObjectId persistent_id)
{
    if(!world_.materialization) return foundation::Result<runtime::RuntimeObjectId>::Failure(E("gameplay.runtime_bridge.world_unavailable","runtime world materializer is unavailable"));
    return world_.materialization->Materialize(runtime::MaterializationRequest{persistent_id,runtime::ObjectRealityLevel::Physical});
}
foundation::Result<void> EngineRuntimeBridgeBackend::Dematerialize(runtime::RuntimeObjectId object)
{
    if(!world_.query||!world_.materialization||!world_.demotion_authority) return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.world_unavailable","runtime dematerialization services are unavailable"));
    auto record=world_.query->FindObject(object);
    if(!record) return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.runtime_object_missing","runtime object is missing"));
    runtime::DemotionSnapshot snapshot; snapshot.object=object; snapshot.target_reality=runtime::ObjectRealityLevel::Logical; snapshot.collapsed_placement=record->placement; snapshot.collapse_record_id=foundation::StringId::FromString("framework.runtime_bridge.dematerialize"); snapshot.source_revision=record->revision;
    auto token=world_.demotion_authority->IssueDemotionCommitToken(snapshot); if(!token)return foundation::Result<void>::Failure(token.GetError());
    auto result=world_.materialization->Demote(runtime::DemotionRequest{object,runtime::ObjectRealityLevel::Logical,token.Value()});
    if(!result){(void)world_.demotion_authority->RevokeDemotionCommitToken(token.Value().token_id);return result;}
    return foundation::Result<void>::Success();
}
foundation::Result<void> EngineRuntimeBridgeBackend::Destroy(runtime::RuntimeObjectId object)
{
    if(!world_.writer) return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.world_unavailable","runtime world writer is unavailable"));
    std::uint64_t revision=0;
    if(world_.query){ auto record=world_.query->FindObject(object); if(record) revision=record->revision; }
    auto r=world_.writer->Apply(runtime::DestroyObjectCommand{object,revision,runtime::GameTimePoint{},foundation::StringId::FromString("framework.runtime_bridge.destroy")});
    if(!r) return foundation::Result<void>::Failure(r.GetError());
    return foundation::Result<void>::Success();
}
foundation::Result<void> EngineRuntimeBridgeBackend::ApplyImpulse(runtime::physics::PhysicsBodyHandle body,runtime::Vec3 impulse)
{
    if(!physics_.scene) return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.physics_unavailable","runtime physics scene is unavailable"));
    return physics_.scene->ApplyImpulse(body,impulse);
}
foundation::Result<void> EngineRuntimeBridgeBackend::ProjectEnvironment(runtime::RegionId region,const environment::EnvironmentValues& v,Revision)
{
    if(!environment_) return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.environment_unavailable","runtime environment is unavailable"));
    runtime::WeatherState weather{};
    weather.precipitation=static_cast<float>(v.precipitation)/environment::kEnvironmentOne;
    weather.intensity=weather.precipitation;
    weather.wind_speed=static_cast<float>(v.wind_strength)/environment::kEnvironmentOne;
    weather.current_temperature=static_cast<float>(v.temperature_milli_c)/1000.0f;
    weather.current_humidity=static_cast<float>(v.humidity)/environment::kEnvironmentOne;
    if(weather.precipitation>0.75f) weather.kind=runtime::WeatherKind::Storm;
    else if(weather.precipitation>0.05f) weather.kind=runtime::WeatherKind::Rain;
    else weather.kind=runtime::WeatherKind::Clear;
    return environment_->SetWeather(region,weather);
}
foundation::Result<void> EngineRuntimeBridgeBackend::ProjectWorldAlteration(const WorldAlterationProjectionRequest&)
{
    return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.world_projection_unsupported","frozen Runtime exposes no generic terrain/structural alteration command"));
}
std::vector<runtime::physics::ContactEvent> EngineRuntimeBridgeBackend::ConsumeContacts()
{
    if(!physics_.events) return {};
    const auto contacts=physics_.events->Contacts();
    std::vector<runtime::physics::ContactEvent> out(contacts.begin(),contacts.end());
    physics_.events->Clear();
    return out;
}

RuntimeProjectionPriority RuntimeBridgeService::PriorityOf(const RuntimeProjectionRequest&r){return std::visit([](const auto&x){return x.priority;},r);}
foundation::Result<void> RuntimeBridgeService::Enqueue(RuntimeProjectionRequest r){queue_.push_back({next_sequence_++,PriorityOf(r),std::move(r)});++projection_requests_;return foundation::Result<void>::Success();}
const RuntimeBinding* RuntimeBridgeService::FindBinding(GameplayObjectRef o)const noexcept{auto i=bindings_.find(o);return i==bindings_.end()?nullptr:&i->second;}
const RuntimeBinding* RuntimeBridgeService::FindBinding(runtime::RuntimeObjectId o)const noexcept{auto r=reverse_.find(o);return r==reverse_.end()?nullptr:FindBinding(r->second);}
void RuntimeBridgeService::RemoveBinding(GameplayObjectRef o){auto i=bindings_.find(o);if(i==bindings_.end())return;if(i->second.physics_body)body_reverse_.erase(*i->second.physics_body);reverse_.erase(i->second.runtime_object);bindings_.erase(i);}
foundation::Result<void> RuntimeBridgeService::AttachPhysicsBody(GameplayObjectRef o,RuntimeBindingGeneration g,runtime::physics::PhysicsBodyHandle b){auto i=bindings_.find(o);if(i==bindings_.end()||i->second.generation!=g||!b.IsValid())return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale","binding missing or stale"));if(i->second.physics_body)body_reverse_.erase(*i->second.physics_body);i->second.physics_body=b;body_reverse_[b]=o;return foundation::Result<void>::Success();}
foundation::Result<void> RuntimeBridgeService::ProcessOne(const RuntimeProjectionRequest&r,RuntimeBridgeProcessResult& out)
{
    return std::visit([&](const auto&x)->foundation::Result<void>{using T=std::decay_t<decltype(x)>;
        if constexpr(std::is_same_v<T,MaterializeRequest>){
            auto existing=bindings_.find(x.object); if(existing!=bindings_.end()&&existing->second.gameplay_revision>=x.gameplay_revision)return foundation::Result<void>::Success();
            auto rr=backend_.Materialize(x.persistent_id); if(!rr)return foundation::Result<void>::Failure(rr.GetError());
            RemoveBinding(x.object); auto& gv=generations_[x.object]; ++gv;if(gv==0)++gv;
            RuntimeBinding b{x.object,rr.Value(),std::nullopt,RuntimeBindingGeneration{gv},RuntimeBindingState::Active,x.gameplay_revision};bindings_[x.object]=b;reverse_[b.runtime_object]=x.object;
            if(auto id=entities::EntityService::FromGameplayObjectRef(x.object);id.IsValid()){auto s=entities_.SetMaterializationState(id,entities::EntityMaterializationState::Materialized,x.context);if(!s)return s;}
            ++materializations_;++out.materialized;return foundation::Result<void>::Success();
        } else if constexpr(std::is_same_v<T,DematerializeRequest>){
            auto i=bindings_.find(x.object);if(i==bindings_.end()||i->second.generation!=x.generation)return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale","dematerialize binding is stale"));auto rr=backend_.Dematerialize(i->second.runtime_object);if(!rr)return rr;
            if(auto id=entities::EntityService::FromGameplayObjectRef(x.object);id.IsValid()){auto state=entities_.SetMaterializationState(id,entities::EntityMaterializationState::Abstract,x.context);if(!state)return state;}
            RemoveBinding(x.object);++dematerializations_;++out.destroyed;return foundation::Result<void>::Success();
        } else if constexpr(std::is_same_v<T,DestroyRuntimeRequest>){
            auto i=bindings_.find(x.object);if(i==bindings_.end()||i->second.generation!=x.generation)return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale","destroy binding is stale"));auto rr=backend_.Destroy(i->second.runtime_object);if(!rr)return rr;RemoveBinding(x.object);++out.destroyed;return foundation::Result<void>::Success();
        } else if constexpr(std::is_same_v<T,ImpulseProjectionRequest>){auto i=bindings_.find(x.object);if(i==bindings_.end()||i->second.generation!=x.generation||!i->second.physics_body)return foundation::Result<void>::Failure(E("gameplay.runtime_bridge.binding_stale","impulse binding is stale or has no physics body"));return backend_.ApplyImpulse(*i->second.physics_body,x.impulse);
        } else if constexpr(std::is_same_v<T,EnvironmentProjectionRequest>){auto& rev=environment_projection_revision_[x.region.Raw()];if(rev>=x.gameplay_revision&&x.gameplay_revision.value!=0)return foundation::Result<void>::Success();auto rr=backend_.ProjectEnvironment(x.region,x.values,x.gameplay_revision);if(rr)rev=x.gameplay_revision;return rr;
        } else {auto& rev=world_projection_revision_[x.alteration];if(rev>=x.gameplay_revision&&x.gameplay_revision.value!=0)return foundation::Result<void>::Success();auto rr=backend_.ProjectWorldAlteration(x);if(rr)rev=x.gameplay_revision;return rr;}
    },r);
}
RuntimeBridgeProcessResult RuntimeBridgeService::Process(RuntimeBridgeBudget budget){std::stable_sort(queue_.begin(),queue_.end(),[](const Queued&a,const Queued&b){if(a.priority!=b.priority)return a.priority<b.priority;return a.sequence<b.sequence;});RuntimeBridgeProcessResult out;std::vector<Queued> remain;std::uint32_t mats=0;for(auto&q:queue_){bool is_mat=std::holds_alternative<MaterializeRequest>(q.request);if(out.processed>=budget.max_commands||(is_mat&&mats>=budget.max_materializations)){remain.push_back(std::move(q));++out.deferred;continue;}auto r=ProcessOne(q.request,out);++out.processed;if(is_mat)++mats;if(!r){++out.failed;++projection_failures_;}}queue_=std::move(remain);return out;}
world::WorldPosition RuntimeBridgeService::Quantize(runtime::Vec3 p)noexcept{return {Milli(p.x),Milli(p.y),Milli(p.z)};}
std::vector<SemanticImpactObservation> RuntimeBridgeService::CollectImpactObservations(GameplayTickId tick,RuntimeBridgeBudget budget){auto contacts=backend_.ConsumeContacts();std::vector<SemanticImpactObservation> out;out.reserve(std::min<std::size_t>(contacts.size(),budget.max_observations));for(const auto&c:contacts){if(out.size()>=budget.max_observations)break;auto ia=body_reverse_.find(c.a),ib=body_reverse_.find(c.b);if(ia==body_reverse_.end()&&ib==body_reverse_.end()){++stale_observations_;continue;}SemanticImpactObservation o;o.observed_tick=tick;o.point=Quantize(c.point);o.impulse_milli=Milli(c.impulse);if(ia!=body_reverse_.end()){o.subject=ia->second;if(auto*b=FindBinding(o.subject))o.subject_generation=b->generation;}if(ib!=body_reverse_.end()){o.other=ib->second;if(auto*b=FindBinding(o.other))o.other_generation=b->generation;}out.push_back(o);++observations_;}return out;}
RuntimeBridgeDiagnostics RuntimeBridgeService::GetDiagnostics()const noexcept{return {bindings_.size(),projection_requests_,projection_failures_,queue_.size(),observations_,stale_observations_,materializations_,dematerializations_};}
} // namespace epidemic::gameplay::runtime_bridge
