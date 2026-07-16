#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/view_system.h"

#include <memory>

namespace epidemic::runtime::renderer
{
struct RendererOptions
{
    IRenderResourceBridge* resource_bridge = nullptr;
    IRenderSceneSource* scene_source = nullptr;
};

struct RendererServices
{
    std::shared_ptr<IRenderResourceBridge> resource_bridge;
    std::shared_ptr<IRenderSceneSource> scene_source;
    std::shared_ptr<IRenderScene> scene;
    std::shared_ptr<IViewSystem> views;
    std::shared_ptr<IRendererRuntime> runtime;
};

[[nodiscard]] foundation::Result<RendererServices> CreateRendererServices(const RendererOptions& options);
[[nodiscard]] foundation::Result<RendererServices> CreateMockRendererServices();
} // namespace epidemic::runtime::renderer
