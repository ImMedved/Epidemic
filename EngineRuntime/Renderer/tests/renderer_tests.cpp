#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/view_system.h"
#include "renderer_runtime_impl.h"

#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace
{
using epidemic::runtime::ResourceId;
using epidemic::runtime::ResourceState;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SceneNodeId;
using epidemic::runtime::renderer::IRenderResourceBridge;
using epidemic::runtime::renderer::IRenderScene;
using epidemic::runtime::renderer::IRenderSceneSource;
using epidemic::runtime::renderer::IRendererRuntime;
using epidemic::runtime::renderer::IViewSystem;
using epidemic::runtime::renderer::RenderFrameState;
using epidemic::runtime::renderer::RenderLayer;
using epidemic::runtime::renderer::RenderProxyDesc;
using epidemic::runtime::renderer::RenderProxyState;
using epidemic::runtime::renderer::RendererRuntime;
using epidemic::runtime::renderer::ViewDesc;

class MockRenderResourceBridge final : public IRenderResourceBridge
{
  public:
    void SetState(ResourceId id, ResourceState state)
    {
        states_[id] = state;
    }

    [[nodiscard]] ResourceState GetResourceState(ResourceId id) const override
    {
        const auto iterator = states_.find(id);
        if (iterator == states_.end())
        {
            return ResourceState::Unknown;
        }

        return iterator->second;
    }

  private:
    std::unordered_map<ResourceId, ResourceState> states_;
};

class MockRenderSceneSource final : public IRenderSceneSource
{
  public:
    void AddNode(SceneNodeId id)
    {
        nodes_.insert(id);
    }

    [[nodiscard]] bool HasNode(SceneNodeId node) const override
    {
        return nodes_.contains(node);
    }

  private:
    std::unordered_set<SceneNodeId> nodes_;
};

RenderProxyDesc MakeProxyDesc(SceneNodeId node, ResourceId mesh, ResourceId material)
{
    RenderProxyDesc desc{};
    desc.owner = RuntimeObjectId{11};
    desc.mesh = mesh;
    desc.material = material;
    desc.transform_node = node;
    desc.layer = RenderLayer::Opaque;
    return desc;
}

bool TestRegisterAndUnregisterProxy()
{
    MockRenderSceneSource scene;
    scene.AddNode(SceneNodeId{1});

    RendererRuntime runtime(nullptr, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(SceneNodeId{1}, ResourceId::FromString("mesh/a"), ResourceId::FromString("mat/a")));
    if (!proxy)
    {
        return false;
    }

    if (runtime.GetProxyState(proxy.Value()) != RenderProxyState::Ready)
    {
        return false;
    }

    const auto remove = runtime.UnregisterProxy(proxy.Value());
    return remove && runtime.GetProxyState(proxy.Value()) == RenderProxyState::Unregistered;
}

bool TestMissingResourceState()
{
    MockRenderSceneSource scene;
    scene.AddNode(SceneNodeId{2});

    MockRenderResourceBridge bridge;
    bridge.SetState(ResourceId::FromString("mesh/ok"), ResourceState::Ready);
    bridge.SetState(ResourceId::FromString("mat/missing"), ResourceState::Unknown);

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(
        SceneNodeId{2},
        ResourceId::FromString("mesh/ok"),
        ResourceId::FromString("mat/missing")));
    return proxy && runtime.GetProxyState(proxy.Value()) == RenderProxyState::ResourceMissing;
}

bool TestDirtyFlags()
{
    MockRenderSceneSource scene;
    scene.AddNode(SceneNodeId{3});

    RendererRuntime runtime(nullptr, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(
        SceneNodeId{3},
        ResourceId::FromString("mesh/dirty"),
        ResourceId::FromString("mat/dirty")));
    if (!proxy)
    {
        return false;
    }

    if (!runtime.MarkTransformDirty(proxy.Value()) || runtime.GetProxyState(proxy.Value()) != RenderProxyState::DirtyTransform)
    {
        return false;
    }

    if (!runtime.MarkMaterialDirty(proxy.Value()) || runtime.GetProxyState(proxy.Value()) != RenderProxyState::DirtyMaterial)
    {
        return false;
    }

    return true;
}

bool TestViewCreationAndMainView()
{
    MockRenderSceneSource scene;
    scene.AddNode(SceneNodeId{4});
    scene.AddNode(SceneNodeId{5});

    RendererRuntime runtime(nullptr, &scene);
    const auto first = runtime.CreateView(ViewDesc{SceneNodeId{4}, 70.0f, 0.1f, 1500.0f});
    const auto second = runtime.CreateView(ViewDesc{SceneNodeId{5}, 60.0f, 0.2f, 900.0f});
    if (!first || !second)
    {
        return false;
    }

    const auto set_main = runtime.SetMainView(second.Value());
    return set_main && runtime.GetMainView() == second.Value();
}

bool TestMinimalFrameFlow()
{
    MockRenderSceneSource scene;
    scene.AddNode(SceneNodeId{6});

    MockRenderResourceBridge bridge;
    bridge.SetState(ResourceId::FromString("mesh/frame"), ResourceState::Ready);
    bridge.SetState(ResourceId::FromString("mat/frame"), ResourceState::Ready);

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(
        SceneNodeId{6},
        ResourceId::FromString("mesh/frame"),
        ResourceId::FromString("mat/frame")));
    const auto view = runtime.CreateView(ViewDesc{SceneNodeId{6}, 75.0f, 0.1f, 2000.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()))
    {
        return false;
    }

    if (!runtime.PrepareFrame() || runtime.GetFrameState() != RenderFrameState::ReadyToRender)
    {
        return false;
    }

    if (!runtime.RenderFrame() || runtime.GetFrameState() != RenderFrameState::Presented)
    {
        return false;
    }

    return runtime.GetProxyState(proxy.Value()) == RenderProxyState::Visible;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IRenderScene>);
    static_assert(std::is_abstract_v<IViewSystem>);
    static_assert(std::is_abstract_v<IRendererRuntime>);
    static_assert(std::is_abstract_v<IRenderResourceBridge>);
    static_assert(std::is_abstract_v<IRenderSceneSource>);

    if (!TestRegisterAndUnregisterProxy())
    {
        return 1;
    }

    if (!TestMissingResourceState())
    {
        return 2;
    }

    if (!TestDirtyFlags())
    {
        return 3;
    }

    if (!TestViewCreationAndMainView())
    {
        return 4;
    }

    if (!TestMinimalFrameFlow())
    {
        return 5;
    }

    return 0;
}
