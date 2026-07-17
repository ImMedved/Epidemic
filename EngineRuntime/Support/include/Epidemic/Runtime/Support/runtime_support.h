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

#include <string>
#include <vector>

namespace epidemic::runtime
{
struct RuntimeFoundationRegistration
{
    std::string name = "RuntimeFoundation";
};

struct EngineRuntimeOptions
{
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

    AssetsOptions assets{};
    SerializationOptions serialization{};
    ResourceOptions resources{};
    PersistenceOptions persistence{};
    TimeOptions time{};
    EnvironmentOptions environment{};
    SceneOptions scene{};
    WorldOptions world{};
    navigation::NavigationOptions navigation{};
    animation::AnimationOptions animation{};
    audio::AudioOptions audio{};
    simulation::SimulationOptions simulation{};
    renderer::RendererOptions renderer{};
};

struct EngineRuntimeServices
{
    std::vector<std::string> registered_majors;
};

enum class RuntimeAdapterKind
{
    SceneToRenderer,
    ResourcesToRenderer,
    SceneToPhysics,
    ResourcesToAnimation,
    AnimationToRenderer,
    ResourcesToAudio,
    SceneToAudio,
    WorldResourcesPersistenceToStreaming,
    TimeToSimulation,
    EnvironmentToAudio,
    EnvironmentToNavigation
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
    DiagnosticsEvents
};

enum class RuntimeShutdownStep
{
    StopNewWork,
    CancelWaitBackgroundJobs,
    FlushDiscardProposals,
    StopAudio,
    StopPhysics,
    ReleaseRenderer,
    UnloadStreaming,
    ClosePersistenceTransactions,
    DestroyServices
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

[[nodiscard]] foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(core::Application& app,
                                                                                     const EngineRuntimeOptions& options = {});
[[nodiscard]] std::vector<RuntimeAdapterKind> GetAllowedRuntimeAdapters();
[[nodiscard]] std::vector<RuntimeUpdateStep> GetRuntimeUpdateOrder();
[[nodiscard]] std::vector<RuntimeShutdownStep> GetRuntimeShutdownOrder();
} // namespace epidemic::runtime
