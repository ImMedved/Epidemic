#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Animation/animation_runtime.h"
#include "Epidemic/Runtime/Assets/asset_services.h"
#include "Epidemic/Runtime/Audio/audio_runtime.h"
#include "Epidemic/Runtime/Environment/environment_services.h"
#include "Epidemic/Runtime/Navigation/navigation_runtime.h"
#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Resources/resource_services.h"
#include "Epidemic/Runtime/Scene/scene_services.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"
#include "Epidemic/Runtime/Simulation/simulation_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Time/time_runtime.h"
#include "Epidemic/Runtime/World/world_services.h"

#include <Epidemic/Core/application.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace epidemic::runtime
{
struct RuntimeFoundationRegistration
{
    std::string name = "RuntimeFoundation";
};

enum class RuntimeProfile
{
    Reference,
    Production,
};

enum class RuntimeAdapterKind
{
    SceneToRenderer,
    ResourcesToRenderer,
    SceneToPhysics,
    SceneToAudio,
    ResourcesToAnimation,
    AnimationToRenderer,
    ResourcesToAudio,
    WorldResourcesPersistenceToStreaming,
    TimeToSimulation,
    SimulationToDomain,
    EnvironmentToNavigation,
};

enum class RuntimeUpdateStep
{
    Time,
    MainThreadCommits,
    Resources,
    Streaming,
    Simulation,
    Navigation,
    Animation,
    Physics,
    SceneProjectionCommit,
    Audio,
    Renderer,
    DiagnosticsEvents,
};

enum class RuntimeShutdownStep
{
    StopNewWork,
    ResolveSimulationWork,
    ShutdownStreaming,
    ShutdownAudio,
    ShutdownPhysics,
    FlushSceneProjections,
    ReleaseAnimationResources,
    ShutdownRenderer,
    ReleaseResourceAdapters,
    ClearNavigationAndEvents,
    DestroyAdapters,
    Complete,
};

enum class ShutdownProposalPolicy
{
    CommitValid,
    DiscardWithReason,
    FailIfPending,
};

struct ChunkResourceRequirement
{
    ResourceId resource{};
    ResourceType type{};
    std::size_t estimated_bytes = 0;
};

struct ChunkStreamingManifest
{
    ChunkId chunk{};
    std::vector<ChunkResourceRequirement> resources;
    PersistenceLocation persistence_location{};
};

class IChunkStreamingManifestSource
{
  public:
    virtual ~IChunkStreamingManifestSource() = default;

    [[nodiscard]] virtual foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const = 0;
};

struct PreparedChunkDataSnapshot
{
    ChunkId chunk{};
    PersistenceLocation persistence_location{};
    std::optional<ZoneOverrideSnapshot> persisted_state{};
    std::uint64_t revision = 0;
};

class IStreamingPreparedChunkDataQuery
{
  public:
    virtual ~IStreamingPreparedChunkDataQuery() = default;

    [[nodiscard]] virtual foundation::Result<PreparedChunkDataSnapshot>
        GetPreparedData(ChunkId chunk) const = 0;
};

class IAnimationResourceMapper
{
  public:
    virtual ~IAnimationResourceMapper() = default;

    [[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveSkeleton(animation::SkeletonId id) const = 0;
    [[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveClip(animation::AnimationClipId id) const = 0;
};

class IAnimationSkeletonResourcePayload
{
  public:
    virtual ~IAnimationSkeletonResourcePayload() = default;
    [[nodiscard]] virtual const animation::SkeletonDesc& GetSkeleton() const noexcept = 0;
};

class IAnimationClipResourcePayload
{
  public:
    virtual ~IAnimationClipResourcePayload() = default;
    [[nodiscard]] virtual const animation::AnimationClipDesc& GetClip() const noexcept = 0;
};

class IAudioResourceMapper
{
  public:
    virtual ~IAudioResourceMapper() = default;
    [[nodiscard]] virtual foundation::Result<ResourceRequest> ResolveSound(audio::SoundId id) const = 0;
};

struct PendingSceneProjection
{
    SceneNodeId node{};
    Transform world_transform{};
};

class ISceneProjectionQueue
{
  public:
    virtual ~ISceneProjectionQueue() = default;

    [[nodiscard]] virtual foundation::Result<std::size_t> Flush() = 0;
    [[nodiscard]] virtual std::size_t PendingCount() const noexcept = 0;
    [[nodiscard]] virtual foundation::Result<void> DiscardPending() = 0;
};

class IRuntimePoseCache
{
  public:
    virtual ~IRuntimePoseCache() = default;

    [[nodiscard]] virtual foundation::Result<std::shared_ptr<const animation::PoseBuffer>>
        GetPose(animation::AnimatorHandle animator) const = 0;
    [[nodiscard]] virtual std::size_t PoseCount() const noexcept = 0;
};

struct RuntimePhaseFailure
{
    RuntimeUpdateStep phase{};
    foundation::Error error{};
};

struct RuntimeFrameEvents
{
    std::vector<physics::ContactEvent> physics_contacts;
    std::vector<animation::AnimationEvent> animation_events;
    std::vector<audio::AudioEvent> audio_events;
    std::vector<RuntimePhaseFailure> phase_failures;
};

class IRuntimeEventSink
{
  public:
    virtual ~IRuntimeEventSink() = default;
    [[nodiscard]] virtual foundation::Result<void> Publish(const RuntimeFrameEvents& events) = 0;
};

class IRuntimeAdapterLifecycle
{
  public:
    virtual ~IRuntimeAdapterLifecycle() = default;
    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
};

class IReferenceSimulationCommitLog
{
  public:
    virtual ~IReferenceSimulationCommitLog() = default;
    [[nodiscard]] virtual std::span<const simulation::SimulationProposalBatch> CommittedBatches() const noexcept = 0;
};

struct EngineRuntimeOptions
{
    RuntimeProfile profile = RuntimeProfile::Reference;

    bool enable_assets = true;
    bool enable_serialization = true;
    bool enable_resources = true;
    bool enable_persistence = true;
    bool enable_time = true;
    bool enable_environment = true;
    bool enable_scene = true;
    bool enable_world = true;
    bool enable_streaming = true;
    bool enable_simulation = true;
    bool enable_navigation = true;
    bool enable_animation = true;
    bool enable_physics = true;
    bool enable_audio = true;
    bool enable_renderer = true;

    std::uint32_t max_simulation_commits_per_frame = 64;
    ShutdownProposalPolicy shutdown_proposal_policy = ShutdownProposalPolicy::DiscardWithReason;

    AssetsOptions assets{};
    SerializationOptions serialization{};
    ResourceOptions resources{};
    PersistenceOptions persistence{};
    TimeOptions time{};
    EnvironmentOptions environment{};
    SceneOptions scene{};
    WorldOptions world{};
    streaming::StreamingBudget streaming{};
    navigation::NavigationOptions navigation{};
    animation::AnimationOptions animation{};
    physics::PhysicsOptions physics{};
    audio::AudioOptions audio{};
    simulation::SimulationOptions simulation{};
    renderer::RendererOptions renderer{};
};

struct RuntimeFrameInput
{
    RuntimeFrameDuration real_delta{};
    RuntimeBudget resource_budget{};
    streaming::StreamingBudget streaming_budget{};
    RuntimeBudget navigation_budget{};
    std::size_t max_animators = 0;
    std::uint32_t max_simulation_commits = 0;
};

struct EngineRuntimeDependencies
{
    renderer::RendererDependencies renderer{};
    physics::PhysicsDependencies physics{};
    navigation::NavigationDependencies navigation{};
    animation::AnimationDependencies animation{};
    audio::AudioDependencies audio{};
    streaming::StreamingDependencies streaming{};
    simulation::SimulationDependencies simulation{};

    std::shared_ptr<IChunkStreamingManifestSource> chunk_manifests;
    std::shared_ptr<IAnimationResourceMapper> animation_resource_mapper;
    std::shared_ptr<IAudioResourceMapper> audio_resource_mapper;
    std::shared_ptr<IRuntimeEventSink> event_sink;
};

struct RuntimeTickResult
{
    TimeAdvanceResult time{};
    std::vector<RuntimeUpdateStep> executed_steps;
    std::vector<RuntimePhaseFailure> failures;
};

struct RuntimeIntegrationServices
{
    std::shared_ptr<renderer::IRenderResourceBridge> render_resources;
    std::shared_ptr<renderer::IRenderSceneSource> render_scene;
    std::shared_ptr<renderer::IRenderPoseSource> render_pose_source;
    std::shared_ptr<renderer::IRenderCommandSink> render_commands;

    std::shared_ptr<physics::IPhysicsTransformSource> physics_transform_source;
    std::shared_ptr<physics::IPhysicsTransformSink> physics_transform_sink;
    std::shared_ptr<audio::IAudioTransformSource> audio_transforms;
    std::shared_ptr<ISceneProjectionQueue> scene_projections;

    std::shared_ptr<animation::IAnimationResourceSource> animation_resources;
    std::shared_ptr<animation::IAnimationPoseSink> animation_pose_sink;
    std::shared_ptr<animation::IAnimationEvaluatorBackend> animation_evaluator;
    std::shared_ptr<IRuntimePoseCache> animation_pose_cache;

    std::shared_ptr<audio::IAudioBackend> audio_backend;
    std::shared_ptr<audio::IAudioResourceSource> audio_resources;

    std::shared_ptr<streaming::IStreamingDataSource> streaming_data_source;
    std::shared_ptr<streaming::IStreamingCommitTarget> streaming_commit_target;
    std::shared_ptr<streaming::IStreamingPriorityProvider> streaming_priority_provider;
    std::shared_ptr<streaming::IResidencyController> streaming_residency_controller;
    std::shared_ptr<streaming::IStreamingWorldSource> streaming_world_source;
    std::shared_ptr<streaming::IStreamingPersistenceSource> streaming_persistence_source;
    std::shared_ptr<streaming::IStreamingResourceSource> streaming_resource_source;
    std::shared_ptr<IStreamingPreparedChunkDataQuery> streaming_prepared_data;

    std::shared_ptr<simulation::ISimulationClock> simulation_clock;
    std::shared_ptr<simulation::ISimulationCommitTarget> simulation_commit_target;
    std::shared_ptr<IReferenceSimulationCommitLog> simulation_commit_log;

    std::shared_ptr<navigation::INavigationDataSource> navigation_data_source;
    std::shared_ptr<navigation::INavCostProvider> navigation_costs;
    std::shared_ptr<navigation::INavigationObstacleSource> navigation_obstacles;

    std::shared_ptr<IRuntimeEventSink> event_sink;

    std::vector<RuntimeAdapterKind> owned_adapters;
    std::vector<RuntimeUpdateStep> last_update_order;
    std::vector<RuntimeShutdownStep> last_shutdown_order;
};

class IEngineRuntimeCoordinator
{
  public:
    virtual ~IEngineRuntimeCoordinator() = default;

    [[nodiscard]] virtual foundation::Result<RuntimeTickResult> Tick(const RuntimeFrameInput& input) = 0;
    [[nodiscard]] virtual foundation::Result<void> Shutdown() = 0;
    [[nodiscard]] virtual bool IsShutdownStarted() const noexcept = 0;
    [[nodiscard]] virtual bool IsShutdownComplete() const noexcept = 0;
};

struct EngineRuntimeServices
{
    RuntimeProfile profile = RuntimeProfile::Reference;
    std::vector<std::string> registered_majors;

    std::shared_ptr<RuntimeFoundationRegistration> foundation;
    std::shared_ptr<AssetServices> assets;
    std::shared_ptr<SerializationServices> serialization;
    std::shared_ptr<ResourceServices> resources;
    std::shared_ptr<PersistenceServices> persistence;
    std::shared_ptr<TimeServices> time;
    std::shared_ptr<EnvironmentServices> environment;
    std::shared_ptr<SceneServices> scene;
    std::shared_ptr<WorldServices> world;
    std::shared_ptr<streaming::StreamingServices> streaming;
    std::shared_ptr<simulation::SimulationServices> simulation;
    std::shared_ptr<navigation::NavigationServices> navigation;
    std::shared_ptr<animation::AnimationServices> animation;
    std::shared_ptr<physics::PhysicsServices> physics;
    std::shared_ptr<audio::AudioServices> audio;
    std::shared_ptr<renderer::RendererServices> renderer;

    std::shared_ptr<RuntimeIntegrationServices> integrations;
    std::shared_ptr<IEngineRuntimeCoordinator> coordinator;
};

struct PreparedEngineRuntime
{
    EngineRuntimeServices services;
};

[[nodiscard]] foundation::Result<void> RegisterRuntimeFoundation(core::Application& app);
[[nodiscard]] foundation::Result<void> RegisterAssets(core::Application& app, const AssetsOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterSerialization(core::Application& app, const SerializationOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterResources(core::Application& app, const ResourceOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterPersistence(core::Application& app, const PersistenceOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterTime(core::Application& app, const TimeOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterEnvironment(core::Application& app, const EnvironmentOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterScene(core::Application& app, const SceneOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterWorld(core::Application& app, const WorldOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterStreaming(core::Application& app);
[[nodiscard]] foundation::Result<void> RegisterSimulation(core::Application& app, const simulation::SimulationOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterNavigation(core::Application& app, navigation::NavigationOptions options = {});
[[nodiscard]] foundation::Result<void> RegisterAnimation(core::Application& app, animation::AnimationOptions options = {});
[[nodiscard]] foundation::Result<void> RegisterPhysics(core::Application& app);
[[nodiscard]] foundation::Result<void> RegisterAudio(core::Application& app, audio::AudioOptions options = {});
[[nodiscard]] foundation::Result<void> RegisterRenderer(core::Application& app, const renderer::RendererOptions& options = {});

[[nodiscard]] foundation::Result<PreparedEngineRuntime> PrepareEngineRuntime(
    const EngineRuntimeOptions& options = {},
    EngineRuntimeDependencies dependencies = {});

[[nodiscard]] foundation::Result<EngineRuntimeServices> CommitPreparedRuntime(
    core::Application& app,
    PreparedEngineRuntime prepared);

[[nodiscard]] foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(
    core::Application& app,
    const EngineRuntimeOptions& options = {},
    EngineRuntimeDependencies dependencies = {});

[[nodiscard]] std::vector<RuntimeAdapterKind> GetAllowedRuntimeAdapters();
[[nodiscard]] std::vector<RuntimeUpdateStep> GetRuntimeUpdateOrder();
[[nodiscard]] std::vector<RuntimeShutdownStep> GetRuntimeShutdownOrder();
} // namespace epidemic::runtime
