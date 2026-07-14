#include "Epidemic/Runtime/Renderer/renderer_services.h"

#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime::renderer
{
namespace
{
class MockRenderResourceBridge final : public IRenderResourceBridge
{
  public:
    [[nodiscard]] foundation::Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        if (!mesh.IsValid() || !material.IsValid())
        {
            return foundation::Result<RenderResourcePayloads>::Failure(
                foundation::Error::Create("renderer.invalid_resource", "mock renderer payload request requires valid resources"));
        }
        return foundation::Result<RenderResourcePayloads>::Success(RenderResourcePayloads{
            std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x01}}),
            std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x02}})});
    }
};

class MockRenderSceneSource final : public IRenderSceneSource
{
  public:
    [[nodiscard]] foundation::Result<RenderTransformSnapshot> GetTransformSnapshot(SceneNodeId node) const override
    {
        if (!node.IsValid())
        {
            return foundation::Result<RenderTransformSnapshot>::Failure(
                foundation::Error::Create("renderer.invalid_transform", "mock renderer requires a valid scene node"));
        }
        return foundation::Result<RenderTransformSnapshot>::Success(RenderTransformSnapshot{node, {}, 1u});
    }
};
} // namespace

foundation::Result<RendererServices> CreateRendererServices(const RendererOptions& options)
{
    if (options.resource_bridge == nullptr)
    {
        return foundation::Result<RendererServices>::Failure(
            foundation::Error::Create("renderer.resource_bridge_missing", "production renderer services require a resource bridge"));
    }
    if (options.scene_source == nullptr)
    {
        return foundation::Result<RendererServices>::Failure(
            foundation::Error::Create("renderer.scene_source_missing", "production renderer services require a scene source"));
    }

    auto runtime = std::make_shared<RendererRuntime>(options.resource_bridge, options.scene_source);
    RendererServices services{};
    services.scene = runtime;
    services.views = runtime;
    services.runtime = runtime;
    return foundation::Result<RendererServices>::Success(std::move(services));
}

foundation::Result<RendererServices> CreateMockRendererServices()
{
    auto resource_bridge = std::make_shared<MockRenderResourceBridge>();
    auto scene_source = std::make_shared<MockRenderSceneSource>();
    auto runtime = std::make_shared<RendererRuntime>(resource_bridge.get(), scene_source.get());

    RendererServices services{};
    services.scene = runtime;
    services.views = runtime;
    services.runtime = runtime;
    services.resource_bridge = resource_bridge;
    services.scene_source = scene_source;
    return foundation::Result<RendererServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::renderer
