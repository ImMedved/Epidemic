#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/Environment/environment.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include "Epidemic/GameFramework/World/world.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/World/world_services.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace epidemic::gameplay::runtime_bridge
{
enum class RuntimeBindingState { Materializing, Active, Dematerializing, Failed };
enum class RuntimeProjectionPriority { Critical=0, High=1, Normal=2, Background=3 };
struct RuntimeBindingGeneration { std::uint32_t value=0; [[nodiscard]] constexpr bool IsValid()const noexcept{return value!=0;} [[nodiscard]] constexpr bool operator==(const RuntimeBindingGeneration&)const noexcept=default; };
struct RuntimeBinding
{
    GameplayObjectRef gameplay_object{};
    runtime::RuntimeObjectId runtime_object{};
    std::optional<runtime::physics::PhysicsBodyHandle> physics_body{};
    RuntimeBindingGeneration generation{};
    RuntimeBindingState state=RuntimeBindingState::Materializing;
    Revision gameplay_revision{};
};
struct MaterializeRequest { GameplayObjectRef object{}; runtime::PersistentObjectId persistent_id{}; Revision gameplay_revision{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; GameplayContext context{}; };
struct DematerializeRequest { GameplayObjectRef object{}; RuntimeBindingGeneration generation{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; GameplayContext context{}; };
struct DestroyRuntimeRequest { GameplayObjectRef object{}; RuntimeBindingGeneration generation{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; GameplayContext context{}; };
struct ImpulseProjectionRequest { GameplayObjectRef object{}; RuntimeBindingGeneration generation{}; runtime::Vec3 impulse{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::High; GameplayContext context{}; };
struct EnvironmentProjectionRequest { runtime::RegionId region{}; environment::EnvironmentValues values{}; Revision gameplay_revision{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; GameplayContext context{}; };
struct WorldAlterationProjectionRequest { world::WorldAlterationId alteration{}; world::WorldAlterationTypeId type{}; world::WorldAabb area{}; std::vector<std::byte> payload; Revision gameplay_revision{}; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; GameplayContext context{}; };
using RuntimeProjectionRequest=std::variant<MaterializeRequest,DematerializeRequest,DestroyRuntimeRequest,ImpulseProjectionRequest,EnvironmentProjectionRequest,WorldAlterationProjectionRequest>;

struct RuntimeBridgeBudget { std::uint32_t max_commands=4096,max_materializations=128,max_observations=8192; };
struct RuntimeBridgeProcessResult { std::uint32_t processed=0,deferred=0,failed=0,materialized=0,destroyed=0; };
struct SemanticImpactObservation
{
    GameplayObjectRef subject{},other{};
    RuntimeBindingGeneration subject_generation{},other_generation{};
    world::WorldPosition point{};
    std::int64_t impulse_milli=0;
    GameplayTickId observed_tick{};
};
struct RuntimeBridgeDiagnostics { std::uint64_t active_bindings=0,projection_requests=0,projection_failures=0,projection_backlog=0,observations=0,stale_observations=0,materializations=0,dematerializations=0; };

class IRuntimeBridgeBackend
{
public:
    virtual ~IRuntimeBridgeBackend()=default;
    [[nodiscard]] virtual foundation::Result<runtime::RuntimeObjectId> Materialize(runtime::PersistentObjectId persistent_id)=0;
    [[nodiscard]] virtual foundation::Result<void> Dematerialize(runtime::RuntimeObjectId object)=0;
    [[nodiscard]] virtual foundation::Result<void> Destroy(runtime::RuntimeObjectId object)=0;
    [[nodiscard]] virtual foundation::Result<void> ApplyImpulse(runtime::physics::PhysicsBodyHandle body,runtime::Vec3 impulse)=0;
    [[nodiscard]] virtual foundation::Result<void> ProjectEnvironment(runtime::RegionId region,const environment::EnvironmentValues& values,Revision revision)=0;
    [[nodiscard]] virtual foundation::Result<void> ProjectWorldAlteration(const WorldAlterationProjectionRequest& request)=0;
    [[nodiscard]] virtual std::vector<runtime::physics::ContactEvent> ConsumeContacts()=0;
};

class EngineRuntimeBridgeBackend final : public IRuntimeBridgeBackend
{
public:
    EngineRuntimeBridgeBackend(runtime::WorldServices world,runtime::physics::PhysicsServices physics,std::shared_ptr<runtime::IEnvironmentRuntime> environment)
        : world_(std::move(world)),physics_(std::move(physics)),environment_(std::move(environment)){}
    [[nodiscard]] foundation::Result<runtime::RuntimeObjectId> Materialize(runtime::PersistentObjectId persistent_id)override;
    [[nodiscard]] foundation::Result<void> Dematerialize(runtime::RuntimeObjectId object)override;
    [[nodiscard]] foundation::Result<void> Destroy(runtime::RuntimeObjectId object)override;
    [[nodiscard]] foundation::Result<void> ApplyImpulse(runtime::physics::PhysicsBodyHandle body,runtime::Vec3 impulse)override;
    [[nodiscard]] foundation::Result<void> ProjectEnvironment(runtime::RegionId region,const environment::EnvironmentValues& values,Revision revision)override;
    [[nodiscard]] foundation::Result<void> ProjectWorldAlteration(const WorldAlterationProjectionRequest& request)override;
    [[nodiscard]] std::vector<runtime::physics::ContactEvent> ConsumeContacts()override;
private:
    runtime::WorldServices world_; runtime::physics::PhysicsServices physics_; std::shared_ptr<runtime::IEnvironmentRuntime> environment_;
};

class RuntimeBridgeService
{
public:
    RuntimeBridgeService(entities::EntityService& entities,IRuntimeBridgeBackend& backend):entities_(entities),backend_(backend){}
    [[nodiscard]] foundation::Result<void> Enqueue(RuntimeProjectionRequest request);
    [[nodiscard]] foundation::Result<void> AttachPhysicsBody(GameplayObjectRef object,RuntimeBindingGeneration generation,runtime::physics::PhysicsBodyHandle body);
    [[nodiscard]] RuntimeBridgeProcessResult Process(RuntimeBridgeBudget budget={});
    [[nodiscard]] std::vector<SemanticImpactObservation> CollectImpactObservations(GameplayTickId tick,RuntimeBridgeBudget budget={});
    [[nodiscard]] const RuntimeBinding* FindBinding(GameplayObjectRef object)const noexcept;
    [[nodiscard]] const RuntimeBinding* FindBinding(runtime::RuntimeObjectId object)const noexcept;
    [[nodiscard]] RuntimeBridgeDiagnostics GetDiagnostics()const noexcept;
private:
    struct RefHash{std::size_t operator()(GameplayObjectRef r)const noexcept{return std::hash<GameplayObjectRef>{}(r);}};
    struct RuntimeHash{std::size_t operator()(runtime::RuntimeObjectId r)const noexcept{return std::hash<runtime::RuntimeObjectId>{}(r);}};
    struct BodyHash{std::size_t operator()(runtime::physics::PhysicsBodyHandle b)const noexcept{return std::hash<runtime::physics::PhysicsBodyHandle>{}(b);}};
    struct Queued { std::uint64_t sequence=0; RuntimeProjectionPriority priority=RuntimeProjectionPriority::Normal; RuntimeProjectionRequest request; };
    [[nodiscard]] foundation::Result<void> ProcessOne(const RuntimeProjectionRequest& request,RuntimeBridgeProcessResult& result);
    static RuntimeProjectionPriority PriorityOf(const RuntimeProjectionRequest& request);
    static world::WorldPosition Quantize(runtime::Vec3 p) noexcept;
    void RemoveBinding(GameplayObjectRef object);
    entities::EntityService& entities_; IRuntimeBridgeBackend& backend_;
    std::unordered_map<GameplayObjectRef,RuntimeBinding,RefHash> bindings_;
    std::unordered_map<runtime::RuntimeObjectId,GameplayObjectRef,RuntimeHash> reverse_;
    std::unordered_map<runtime::physics::PhysicsBodyHandle,GameplayObjectRef,BodyHash> body_reverse_;
    std::unordered_map<GameplayObjectRef,std::uint32_t,RefHash> generations_;
    std::unordered_map<std::uint64_t,Revision> environment_projection_revision_;
    struct AlterationHash{std::size_t operator()(world::WorldAlterationId id)const noexcept{return std::hash<GameplayObjectId>{}(id.value);}};
    std::unordered_map<world::WorldAlterationId,Revision,AlterationHash> world_projection_revision_;
    std::vector<Queued> queue_; std::uint64_t next_sequence_=1;
    std::uint64_t projection_requests_=0,projection_failures_=0,observations_=0,stale_observations_=0,materializations_=0,dematerializations_=0;
};
} // namespace epidemic::gameplay::runtime_bridge
