#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)
using namespace epidemic; using namespace epidemic::gameplay; using namespace epidemic::gameplay::runtime_bridge;
struct Backend final:IRuntimeBridgeBackend{
 uint64_t next=10;int env_calls=0;std::vector<runtime::physics::ContactEvent> contacts;
 foundation::Result<runtime::RuntimeObjectId> Materialize(runtime::PersistentObjectId)override{return foundation::Result<runtime::RuntimeObjectId>::Success(runtime::RuntimeObjectId{next++});}
 foundation::Result<void> Dematerialize(runtime::RuntimeObjectId)override{return foundation::Result<void>::Success();}
 foundation::Result<void> Destroy(runtime::RuntimeObjectId)override{return foundation::Result<void>::Success();}
 foundation::Result<void> ApplyImpulse(runtime::physics::PhysicsBodyHandle,runtime::Vec3)override{return foundation::Result<void>::Success();}
 foundation::Result<void> ProjectEnvironment(runtime::RegionId,const gameplay::environment::EnvironmentValues&,Revision)override{++env_calls;return foundation::Result<void>::Success();}
 foundation::Result<void> ProjectWorldAlteration(const WorldAlterationProjectionRequest&)override{return foundation::Result<void>::Success();}
 std::vector<runtime::physics::ContactEvent> ConsumeContacts()override{auto x=contacts;contacts.clear();return x;}
};
int main(){gameplay::entities::EntityService entities;gameplay::entities::EntityArchetypeDefinition a;a.id=gameplay::entities::EntityArchetypeId::FromString("test.entity");a.canonical_name="test.entity";CHECK(entities.RegisterArchetype(a));entities.Freeze();gameplay::entities::CreateEntityRequest creq;creq.archetype=a.id;auto cr=entities.Create(creq);CHECK(cr);auto obj=gameplay::entities::EntityService::ToGameplayObjectRef(cr.Value().id);Backend b;RuntimeBridgeService bridge(entities,b);MaterializeRequest mr;mr.object=obj;mr.persistent_id=runtime::PersistentObjectId{42};mr.gameplay_revision={1};CHECK(bridge.Enqueue(mr));auto pr=bridge.Process();CHECK(pr.materialized==1);auto binding=bridge.FindBinding(obj);CHECK(binding&&binding->generation.IsValid());auto gen=binding->generation;runtime::physics::PhysicsBodyHandle body{{77},1};CHECK(bridge.AttachPhysicsBody(obj,gen,body));b.contacts.push_back({body,{},runtime::Vec3{1.25f,2.0f,3.0f},{},4.5f,runtime::physics::PhysicsEventState::Begin});auto obs=bridge.CollectImpactObservations(GameplayTickId{7});CHECK(obs.size()==1);CHECK(obs[0].point.x_mm==1250);CHECK(obs[0].impulse_milli==4500);
 EnvironmentProjectionRequest er;er.region=runtime::RegionId{1};er.gameplay_revision={5};CHECK(bridge.Enqueue(er));CHECK(bridge.Enqueue(er));auto env_process=bridge.Process();CHECK(env_process.processed==2);CHECK(b.env_calls==1);
 DematerializeRequest dr;dr.object=obj;dr.generation=gen;CHECK(bridge.Enqueue(dr));auto destroy_process=bridge.Process();CHECK(destroy_process.destroyed==1);CHECK(bridge.FindBinding(obj)==nullptr);return 0;}
