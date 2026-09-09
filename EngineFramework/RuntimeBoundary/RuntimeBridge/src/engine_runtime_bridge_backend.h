#pragma once

#include "Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Navigation/navigation_runtime.h"
#include "Epidemic/Runtime/World/world_services.h"

#include <memory>
#include <unordered_map>

namespace epidemic::gameplay::runtime_bridge
{
class EngineRuntimeBridgeBackend final : public IRuntimeBridgeBackend
{
  public:
    EngineRuntimeBridgeBackend(runtime::WorldServices world,
                               runtime::physics::PhysicsServices physics,
                               std::shared_ptr<runtime::IEnvironmentRuntime> environment,
                               runtime::navigation::NavigationServices navigation = {})
        : world_(std::move(world)), physics_(std::move(physics)), environment_(std::move(environment)),
          navigation_(std::move(navigation))
    {
    }

    [[nodiscard]] RuntimePersistentObjectHandle ImportPersistentObjectId(runtime::PersistentObjectId id);
    [[nodiscard]] RuntimePhysicsBodyHandle ImportPhysicsBody(runtime::physics::PhysicsBodyHandle body);
    [[nodiscard]] RuntimeRegionHandle ImportRegion(runtime::RegionId region);
    [[nodiscard]] foundation::Result<void> ReleasePersistentObject(RuntimePersistentObjectHandle handle);
    [[nodiscard]] foundation::Result<void> ReleasePhysicsBody(RuntimePhysicsBodyHandle handle);
    [[nodiscard]] foundation::Result<void> ReleaseRegion(RuntimeRegionHandle handle);

    [[nodiscard]] foundation::Result<RuntimeMaterializationResult> Materialize(RuntimePersistentObjectHandle persistent_id) override;
    [[nodiscard]] foundation::Result<void> Dematerialize(RuntimeObjectHandle object) override;
    [[nodiscard]] foundation::Result<void> Destroy(RuntimeObjectHandle object) override;
    [[nodiscard]] foundation::Result<void> ApplyImpulse(RuntimePhysicsBodyHandle body, RuntimeVector3 impulse) override;
    [[nodiscard]] foundation::Result<void> ProjectEnvironment(
        RuntimeRegionHandle region, const RuntimeEnvironmentValues& values, Revision revision) override;
    [[nodiscard]] RuntimeBridgeCapabilities GetCapabilities() const noexcept override
    {
        RuntimeBridgeCapabilities capabilities;
        capabilities.environment_visibility = false;
        capabilities.environment_light_exposure = false;
        capabilities.world_alteration_projection = false;
        return capabilities;
    }
    [[nodiscard]] std::vector<RuntimeContactObservation> ConsumeContacts() override;
    [[nodiscard]] foundation::Result<std::vector<RuntimeRayHit>> Raycast(const RuntimeRayQuery& query) const override;
    [[nodiscard]] foundation::Result<std::vector<RuntimePhysicsBodyHandle>> Overlap(const RuntimeOverlapQuery& query) const override;
    [[nodiscard]] foundation::Result<bool> Visible(RuntimeObjectHandle observer, RuntimeObjectHandle target) const override;
    [[nodiscard]] foundation::Result<RuntimeTransformObservation> ObserveTransform(RuntimeObjectHandle object) const override;
    [[nodiscard]] foundation::Result<bool> IsRepresentationAlive(RuntimeObjectHandle object) const override;
    [[nodiscard]] foundation::Result<RuntimeNavigationQueryHandle> RequestPath(const RuntimeNavigationPathQuery& query) override;
    [[nodiscard]] foundation::Result<RuntimeNavigationPathState> GetPathState(RuntimeNavigationQueryHandle handle) const override;
    [[nodiscard]] foundation::Result<RuntimeNavigationPath> GetPathResult(RuntimeNavigationQueryHandle handle) const override;
    [[nodiscard]] foundation::Result<void> CancelPath(RuntimeNavigationQueryHandle handle) override;
    [[nodiscard]] foundation::Result<void> ReleasePath(RuntimeNavigationQueryHandle handle) override;
    [[nodiscard]] foundation::Result<RuntimeEnvironmentSample> SampleEnvironment(RuntimeRegionHandle region) const override;

  private:
    template <typename T> struct RuntimeHash
    {
        std::size_t operator()(const T& value) const noexcept { return std::hash<T>{}(value); }
    };

    runtime::WorldServices world_;
    runtime::physics::PhysicsServices physics_;
    std::shared_ptr<runtime::IEnvironmentRuntime> environment_;
    runtime::navigation::NavigationServices navigation_;

    std::uint64_t next_persistent_ = 1, next_object_ = 1, next_body_ = 1, next_region_ = 1, next_path_ = 1;
    std::unordered_map<RuntimePersistentObjectHandle, runtime::PersistentObjectId> persistent_objects_;
    std::unordered_map<RuntimeObjectHandle, runtime::RuntimeObjectId> runtime_objects_;
    std::unordered_map<runtime::RuntimeObjectId, RuntimeObjectHandle, RuntimeHash<runtime::RuntimeObjectId>> runtime_object_reverse_;
    std::unordered_map<RuntimePhysicsBodyHandle, runtime::physics::PhysicsBodyHandle> physics_bodies_;
    std::unordered_map<runtime::physics::PhysicsBodyHandle, RuntimePhysicsBodyHandle, RuntimeHash<runtime::physics::PhysicsBodyHandle>> physics_body_reverse_;
    std::unordered_map<RuntimeRegionHandle, runtime::RegionId> regions_;
    std::unordered_map<std::uint64_t, runtime::navigation::PathQueryHandle> paths_;
};
} // namespace epidemic::gameplay::runtime_bridge

