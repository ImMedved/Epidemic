#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Renderer/view_system.h"
#include "renderer_runtime_impl.h"

#include <memory>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{
using epidemic::foundation::Result;
using epidemic::runtime::ByteResourcePayload;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::Transform;
using epidemic::runtime::renderer::CreateMockRendererServices;
using epidemic::runtime::renderer::CreateRendererServices;
using epidemic::runtime::renderer::HasRenderDirtyFlag;
using epidemic::runtime::renderer::IRenderResourceBridge;
using epidemic::runtime::renderer::IRenderScene;
using epidemic::runtime::renderer::IRenderSceneSource;
using epidemic::runtime::renderer::IRendererRuntime;
using epidemic::runtime::renderer::IViewSystem;
using epidemic::runtime::renderer::RenderFrameState;
using epidemic::runtime::renderer::RenderLayer;
using epidemic::runtime::renderer::RenderProxyDesc;
using epidemic::runtime::renderer::RenderProxyDirtyFlags;
using epidemic::runtime::renderer::RenderProxyLifecycle;
using epidemic::runtime::renderer::RenderProxyReadiness;
using epidemic::runtime::renderer::RenderProxyVisibility;
using epidemic::runtime::renderer::RenderResourcePayloads;
using epidemic::runtime::renderer::RenderTransformSnapshot;
using epidemic::runtime::renderer::RendererRuntime;
using epidemic::runtime::renderer::ViewDesc;
using epidemic::runtime::renderer::ViewLifecycle;

class TestResourceBridge final : public IRenderResourceBridge
{
  public:
    bool ready = true;

    [[nodiscard]] Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        if (!ready || !mesh.IsValid() || !material.IsValid())
        {
            return Result<RenderResourcePayloads>::Failure(
                epidemic::foundation::Error::Create("renderer.resource_missing", "test resources are missing"));
        }
        return Result<RenderResourcePayloads>::Success(RenderResourcePayloads{
            std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x01}}),
            std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x02}})});
    }
};

class TestSceneSource final : public IRenderSceneSource
{
  public:
    void AddNode(SceneNodeId id)
    {
        nodes.insert(id);
    }

    [[nodiscard]] Result<RenderTransformSnapshot> GetTransformSnapshot(SceneNodeId node) const override
    {
        if (!nodes.contains(node))
        {
            return Result<RenderTransformSnapshot>::Failure(
                epidemic::foundation::Error::Create("renderer.transform_missing", "test scene node is missing"));
        }
        return Result<RenderTransformSnapshot>::Success(RenderTransformSnapshot{node, Transform{}, 7u});
    }

    std::unordered_set<SceneNodeId> nodes;
};

[[nodiscard]] RenderProxyDesc MakeProxyDesc(SceneNodeId node, ResourceId mesh, ResourceId material)
{
    RenderProxyDesc desc{};
    desc.owner = RuntimeObjectId{11};
    desc.mesh = mesh;
    desc.material = material;
    desc.transform_node = node;
    desc.layer = RenderLayer::Opaque;
    desc.visibility = RenderProxyVisibility::Visible;
    return desc;
}

[[nodiscard]] bool TestRegisterProxyTracksSplitState()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{1});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{1}, ResourceId::FromString("mesh/a"), ResourceId::FromString("mat/a")));
    return proxy && runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::Registered &&
           runtime.GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Ready &&
           runtime.GetProxyVisibility(proxy.Value()) == RenderProxyVisibility::Visible &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform) &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Material);
}

[[nodiscard]] bool TestMissingResourceMarksNotReady()
{
    TestResourceBridge bridge;
    bridge.ready = false;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{2});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{2}, ResourceId::FromString("mesh/a"), ResourceId::FromString("mat/a")));
    return !proxy && proxy.GetError().HasCode("renderer.resource_missing");
}

[[nodiscard]] bool TestDirtyAndVisibilityFlags()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{3});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{3}, ResourceId::FromString("mesh/dirty"), ResourceId::FromString("mat/dirty")));
    if (!proxy || !runtime.MarkTransformDirty(proxy.Value()) || !runtime.MarkMaterialDirty(proxy.Value()) ||
        !runtime.SetProxyVisibility(proxy.Value(), RenderProxyVisibility::Hidden))
    {
        return false;
    }

    return runtime.GetProxyVisibility(proxy.Value()) == RenderProxyVisibility::Hidden &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform) &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Material) &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Visibility);
}

[[nodiscard]] bool TestDeferredProxyDestroy()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{4});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{4}, ResourceId::FromString("mesh/d"), ResourceId::FromString("mat/d")));
    if (!proxy || !runtime.DestroyProxy(proxy.Value()))
    {
        return false;
    }

    const bool pending = runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::DestroyPending;
    const auto flush = runtime.FlushDeferredDestroys();
    return pending && flush && runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::Unregistered;
}

[[nodiscard]] bool TestViewLifecycleAndMainView()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{5});

    RendererRuntime runtime(&bridge, &scene);
    const auto view = runtime.CreateView(ViewDesc{SceneNodeId{5}, 70.0f, 0.1f, 1500.0f});
    if (!view || !runtime.SetMainView(view.Value()) || runtime.GetMainView() != view.Value())
    {
        return false;
    }
    const auto destroy = runtime.DestroyView(view.Value());
    const bool pending = runtime.GetViewLifecycle(view.Value()) == ViewLifecycle::DestroyPending;
    const auto flush = runtime.FlushDeferredDestroys();
    return destroy && pending && flush && runtime.GetViewLifecycle(view.Value()) == ViewLifecycle::Destroyed && !runtime.GetMainView().IsValid();
}

[[nodiscard]] bool TestFrameFlowEndsSubmittedAndClearsDirty()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(SceneNodeId{6});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{6}, ResourceId::FromString("mesh/frame"), ResourceId::FromString("mat/frame")));
    const auto view = runtime.CreateView(ViewDesc{SceneNodeId{6}, 75.0f, 0.1f, 2000.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame())
    {
        return false;
    }
    if (runtime.GetFrameState() != RenderFrameState::ReadyToRender || runtime.GetProxyDirtyFlags(proxy.Value()) != 0u)
    {
        return false;
    }
    const auto render = runtime.RenderFrame();
    return render && runtime.GetFrameState() == RenderFrameState::Submitted;
}

[[nodiscard]] bool TestStrictAndMockFactories()
{
    const auto strict_missing = CreateRendererServices({});
    if (strict_missing || !strict_missing.GetError().HasCode("renderer.resource_bridge_missing"))
    {
        return false;
    }

    const auto mock = CreateMockRendererServices();
    if (!mock || !mock.Value().scene || !mock.Value().views || !mock.Value().runtime || !mock.Value().resource_bridge || !mock.Value().scene_source)
    {
        return false;
    }

    const auto proxy = mock.Value().scene->RegisterProxy(MakeProxyDesc(SceneNodeId{9}, ResourceId::FromString("mesh/m"), ResourceId::FromString("mat/m")));
    return proxy && mock.Value().scene->GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Ready;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IRenderScene>);
    static_assert(std::is_abstract_v<IViewSystem>);
    static_assert(std::is_abstract_v<IRendererRuntime>);
    static_assert(std::is_abstract_v<IRenderResourceBridge>);
    static_assert(std::is_abstract_v<IRenderSceneSource>);

    if (!TestRegisterProxyTracksSplitState()) return 1;
    if (!TestMissingResourceMarksNotReady()) return 2;
    if (!TestDirtyAndVisibilityFlags()) return 3;
    if (!TestDeferredProxyDestroy()) return 4;
    if (!TestViewLifecycleAndMainView()) return 5;
    if (!TestFrameFlowEndsSubmittedAndClearsDirty()) return 6;
    if (!TestStrictAndMockFactories()) return 7;
    return 0;
}
