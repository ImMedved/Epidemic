#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_services.h"
#include "Epidemic/Runtime/Environment/environment_services.h"
#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Resources/resource_services.h"
#include "Epidemic/Runtime/Scene/scene_services.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"

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
    bool enable_environment = true;
    bool enable_scene = true;
    bool enable_renderer = true;

    AssetsOptions assets{};
    SerializationOptions serialization{};
    ResourceOptions resources{};
    PersistenceOptions persistence{};
    EnvironmentOptions environment{};
    SceneOptions scene{};
    renderer::RendererOptions renderer{};
};

struct EngineRuntimeServices
{
    std::vector<std::string> registered_majors;
};

[[nodiscard]] foundation::Result<void> RegisterRuntimeFoundation(core::Application& app);
[[nodiscard]] foundation::Result<void> RegisterAssets(core::Application& app, const AssetsOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterSerialization(core::Application& app, const SerializationOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterResources(core::Application& app, const ResourceOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterPersistence(core::Application& app, const PersistenceOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterEnvironment(core::Application& app, const EnvironmentOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterScene(core::Application& app, const SceneOptions& options = {});
[[nodiscard]] foundation::Result<void> RegisterRenderer(core::Application& app, const renderer::RendererOptions& options = {});

[[nodiscard]] foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(core::Application& app,
                                                                                     const EngineRuntimeOptions& options = {});
} // namespace epidemic::runtime
