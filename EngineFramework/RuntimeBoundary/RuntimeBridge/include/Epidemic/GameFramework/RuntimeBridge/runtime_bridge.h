#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

namespace epidemic::gameplay::runtime_bridge
{
struct RuntimeObjectHandle
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimeObjectHandle&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RuntimeObjectHandle&) const noexcept = default;
};
struct RuntimePersistentObjectHandle
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimePersistentObjectHandle&) const noexcept = default;
};
struct RuntimePhysicsBodyHandle
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimePhysicsBodyHandle&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RuntimePhysicsBodyHandle&) const noexcept = default;
};
struct RuntimeRegionHandle
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimeRegionHandle&) const noexcept = default;
};
struct RuntimeVector3
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    [[nodiscard]] constexpr bool operator==(const RuntimeVector3&) const noexcept = default;
};
struct RuntimeQuaternion
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    [[nodiscard]] constexpr bool operator==(const RuntimeQuaternion&) const noexcept = default;
};
struct RuntimeWorldPosition
{
    std::int64_t x_mm = 0, y_mm = 0, z_mm = 0;
    [[nodiscard]] constexpr bool operator==(const RuntimeWorldPosition&) const noexcept = default;
};
struct RuntimeWorldAabb
{
    RuntimeWorldPosition min{}, max{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return min.x_mm <= max.x_mm && min.y_mm <= max.y_mm && min.z_mm <= max.z_mm;
    }
};

struct RuntimeEnvironmentValues
{
    std::int32_t temperature_milli_c = 0;
    std::int32_t humidity_milli = 0;
    std::int32_t precipitation_milli = 0;
    std::int32_t wind_strength_milli = 0;
    std::int32_t visibility_milli = 1000;
    std::int32_t light_exposure_milli = 1000;
};

enum class RuntimeBindingState
{
    Materializing,
    Active,
    Dematerializing,
    Failed
};
enum class RuntimeProjectionPriority
{
    Critical = 0,
    High = 1,
    Normal = 2,
    Background = 3
};
struct RuntimeBindingGeneration
{
    std::uint32_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimeBindingGeneration&) const noexcept = default;
};
struct RuntimeTransformObservation
{
    RuntimeWorldPosition position{};
    RuntimeQuaternion rotation{};
    RuntimeVector3 scale{1.0f, 1.0f, 1.0f};
    RuntimeRegionHandle region{};
    RuntimeBindingGeneration generation{};
};
struct RuntimePhysicsAttachment
{
    RuntimePhysicsBodyHandle body{};
    GameplayObjectPartRef part{}; // invalid part means the body represents the whole object
    [[nodiscard]] constexpr bool operator==(const RuntimePhysicsAttachment&) const noexcept = default;
};
struct RuntimeBinding
{
    GameplayObjectRef gameplay_object{};
    RuntimePersistentObjectHandle persistent_object{};
    RuntimeObjectHandle runtime_object{};
    std::vector<RuntimePhysicsAttachment> physics_attachments;
    RuntimeBindingGeneration generation{};
    RuntimeBindingState state = RuntimeBindingState::Materializing;
    Revision gameplay_revision{};
};
struct MaterializeRequest
{
    GameplayObjectRef object{};
    RuntimePersistentObjectHandle persistent_id{};
    Revision gameplay_revision{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
    GameplayContext context{};
};
struct DematerializeRequest
{
    GameplayObjectRef object{};
    RuntimeBindingGeneration generation{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
    GameplayContext context{};
};
struct DestroyRuntimeRequest
{
    GameplayObjectRef object{};
    RuntimeBindingGeneration generation{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
    GameplayContext context{};
};
struct ImpulseProjectionRequest
{
    GameplayObjectRef object{};
    GameplayObjectPartRef part{};
    RuntimeBindingGeneration generation{};
    RuntimeVector3 impulse{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::High;
    GameplayContext context{};
};
struct EnvironmentProjectionRequest
{
    RuntimeRegionHandle region{};
    RuntimeEnvironmentValues values{};
    Revision gameplay_revision{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
    GameplayContext context{};
};
struct WorldAlterationProjectionRequest
{
    GameplayObjectId alteration{};
    TypeId type{};
    RuntimeWorldAabb area{};
    std::vector<std::byte> payload;
    Revision gameplay_revision{};
    RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
    GameplayContext context{};
};
using RuntimeProjectionRequest = std::variant<MaterializeRequest,
                                              DematerializeRequest,
                                              DestroyRuntimeRequest,
                                              ImpulseProjectionRequest,
                                              EnvironmentProjectionRequest,
                                              WorldAlterationProjectionRequest>;

struct RuntimeBridgeBudget
{
    std::uint32_t max_commands = 4096;
    std::uint32_t max_materializations = 128;
    std::uint32_t max_observations = 8192;
};
struct RuntimeBridgeQueuePolicy
{
    std::size_t max_projection_requests = 16384;
    std::size_t max_contact_backlog = 32768;
};
struct RuntimeBridgeCapabilities
{
    bool environment_temperature = true;
    bool environment_humidity = true;
    bool environment_precipitation = true;
    bool environment_wind = true;
    bool environment_visibility = true;
    bool environment_light_exposure = true;
    bool world_alteration_projection = false;
};
struct RuntimeBridgeProcessResult
{
    std::uint32_t processed = 0, deferred = 0, failed = 0, materialized = 0, dematerialized = 0, destroyed = 0;
};
struct RuntimeContactObservation
{
    RuntimePhysicsBodyHandle a{}, b{};
    RuntimeVector3 point{};
    float impulse = 0.0f;
};
struct SemanticImpactObservation
{
    GameplayObjectRef subject{}, other{};
    GameplayObjectPartRef subject_part{}, other_part{};
    RuntimeBindingGeneration subject_generation{}, other_generation{};
    RuntimeWorldPosition point{};
    std::int64_t impulse_milli = 0;
    GameplayTickId observed_tick{};
};
struct RuntimeBridgeDiagnostics
{
    std::uint64_t active_bindings = 0, projection_requests = 0, projection_failures = 0, projection_backlog = 0,
                  observations = 0, stale_observations = 0, materializations = 0, dematerializations = 0,
                  coalesced_projection_requests = 0, rejected_projection_requests = 0, dropped_contacts = 0,
                  contact_backlog = 0;
};

struct RuntimeMaterializationResult
{
    RuntimeObjectHandle object{};
    bool created = true;
};

struct RuntimeRayQuery
{
    RuntimeVector3 origin{};
    RuntimeVector3 direction{};
    float max_distance = 0.0f;
};
struct RuntimeRayHit
{
    RuntimePhysicsBodyHandle body{};
    RuntimeVector3 point{};
    RuntimeVector3 normal{};
    float distance = 0.0f;
};
struct SemanticRayHit
{
    GameplayObjectRef object{};
    GameplayObjectPartRef part{};
    RuntimeBindingGeneration generation{};
    RuntimeWorldPosition point{};
    std::int64_t distance_milli = 0;
};
struct RuntimeOverlapQuery
{
    RuntimeWorldAabb bounds{};
};
struct SemanticOverlapHit
{
    GameplayObjectRef object{};
    GameplayObjectPartRef part{};
    RuntimeBindingGeneration generation{};
};
struct RuntimeNavigationQueryHandle
{
    std::uint64_t value = 0;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const RuntimeNavigationQueryHandle&) const noexcept = default;
};
enum class RuntimeNavigationPathState
{
    Pending,
    Running,
    PartiallyComplete,
    Completed,
    Failed,
    Cancelled,
    Stale,
};
struct RuntimeNavigationPathQuery
{
    RuntimeWorldPosition from{};
    RuntimeWorldPosition to{};
    RuntimeRegionHandle region{};
    std::uint64_t source_revision = 0;
};
struct RuntimeNavigationPath
{
    RuntimeNavigationPathState state = RuntimeNavigationPathState::Pending;
    std::vector<RuntimeWorldPosition> points;
    std::uint64_t navigation_revision = 0;
    std::uint64_t result_revision = 0;
    [[nodiscard]] bool IsComplete() const noexcept { return state == RuntimeNavigationPathState::Completed; }
};
struct RuntimeEnvironmentSample
{
    RuntimeEnvironmentValues values{};
    Revision revision{};
};

class IRuntimeBridgeBackend
{
  public:
    virtual ~IRuntimeBridgeBackend() = default;
    [[nodiscard]] virtual foundation::Result<RuntimeMaterializationResult> Materialize(RuntimePersistentObjectHandle persistent_id) = 0;
    [[nodiscard]] virtual foundation::Result<void> Dematerialize(RuntimeObjectHandle object) = 0;
    [[nodiscard]] virtual foundation::Result<void> Destroy(RuntimeObjectHandle object) = 0;
    [[nodiscard]] virtual foundation::Result<void> ApplyImpulse(RuntimePhysicsBodyHandle body, RuntimeVector3 impulse) = 0;
    [[nodiscard]] virtual foundation::Result<void> ProjectEnvironment(
        RuntimeRegionHandle region, const RuntimeEnvironmentValues& values, Revision revision) = 0;
    [[nodiscard]] virtual RuntimeBridgeCapabilities GetCapabilities() const noexcept { return {}; }
    [[nodiscard]] virtual bool SupportsWorldAlterationProjection() const noexcept { return GetCapabilities().world_alteration_projection; }
    [[nodiscard]] virtual foundation::Result<void> ProjectWorldAlteration(const WorldAlterationProjectionRequest&)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.world_projection_unsupported",
                                      "runtime backend does not support generic world alteration projection"));
    }
    [[nodiscard]] virtual std::vector<RuntimeContactObservation> ConsumeContacts() = 0;

    [[nodiscard]] virtual foundation::Result<std::vector<RuntimeRayHit>> Raycast(const RuntimeRayQuery&) const
    {
        return foundation::Result<std::vector<RuntimeRayHit>>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.raycast_unsupported", "runtime backend does not expose ray queries"));
    }
    [[nodiscard]] virtual foundation::Result<std::vector<RuntimePhysicsBodyHandle>> Overlap(const RuntimeOverlapQuery&) const
    {
        return foundation::Result<std::vector<RuntimePhysicsBodyHandle>>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.overlap_unsupported", "runtime backend does not expose overlap queries"));
    }
    [[nodiscard]] virtual foundation::Result<bool> Visible(RuntimeObjectHandle, RuntimeObjectHandle) const
    {
        return foundation::Result<bool>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.visibility_unsupported", "runtime backend does not expose visibility queries"));
    }
    [[nodiscard]] virtual foundation::Result<RuntimeTransformObservation> ObserveTransform(RuntimeObjectHandle) const
    {
        return foundation::Result<RuntimeTransformObservation>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.transform_unsupported", "runtime backend does not expose object transforms"));
    }
    [[nodiscard]] virtual foundation::Result<bool> IsRepresentationAlive(RuntimeObjectHandle) const
    {
        return foundation::Result<bool>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.reconciliation_unsupported", "runtime backend does not expose representation liveness"));
    }
    [[nodiscard]] virtual foundation::Result<RuntimeNavigationQueryHandle> RequestPath(const RuntimeNavigationPathQuery&)
    {
        return foundation::Result<RuntimeNavigationQueryHandle>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.navigation_unsupported", "runtime backend does not expose navigation queries"));
    }
    [[nodiscard]] virtual foundation::Result<RuntimeNavigationPathState> GetPathState(RuntimeNavigationQueryHandle) const
    {
        return foundation::Result<RuntimeNavigationPathState>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.navigation_unsupported", "runtime backend does not expose navigation queries"));
    }
    [[nodiscard]] virtual foundation::Result<RuntimeNavigationPath> GetPathResult(RuntimeNavigationQueryHandle) const
    {
        return foundation::Result<RuntimeNavigationPath>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.navigation_unsupported", "runtime backend does not expose navigation queries"));
    }
    [[nodiscard]] virtual foundation::Result<void> CancelPath(RuntimeNavigationQueryHandle)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.navigation_unsupported", "runtime backend does not expose navigation queries"));
    }
    [[nodiscard]] virtual foundation::Result<void> ReleasePath(RuntimeNavigationQueryHandle)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.navigation_unsupported", "runtime backend does not expose navigation queries"));
    }
    [[nodiscard]] virtual foundation::Result<RuntimeEnvironmentSample> SampleEnvironment(RuntimeRegionHandle) const
    {
        return foundation::Result<RuntimeEnvironmentSample>::Failure(
            foundation::Error::Create("gameplay.runtime_bridge.environment_sample_unsupported",
                                      "runtime backend does not expose environment sampling"));
    }
};

class RuntimeBridgeService
{
  public:
    explicit RuntimeBridgeService(IRuntimeBridgeBackend& backend, RuntimeBridgeQueuePolicy queue_policy = {})
        : backend_(backend), queue_policy_(queue_policy) {}
    [[nodiscard]] foundation::Result<void> Enqueue(RuntimeProjectionRequest request);
    [[nodiscard]] foundation::Result<void> AttachPhysicsBody(
        GameplayObjectRef object,
        RuntimeBindingGeneration generation,
        RuntimePhysicsBodyHandle body,
        GameplayObjectPartRef part = {});
    [[nodiscard]] RuntimeBridgeProcessResult Process(RuntimeBridgeBudget budget = {});
    [[nodiscard]] std::vector<SemanticImpactObservation> CollectImpactObservations(GameplayTickId tick, RuntimeBridgeBudget budget = {});
    [[nodiscard]] std::optional<RuntimeBinding> GetBinding(GameplayObjectRef object) const noexcept;
    [[nodiscard]] std::optional<RuntimeBinding> GetBinding(RuntimeObjectHandle object) const noexcept;

    [[nodiscard]] foundation::Result<std::vector<SemanticRayHit>> Raycast(const RuntimeRayQuery& query) const;
    [[nodiscard]] foundation::Result<std::vector<SemanticOverlapHit>> Overlap(const RuntimeOverlapQuery& query) const;
    [[nodiscard]] foundation::Result<bool> Visible(GameplayObjectRef observer, GameplayObjectRef target) const;
    [[nodiscard]] foundation::Result<RuntimeTransformObservation> ObserveTransform(GameplayObjectRef object) const;
    [[nodiscard]] foundation::Result<void> ForgetObjectIdentity(GameplayObjectRef object);
    [[nodiscard]] foundation::Result<void> ForgetEnvironmentProjection(RuntimeRegionHandle region);
    [[nodiscard]] foundation::Result<void> ForgetWorldAlterationProjection(GameplayObjectId alteration);
    [[nodiscard]] RuntimeBridgeCapabilities GetCapabilities() const noexcept { return backend_.GetCapabilities(); }
    // Reconciles bindings whose Runtime representation disappeared independently (streaming/runtime-side teardown).
    // Returned objects were removed from the active binding map and can be rematerialized by gameplay orchestration.
    [[nodiscard]] foundation::Result<std::vector<GameplayObjectRef>> ReconcileBindings();
    [[nodiscard]] foundation::Result<RuntimeNavigationQueryHandle> RequestPath(const RuntimeNavigationPathQuery& query)
    {
        return backend_.RequestPath(query);
    }
    [[nodiscard]] foundation::Result<RuntimeNavigationPathState> GetPathState(RuntimeNavigationQueryHandle handle) const
    {
        return backend_.GetPathState(handle);
    }
    [[nodiscard]] foundation::Result<RuntimeNavigationPath> GetPathResult(RuntimeNavigationQueryHandle handle) const
    {
        return backend_.GetPathResult(handle);
    }
    [[nodiscard]] foundation::Result<void> CancelPath(RuntimeNavigationQueryHandle handle) { return backend_.CancelPath(handle); }
    [[nodiscard]] foundation::Result<void> ReleasePath(RuntimeNavigationQueryHandle handle) { return backend_.ReleasePath(handle); }
    [[nodiscard]] foundation::Result<RuntimeEnvironmentSample> SampleEnvironment(RuntimeRegionHandle region) const
    {
        return backend_.SampleEnvironment(region);
    }
    [[nodiscard]] RuntimeBridgeDiagnostics GetDiagnostics() const noexcept;

  private:
    struct RefHash
    {
        std::size_t operator()(GameplayObjectRef r) const noexcept { return std::hash<GameplayObjectRef>{}(r); }
    };
    struct RuntimeHash
    {
        std::size_t operator()(RuntimeObjectHandle r) const noexcept { return std::hash<std::uint64_t>{}(r.value); }
    };
    struct BodyHash
    {
        std::size_t operator()(RuntimePhysicsBodyHandle b) const noexcept { return std::hash<std::uint64_t>{}(b.value); }
    };
    struct BodyOwner
    {
        GameplayObjectRef object{};
        GameplayObjectPartRef part{};
    };
    struct Queued
    {
        std::uint64_t sequence = 0;
        RuntimeProjectionPriority priority = RuntimeProjectionPriority::Normal;
        RuntimeProjectionRequest request;
    };
    [[nodiscard]] foundation::Result<void> ProcessOne(const RuntimeProjectionRequest& request, RuntimeBridgeProcessResult& result);
    static RuntimeProjectionPriority PriorityOf(const RuntimeProjectionRequest& request);
    static RuntimeWorldPosition Quantize(RuntimeVector3 p) noexcept;
    void RemoveBinding(GameplayObjectRef object);

    IRuntimeBridgeBackend& backend_;
    RuntimeBridgeQueuePolicy queue_policy_{};
    std::unordered_map<GameplayObjectRef, RuntimeBinding, RefHash> bindings_;
    std::unordered_map<RuntimeObjectHandle, GameplayObjectRef, RuntimeHash> reverse_;
    std::unordered_map<RuntimePhysicsBodyHandle, BodyOwner, BodyHash> body_reverse_;
    std::unordered_map<GameplayObjectRef, std::uint32_t, RefHash> generations_;
    std::unordered_map<std::uint64_t, Revision> environment_projection_revision_;
    std::unordered_map<GameplayObjectId, Revision> world_projection_revision_;
    std::vector<Queued> queue_;
    std::vector<RuntimeContactObservation> contact_backlog_;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t projection_requests_ = 0, projection_failures_ = 0, observations_ = 0, stale_observations_ = 0,
                  materializations_ = 0, dematerializations_ = 0, coalesced_projection_requests_ = 0,
                  rejected_projection_requests_ = 0, dropped_contacts_ = 0;
};
} // namespace epidemic::gameplay::runtime_bridge

namespace std
{
template <> struct hash<epidemic::gameplay::runtime_bridge::RuntimeObjectHandle>
{
    std::size_t operator()(epidemic::gameplay::runtime_bridge::RuntimeObjectHandle value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.value);
    }
};
template <> struct hash<epidemic::gameplay::runtime_bridge::RuntimePersistentObjectHandle>
{
    std::size_t operator()(epidemic::gameplay::runtime_bridge::RuntimePersistentObjectHandle value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.value);
    }
};
template <> struct hash<epidemic::gameplay::runtime_bridge::RuntimeRegionHandle>
{
    std::size_t operator()(epidemic::gameplay::runtime_bridge::RuntimeRegionHandle value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.value);
    }
};
template <> struct hash<epidemic::gameplay::runtime_bridge::RuntimePhysicsBodyHandle>
{
    std::size_t operator()(epidemic::gameplay::runtime_bridge::RuntimePhysicsBodyHandle value) const noexcept
    {
        return std::hash<std::uint64_t>{}(value.value);
    }
};
} // namespace std
