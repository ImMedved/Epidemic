#include "Epidemic/Runtime/Renderer/renderer_services.h"

#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime::renderer
{
namespace
{
struct MockMeshResource final : public IRenderMeshResource
{
};

struct MockMaterialResource final : public IRenderMaterialResource
{
};

class MockRenderResourceBridge final : public IRenderResourceBridge
{
  public:
    [[nodiscard]] foundation::Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) override
    {
        if (!mesh.IsValid() || !material.IsValid())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("renderer.invalid_resource", "mock renderer payload acquire requires valid resources"));
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> ReleasePayloads(ResourceId mesh, ResourceId material) override
    {
        (void)mesh;
        (void)material;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        if (!mesh.IsValid() || !material.IsValid())
        {
            return foundation::Result<RenderResourcePayloads>::Failure(
                foundation::Error::Create("renderer.invalid_resource", "mock renderer payload request requires valid resources"));
        }
        return foundation::Result<RenderResourcePayloads>::Success(RenderResourcePayloads{
            std::make_shared<MockMeshResource>(),
            std::make_shared<MockMaterialResource>()});
    }
};

class MockRenderSceneSource final : public IRenderSceneSource
{
  public:
    [[nodiscard]] foundation::Result<RenderTransformSnapshot> GetTransformSnapshot(RenderTransformId node) const override
    {
        if (!node.IsValid())
        {
            return foundation::Result<RenderTransformSnapshot>::Failure(
                foundation::Error::Create("renderer.invalid_transform", "mock renderer requires a valid scene node"));
        }
        return foundation::Result<RenderTransformSnapshot>::Success(RenderTransformSnapshot{node, {}, 1u});
    }
};

class MockRenderCommandSink final : public IRenderCommandSink
{
  public:
    [[nodiscard]] foundation::Result<void> BeginFrame(const RenderFrameContext&) override { return foundation::Result<void>::Success(); }
    [[nodiscard]] foundation::Result<void> SubmitProxy(const RenderProxySubmission&) override { return foundation::Result<void>::Success(); }
    [[nodiscard]] foundation::Result<void> EndFrame() override { return foundation::Result<void>::Success(); }
    [[nodiscard]] foundation::Result<void> AbortFrame() override { return foundation::Result<void>::Success(); }
};
} // namespace

foundation::Result<RendererServices> CreateRendererServices(const RendererOptions& options, RendererDependencies dependencies)
{
    (void)options;
    if (dependencies.resource_bridge == nullptr)
    {
        return foundation::Result<RendererServices>::Failure(
            foundation::Error::Create("renderer.resource_bridge_missing", "production renderer services require a resource bridge"));
    }
    if (dependencies.scene_source == nullptr)
    {
        return foundation::Result<RendererServices>::Failure(
            foundation::Error::Create("renderer.scene_source_missing", "production renderer services require a scene source"));
    }
    if (dependencies.command_sink == nullptr)
    {
        return foundation::Result<RendererServices>::Failure(
            foundation::Error::Create("renderer.command_sink_missing", "production renderer services require a command sink"));
    }

    auto runtime = std::make_shared<RendererRuntime>(dependencies.resource_bridge.get(),
                                                     dependencies.scene_source.get(),
                                                     dependencies.pose_source.get(),
                                                     dependencies.command_sink.get());
    RendererServices services{};
    services.scene = runtime;
    services.views = runtime;
    services.runtime = runtime;
    services.resource_bridge = std::move(dependencies.resource_bridge);
    services.scene_source = std::move(dependencies.scene_source);
    services.pose_source = std::move(dependencies.pose_source);
    services.command_sink = std::move(dependencies.command_sink);
    return foundation::Result<RendererServices>::Success(std::move(services));
}

foundation::Result<RendererServices> CreateMockRendererServices()
{
    auto resource_bridge = std::make_shared<MockRenderResourceBridge>();
    auto scene_source = std::make_shared<MockRenderSceneSource>();
    auto command_sink = std::make_shared<MockRenderCommandSink>();
    auto runtime = std::make_shared<RendererRuntime>(resource_bridge.get(), scene_source.get(), command_sink.get());

    RendererServices services{};
    services.scene = runtime;
    services.views = runtime;
    services.runtime = runtime;
    services.resource_bridge = resource_bridge;
    services.scene_source = scene_source;
    services.pose_source = {};
    services.command_sink = command_sink;
    return foundation::Result<RendererServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::renderer
