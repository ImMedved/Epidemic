#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Renderer/view_system.h"
#include "renderer_runtime_impl.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace
{
using epidemic::foundation::Result;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::Transform;
using epidemic::runtime::renderer::CreateMockRendererServices;
using epidemic::runtime::renderer::CreateRendererServices;
using epidemic::runtime::renderer::HasRenderDirtyFlag;
using epidemic::runtime::renderer::IRenderCommandSink;
using epidemic::runtime::renderer::IRenderMaterialResource;
using epidemic::runtime::renderer::IRenderMeshResource;
using epidemic::runtime::renderer::IRenderResourceBridge;
using epidemic::runtime::renderer::IRenderScene;
using epidemic::runtime::renderer::IRenderSceneSource;
using epidemic::runtime::renderer::IRendererRuntime;
using epidemic::runtime::renderer::IViewSystem;
using epidemic::runtime::renderer::RenderFrameState;
using epidemic::runtime::renderer::RenderFrameContext;
using epidemic::runtime::renderer::RenderLayer;
using epidemic::runtime::renderer::RenderProxyDesc;
using epidemic::runtime::renderer::RenderProxyDirtyFlags;
using epidemic::runtime::renderer::RenderProxyLifecycle;
using epidemic::runtime::renderer::RenderProxyReadiness;
using epidemic::runtime::renderer::RenderProxyVisibility;
using epidemic::runtime::renderer::RenderResourcePayloads;
using epidemic::runtime::renderer::RenderProxySubmission;
using epidemic::runtime::renderer::RenderTransformId;
using epidemic::runtime::renderer::RenderTransformSnapshot;
using epidemic::runtime::renderer::RendererDependencies;
using epidemic::runtime::renderer::RendererRuntime;
using epidemic::runtime::renderer::ViewDesc;
using epidemic::runtime::renderer::ViewLifecycle;

struct TestMeshResource final : public IRenderMeshResource
{
};

struct TestMaterialResource final : public IRenderMaterialResource
{
};

class TestResourceBridge final : public IRenderResourceBridge
{
  public:
    enum class MissingMode
    {
        Temporary,
        Permanent,
    };

    bool ready = true;
    bool fail_release = false;
    MissingMode missing_mode = MissingMode::Temporary;
    std::unordered_set<ResourceId> missing_resources;
    int acquire_count = 0;
    int release_count = 0;

    [[nodiscard]] Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) override
    {
        if (!mesh.IsValid() || !material.IsValid())
        {
            return Result<void>::Failure(
                epidemic::foundation::Error::Create("renderer.invalid_resource", "test resource acquire requires valid resources"));
        }
        ++acquire_count;
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> ReleasePayloads(ResourceId mesh, ResourceId material) override
    {
        (void)mesh;
        (void)material;
        ++release_count;
        if (fail_release)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.release_failed", "release failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        if (!ready || !mesh.IsValid() || !material.IsValid() || missing_resources.contains(mesh) || missing_resources.contains(material))
        {
            const char* code = missing_mode == MissingMode::Temporary ? "renderer.resource_not_ready" : "renderer.resource_failed";
            return Result<RenderResourcePayloads>::Failure(
                epidemic::foundation::Error::Create(code, "test resources are missing"));
        }
        return Result<RenderResourcePayloads>::Success(RenderResourcePayloads{
            std::make_shared<TestMeshResource>(),
            std::make_shared<TestMaterialResource>()});
    }
};

class TestSceneSource final : public IRenderSceneSource
{
  public:
    void AddNode(RenderTransformId id)
    {
        nodes.insert(id);
    }

    void SetRevision(std::uint64_t value)
    {
        revision = value;
    }

    [[nodiscard]] Result<RenderTransformSnapshot> GetTransformSnapshot(RenderTransformId node) const override
    {
        ++requests;
        if (!nodes.contains(node))
        {
            return Result<RenderTransformSnapshot>::Failure(
                epidemic::foundation::Error::Create("renderer.transform_missing", "test scene node is missing"));
        }
        return Result<RenderTransformSnapshot>::Success(RenderTransformSnapshot{node, Transform{}, revision});
    }

    std::unordered_set<RenderTransformId> nodes;
    mutable int requests = 0;
    std::uint64_t revision = 7u;
};

class TestCommandSink final : public IRenderCommandSink
{
  public:
    [[nodiscard]] Result<void> BeginFrame(const RenderFrameContext& context) override
    {
        ++begin_count;
        last_view = context.view;
        last_view_revision = context.transform.revision;
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> SubmitProxy(const RenderProxySubmission& submission) override
    {
        ++submit_count;
        last_revision = submission.transform.revision;
        submitted.push_back(submission.proxy);
        if (fail_submit)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.submit_failed", "submit failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> EndFrame() override
    {
        ++end_count;
        if (fail_end)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.end_failed", "end failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> AbortFrame() override
    {
        ++abort_count;
        return Result<void>::Success();
    }

    int begin_count = 0;
    int submit_count = 0;
    int end_count = 0;
    int abort_count = 0;
    bool fail_submit = false;
    bool fail_end = false;
    epidemic::runtime::renderer::ViewId last_view{};
    std::uint64_t last_view_revision = 0;
    std::uint64_t last_revision = 0;
    std::vector<epidemic::runtime::renderer::RenderProxyId> submitted;
};

[[nodiscard]] RenderProxyDesc MakeProxyDesc(RenderTransformId node, ResourceId mesh, ResourceId material)
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
    scene.AddNode(RenderTransformId{1});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{1}, ResourceId::FromString("mesh/a"), ResourceId::FromString("mat/a")));
    return proxy && runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::Registered &&
           runtime.GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Ready &&
           runtime.GetProxyVisibility(proxy.Value()) == RenderProxyVisibility::Visible &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform) &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Material);
}

[[nodiscard]] bool TestMissingResourceMarksNotReady()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{2});

    RendererRuntime runtime(&bridge, &scene);
    const auto mesh = ResourceId::FromString("mesh/a");
    bridge.missing_resources.insert(mesh);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{2}, mesh, ResourceId::FromString("mat/a")));
    return proxy && runtime.GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Loading &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Material);
}

[[nodiscard]] bool TestDirtyAndVisibilityFlags()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{3});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{3}, ResourceId::FromString("mesh/dirty"), ResourceId::FromString("mat/dirty")));
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
    scene.AddNode(RenderTransformId{4});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{4}, ResourceId::FromString("mesh/d"), ResourceId::FromString("mat/d")));
    if (!proxy || !runtime.DestroyProxy(proxy.Value()))
    {
        return false;
    }

    const bool pending = runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::DestroyPending;
    const auto flush = runtime.FlushDeferredDestroys();
    return pending && flush && runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::Unregistered &&
           bridge.acquire_count == 1 && bridge.release_count == 1;
}

[[nodiscard]] bool TestViewLifecycleAndMainView()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{5});

    RendererRuntime runtime(&bridge, &scene);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{5}, 70.0f, 0.1f, 1500.0f});
    if (!view || !runtime.SetMainView(view.Value()) || runtime.GetMainView() != view.Value())
    {
        return false;
    }
    const auto destroy = runtime.DestroyView(view.Value());
    const bool pending = runtime.GetViewLifecycle(view.Value()) == ViewLifecycle::DestroyPending;
    const auto flush = runtime.FlushDeferredDestroys();
    return destroy && pending && flush && runtime.GetViewLifecycle(view.Value()) == ViewLifecycle::Destroyed && !runtime.GetMainView().IsValid();
}

[[nodiscard]] bool TestViewRejectsNonFiniteCameraParameters()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{55});
    RendererRuntime runtime(&bridge, &scene);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const ViewDesc cases[] = {
        ViewDesc{RenderTransformId{55}, nan, 0.1f, 1000.0f},
        ViewDesc{RenderTransformId{55}, inf, 0.1f, 1000.0f},
        ViewDesc{RenderTransformId{55}, -inf, 0.1f, 1000.0f},
        ViewDesc{RenderTransformId{55}, 60.0f, nan, 1000.0f},
        ViewDesc{RenderTransformId{55}, 60.0f, inf, 1000.0f},
        ViewDesc{RenderTransformId{55}, 60.0f, 0.1f, nan},
        ViewDesc{RenderTransformId{55}, 60.0f, 0.1f, inf},
        ViewDesc{RenderTransformId{55}, 60.0f, 0.1f, -inf},
        ViewDesc{RenderTransformId{55}, 0.0f, 0.1f, 1000.0f},
        ViewDesc{RenderTransformId{55}, 180.0f, 0.1f, 1000.0f},
        ViewDesc{RenderTransformId{55}, 60.0f, 0.0f, 1000.0f},
        ViewDesc{RenderTransformId{55}, 60.0f, 1.0f, 1.0f},
    };
    for (const ViewDesc& desc : cases)
    {
        const auto view = runtime.CreateView(desc);
        if (view || !view.GetError().HasCode("renderer.invalid_view"))
        {
            return false;
        }
    }

    const auto valid = runtime.CreateView(ViewDesc{RenderTransformId{55}, 179.999f, std::numeric_limits<float>::min(), 1.0f});
    return valid && valid.Value().value == 1u;
}

[[nodiscard]] bool TestFrameFlowEndsSubmittedAndClearsDirty()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{6});

    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{6}, ResourceId::FromString("mesh/frame"), ResourceId::FromString("mat/frame")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{6}, 75.0f, 0.1f, 2000.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame())
    {
        return false;
    }
    if (runtime.GetFrameState() != RenderFrameState::ReadyToRender || runtime.GetProxyDirtyFlags(proxy.Value()) != 0u)
    {
        return false;
    }
    const auto render = runtime.RenderFrame();
    return render && runtime.GetFrameState() == RenderFrameState::Submitted &&
           sink.begin_count == 1 && sink.end_count == 1 && sink.last_view == view.Value();
}

[[nodiscard]] bool TestPrepareFrameRefreshesDirtyTransformBeforeSubmit()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{7});

    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{7}, ResourceId::FromString("mesh/tx"), ResourceId::FromString("mat/tx")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{7}, 70.0f, 0.1f, 1500.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame() || !runtime.RenderFrame() || sink.last_revision != 7u)
    {
        return false;
    }

    scene.SetRevision(9u);
    if (!runtime.MarkTransformDirty(proxy.Value()) || !runtime.PrepareFrame() || runtime.GetProxyDirtyFlags(proxy.Value()) != 0u)
    {
        return false;
    }
    const auto rendered = runtime.RenderFrame();
    return rendered && sink.submit_count == 2 && sink.last_revision == 9u && sink.last_view_revision == 9u && scene.requests >= 3;
}

[[nodiscard]] bool TestRenderFrameAbortOnSubmitFailure()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    sink.fail_submit = true;
    scene.AddNode(RenderTransformId{11});

    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{11}, ResourceId::FromString("mesh/fail"), ResourceId::FromString("mat/fail")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{11}, 75.0f, 0.1f, 2000.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame())
    {
        return false;
    }

    const auto rendered = runtime.RenderFrame();
    return !rendered && rendered.GetError().HasCode("renderer.submit_failed") &&
           sink.begin_count == 1 && sink.abort_count == 1 && runtime.GetFrameState() == RenderFrameState::Failed;
}

[[nodiscard]] bool TestPrepareFrameSkipsMissingResourceAndContinues()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{8});
    scene.AddNode(RenderTransformId{9});

    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto missing_mesh = ResourceId::FromString("mesh/missing");
    bridge.missing_resources.insert(missing_mesh);
    const auto missing = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{8}, missing_mesh, ResourceId::FromString("mat/missing")));
    const auto ready = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{9}, ResourceId::FromString("mesh/ready"), ResourceId::FromString("mat/ready")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{9}, 70.0f, 0.1f, 1500.0f});
    if (!missing || !ready || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame())
    {
        return false;
    }
    const auto rendered = runtime.RenderFrame();
    return rendered && runtime.GetFrameState() == RenderFrameState::Submitted &&
           runtime.GetProxyReadiness(missing.Value()) == RenderProxyReadiness::Loading &&
           runtime.GetProxyReadiness(ready.Value()) == RenderProxyReadiness::Ready && sink.submit_count == 1;
}

[[nodiscard]] bool TestPermanentResourceFailureMarksFailed()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{12});
    scene.AddNode(RenderTransformId{13});

    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto failed_mesh = ResourceId::FromString("mesh/permanent-fail");
    bridge.missing_resources.insert(failed_mesh);
    bridge.missing_mode = TestResourceBridge::MissingMode::Permanent;
    const auto failed = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{12}, failed_mesh, ResourceId::FromString("mat/permanent-fail")));
    const auto ready = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{13}, ResourceId::FromString("mesh/permanent-ready"), ResourceId::FromString("mat/permanent-ready")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{13}, 70.0f, 0.1f, 1500.0f});
    if (!failed || !ready || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame())
    {
        return false;
    }

    const auto rendered = runtime.RenderFrame();
    return rendered && runtime.GetProxyReadiness(failed.Value()) == RenderProxyReadiness::Failed &&
           runtime.GetProxyReadiness(ready.Value()) == RenderProxyReadiness::Ready && sink.submit_count == 1;
}

[[nodiscard]] bool TestShutdownBestEffortReleasesAllProxies()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{14});
    scene.AddNode(RenderTransformId{15});

    RendererRuntime runtime(&bridge, &scene);
    const auto first = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{14}, ResourceId::FromString("mesh/shutdown-a"), ResourceId::FromString("mat/shutdown-a")));
    const auto second = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{15}, ResourceId::FromString("mesh/shutdown-b"), ResourceId::FromString("mat/shutdown-b")));
    if (!first || !second)
    {
        return false;
    }

    bridge.fail_release = true;
    const auto shutdown = runtime.Shutdown();
    return !shutdown && shutdown.GetError().HasCode("renderer.release_failed") && bridge.release_count == 2;
}

[[nodiscard]] bool TestSubmissionOrderIsDeterministicByLayerThenProxyId()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{16});
    scene.AddNode(RenderTransformId{17});
    scene.AddNode(RenderTransformId{18});

    RendererRuntime runtime(&bridge, &scene, &sink);
    auto transparent_desc = MakeProxyDesc(RenderTransformId{16}, ResourceId::FromString("mesh/t"), ResourceId::FromString("mat/t"));
    transparent_desc.layer = RenderLayer::Transparent;
    auto sky_desc = MakeProxyDesc(RenderTransformId{17}, ResourceId::FromString("mesh/s"), ResourceId::FromString("mat/s"));
    sky_desc.layer = RenderLayer::Sky;
    auto opaque_desc = MakeProxyDesc(RenderTransformId{18}, ResourceId::FromString("mesh/o"), ResourceId::FromString("mat/o"));
    opaque_desc.layer = RenderLayer::Opaque;

    const auto transparent = runtime.RegisterProxy(transparent_desc);
    const auto sky = runtime.RegisterProxy(sky_desc);
    const auto opaque = runtime.RegisterProxy(opaque_desc);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{18}, 70.0f, 0.1f, 1500.0f});
    if (!transparent || !sky || !opaque || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame() || !runtime.RenderFrame())
    {
        return false;
    }

    return sink.submitted.size() == 3u && sink.submitted[0] == sky.Value() &&
           sink.submitted[1] == opaque.Value() && sink.submitted[2] == transparent.Value();
}

[[nodiscard]] bool TestReleaseErrorsSurfaceFromDeferredDestroy()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{10});

    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{10}, ResourceId::FromString("mesh/release"), ResourceId::FromString("mat/release")));
    if (!proxy || !runtime.DestroyProxy(proxy.Value()))
    {
        return false;
    }
    bridge.fail_release = true;
    const auto flush = runtime.FlushDeferredDestroys();
    return !flush && flush.GetError().HasCode("renderer.release_failed") &&
           runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::DestroyPending;
}

[[nodiscard]] bool TestStrictAndMockFactories()
{
    const auto strict_missing = CreateRendererServices({});
    if (strict_missing || !strict_missing.GetError().HasCode("renderer.resource_bridge_missing"))
    {
        return false;
    }
    auto bridge = std::make_shared<TestResourceBridge>();
    auto scene = std::make_shared<TestSceneSource>();
    RendererDependencies missing_sink_dependencies{};
    missing_sink_dependencies.resource_bridge = bridge;
    missing_sink_dependencies.scene_source = scene;
    const auto missing_sink = CreateRendererServices({}, missing_sink_dependencies);
    if (missing_sink || !missing_sink.GetError().HasCode("renderer.command_sink_missing"))
    {
        return false;
    }

    const auto mock = CreateMockRendererServices();
    if (!mock || !mock.Value().scene || !mock.Value().views || !mock.Value().runtime || !mock.Value().resource_bridge || !mock.Value().scene_source)
    {
        return false;
    }

    const auto proxy = mock.Value().scene->RegisterProxy(MakeProxyDesc(RenderTransformId{9}, ResourceId::FromString("mesh/m"), ResourceId::FromString("mat/m")));
    return proxy && mock.Value().scene->GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Ready &&
           mock.Value().command_sink;
}
} // namespace

int main()
{
    static_assert(std::is_abstract_v<IRenderScene>);
    static_assert(std::is_abstract_v<IViewSystem>);
    static_assert(std::is_abstract_v<IRendererRuntime>);
    static_assert(std::is_abstract_v<IRenderResourceBridge>);
    static_assert(std::is_abstract_v<IRenderSceneSource>);
    static_assert(std::is_abstract_v<IRenderCommandSink>);

    if (!TestRegisterProxyTracksSplitState()) return 1;
    if (!TestMissingResourceMarksNotReady()) return 2;
    if (!TestDirtyAndVisibilityFlags()) return 3;
    if (!TestDeferredProxyDestroy()) return 4;
    if (!TestViewLifecycleAndMainView()) return 5;
    if (!TestViewRejectsNonFiniteCameraParameters()) return 15;
    if (!TestFrameFlowEndsSubmittedAndClearsDirty()) return 6;
    if (!TestPrepareFrameRefreshesDirtyTransformBeforeSubmit()) return 7;
    if (!TestPrepareFrameSkipsMissingResourceAndContinues()) return 8;
    if (!TestPermanentResourceFailureMarksFailed()) return 12;
    if (!TestShutdownBestEffortReleasesAllProxies()) return 13;
    if (!TestSubmissionOrderIsDeterministicByLayerThenProxyId()) return 14;
    if (!TestReleaseErrorsSurfaceFromDeferredDestroy()) return 9;
    if (!TestRenderFrameAbortOnSubmitFailure()) return 10;
    if (!TestStrictAndMockFactories()) return 11;
    return 0;
}
