#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Renderer/view_system.h"
#include "renderer_runtime_impl.h"

#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace epidemic::runtime::renderer
{
struct RendererRuntimeTestAccess
{
    static void SetNextProxyValue(RendererRuntime& runtime, std::uint64_t value) { runtime.next_proxy_value_ = value; }
    static std::uint64_t NextProxyValue(const RendererRuntime& runtime) { return runtime.next_proxy_value_; }
    static void SetNextViewValue(RendererRuntime& runtime, std::uint64_t value) { runtime.next_view_value_ = value; }
    static std::uint64_t NextViewValue(const RendererRuntime& runtime) { return runtime.next_view_value_; }
    static void FailNextProxyPublication(RendererRuntime& runtime) { runtime.fail_next_proxy_publication_for_testing_ = true; }
    static std::size_t ProxyCount(const RendererRuntime& runtime) { return runtime.proxies_.size(); }
    static bool IsShutdownStarted(const RendererRuntime& runtime) { return runtime.shutdown_started_; }
    static bool IsShutdown(const RendererRuntime& runtime) { return runtime.shutdown_; }
    static bool IsFrameRecoveryPending(const RendererRuntime& runtime) { return runtime.frame_recovery_pending_; }
    static std::size_t PreparedSubmissionCount(const RendererRuntime& runtime) { return runtime.prepared_submissions_.size(); }
    static std::uint64_t CachedRevision(const RendererRuntime& runtime, RenderProxyId id)
    {
        const auto it = runtime.proxies_.find(id);
        return it == runtime.proxies_.end() ? 0u : it->second.cached_transform.revision;
    }
};
} // namespace epidemic::runtime::renderer

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
using epidemic::runtime::renderer::IRenderPoseSource;
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
using epidemic::runtime::renderer::RenderPoseBuffer;
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
    bool fail_acquire = false;
    bool fail_release = false;
    bool throw_acquire = false;
    bool throw_release = false;
    bool throw_get = false;
    MissingMode missing_mode = MissingMode::Temporary;
    std::unordered_set<ResourceId> missing_resources;
    std::unordered_set<ResourceId> fail_release_resources;
    std::vector<ResourceId> released_meshes;
    int acquire_count = 0;
    int release_count = 0;

    [[nodiscard]] Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) override
    {
        if (throw_acquire) throw std::runtime_error("acquire");
        if (!mesh.IsValid() || !material.IsValid())
        {
            return Result<void>::Failure(
                epidemic::foundation::Error::Create("renderer.invalid_resource", "test resource acquire requires valid resources"));
        }
        ++acquire_count;
        if (fail_acquire)
        {
            const char* code = missing_mode == MissingMode::Temporary ? "renderer.resource_not_ready" : "renderer.resource_failed";
            return Result<void>::Failure(epidemic::foundation::Error::Create(code, "acquire failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> ReleasePayloads(ResourceId mesh, ResourceId material) override
    {
        if (throw_release) throw std::runtime_error("release");
        (void)mesh;
        (void)material;
        ++release_count;
        released_meshes.push_back(mesh);
        if (fail_release || fail_release_resources.contains(mesh))
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.release_failed", "release failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<RenderResourcePayloads> GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        if (throw_get) throw std::runtime_error("get");
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
        if (throw_on_read) throw std::runtime_error("scene");
        if (!nodes.contains(node))
        {
            return Result<RenderTransformSnapshot>::Failure(
                epidemic::foundation::Error::Create("renderer.transform_missing", "test scene node is missing"));
        }
        Transform transform{};
        if (return_non_finite) transform.position.x = std::numeric_limits<float>::infinity();
        return Result<RenderTransformSnapshot>::Success(RenderTransformSnapshot{node, transform, revision});
    }

    std::unordered_set<RenderTransformId> nodes;
    bool throw_on_read = false;
    bool return_non_finite = false;
    mutable int requests = 0;
    std::uint64_t revision = 7u;
};

class TestPoseSource final : public IRenderPoseSource
{
  public:
    [[nodiscard]] Result<std::shared_ptr<const RenderPoseBuffer>> GetPose(RuntimeObjectId owner) const override
    {
        ++requests;
        if (throw_on_read) throw std::runtime_error("pose");
        if (fail_on_read)
        {
            return Result<std::shared_ptr<const RenderPoseBuffer>>::Failure(
                epidemic::foundation::Error::Create("renderer.pose_failed", "pose failed for test"));
        }
        if (!pose)
        {
            return Result<std::shared_ptr<const RenderPoseBuffer>>::Success({});
        }
        if (pose->owner == RuntimeObjectId{})
        {
            auto owned = std::make_shared<RenderPoseBuffer>(*pose);
            owned->owner = owner;
            return Result<std::shared_ptr<const RenderPoseBuffer>>::Success(std::move(owned));
        }
        return Result<std::shared_ptr<const RenderPoseBuffer>>::Success(pose);
    }

    std::shared_ptr<const RenderPoseBuffer> pose{};
    bool fail_on_read = false;
    bool throw_on_read = false;
    mutable int requests = 0;
};

class TestCommandSink final : public IRenderCommandSink
{
  public:
    [[nodiscard]] Result<void> BeginFrame(const RenderFrameContext& context) override
    {
        ++begin_count;
        if (throw_begin) throw std::runtime_error("begin");
        if (fail_begin)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.begin_failed", "begin failed for test"));
        }
        last_view = context.view;
        last_view_revision = context.transform.revision;
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> SubmitProxy(const RenderProxySubmission& submission) override
    {
        ++submit_count;
        if (throw_submit) throw std::runtime_error("submit");
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
        if (throw_end) throw std::runtime_error("end");
        if (fail_end)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.end_failed", "end failed for test"));
        }
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> AbortFrame() override
    {
        ++abort_count;
        if (throw_abort) throw std::runtime_error("abort");
        if (fail_abort)
        {
            return Result<void>::Failure(epidemic::foundation::Error::Create("renderer.abort_failed", "abort failed for test"));
        }
        return Result<void>::Success();
    }

    int begin_count = 0;
    int submit_count = 0;
    int end_count = 0;
    int abort_count = 0;
    bool fail_begin = false;
    bool fail_submit = false;
    bool fail_end = false;
    bool fail_abort = false;
    bool throw_begin = false;
    bool throw_submit = false;
    bool throw_end = false;
    bool throw_abort = false;
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
    if (runtime.GetFrameState() != RenderFrameState::ReadyToRender || runtime.GetProxyDirtyFlags(proxy.Value()) == 0u)
    {
        return false;
    }
    const auto render = runtime.RenderFrame();
    return render && runtime.GetFrameState() == RenderFrameState::Submitted && runtime.GetProxyDirtyFlags(proxy.Value()) == 0u &&
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
    if (!runtime.MarkTransformDirty(proxy.Value()) || !runtime.PrepareFrame() ||
        !HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform))
    {
        return false;
    }
    const auto rendered = runtime.RenderFrame();
    return rendered && runtime.GetProxyDirtyFlags(proxy.Value()) == 0u && sink.submit_count == 2 &&
           sink.last_revision == 9u && sink.last_view_revision == 9u && scene.requests >= 3;
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

[[nodiscard]] bool TestShutdownFailureStartsTerminalLifecycleAndIsRetryable()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{60});
    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{60}, ResourceId::FromString("mesh/shutdown-retry"), ResourceId::FromString("mat/shutdown-retry")));
    if (!proxy) return false;

    bridge.fail_release = true;
    const auto failed = runtime.Shutdown();
    if (failed || !failed.GetError().HasCode("renderer.release_failed") ||
        !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsShutdownStarted(runtime) ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::IsShutdown(runtime))
    {
        return false;
    }
    const auto dirty_during_shutdown = runtime.MarkMaterialDirty(proxy.Value());
    const auto register_during_shutdown = runtime.RegisterProxy(
        MakeProxyDesc(RenderTransformId{60}, ResourceId::FromString("mesh/late-during-shutdown"),
                      ResourceId::FromString("mat/late-during-shutdown")));
    if (dirty_during_shutdown || register_during_shutdown ||
        !dirty_during_shutdown.GetError().HasCode("renderer.shutdown_in_progress") ||
        !register_during_shutdown.GetError().HasCode("renderer.shutdown_in_progress"))
    {
        return false;
    }

    bridge.fail_release = false;
    if (!runtime.Shutdown() || !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsShutdown(runtime)) return false;
    const auto reg = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{60}, ResourceId::FromString("mesh/late"), ResourceId::FromString("mat/late")));
    const auto destroy = runtime.DestroyProxy(proxy.Value());
    const auto flush = runtime.FlushDeferredDestroys();
    const auto dirty = runtime.MarkTransformDirty(proxy.Value());
    const auto material = runtime.MarkMaterialDirty(proxy.Value());
    const auto visibility = runtime.SetProxyVisibility(proxy.Value(), RenderProxyVisibility::Hidden);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{60}, 60.0f, 0.1f, 100.0f});
    const auto destroy_view = runtime.DestroyView(epidemic::runtime::renderer::ViewId{1});
    const auto main = runtime.SetMainView(epidemic::runtime::renderer::ViewId{1});
    const auto prepare = runtime.PrepareFrame();
    const auto render = runtime.RenderFrame();
    return !reg && !destroy && !flush && !dirty && !material && !visibility && !view && !destroy_view && !main && !prepare && !render &&
           reg.GetError().HasCode("renderer.shutdown_complete") && render.GetError().HasCode("renderer.shutdown_complete");
}

[[nodiscard]] bool TestProxyPublicationFailureDoesNotAcquireOrConsumeId()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{61});
    RendererRuntime runtime(&bridge, &scene);
    const auto before = epidemic::runtime::renderer::RendererRuntimeTestAccess::NextProxyValue(runtime);
    epidemic::runtime::renderer::RendererRuntimeTestAccess::FailNextProxyPublication(runtime);
    const auto failed = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{61}, ResourceId::FromString("mesh/fault"), ResourceId::FromString("mat/fault")));
    if (failed || !failed.GetError().HasCode("renderer.allocation_failed") || bridge.acquire_count != 0 ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::ProxyCount(runtime) != 0u ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::NextProxyValue(runtime) != before)
    {
        return false;
    }
    const auto retry = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{61}, ResourceId::FromString("mesh/fault"), ResourceId::FromString("mat/fault")));
    return retry && retry.Value().value == before && bridge.acquire_count == 1;
}

[[nodiscard]] bool TestRendererEnumValidationAndIdExhaustion()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{62});
    RendererRuntime runtime(&bridge, &scene);
    auto invalid_layer = MakeProxyDesc(RenderTransformId{62}, ResourceId::FromString("mesh/enum"), ResourceId::FromString("mat/enum"));
    invalid_layer.layer = static_cast<RenderLayer>(999);
    auto invalid_visibility = invalid_layer;
    invalid_visibility.layer = RenderLayer::Opaque;
    invalid_visibility.visibility = static_cast<RenderProxyVisibility>(999);
    const auto a = runtime.RegisterProxy(invalid_layer);
    const auto b = runtime.RegisterProxy(invalid_visibility);
    if (a || b || epidemic::runtime::renderer::RendererRuntimeTestAccess::NextProxyValue(runtime) != 1u || bridge.acquire_count != 0) return false;

    epidemic::runtime::renderer::RendererRuntimeTestAccess::SetNextProxyValue(runtime, std::numeric_limits<std::uint64_t>::max());
    const auto last_proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{62}, ResourceId::FromString("mesh/last"), ResourceId::FromString("mat/last")));
    const auto exhausted_proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{62}, ResourceId::FromString("mesh/next"), ResourceId::FromString("mat/next")));
    epidemic::runtime::renderer::RendererRuntimeTestAccess::SetNextViewValue(runtime, std::numeric_limits<std::uint64_t>::max());
    const auto last_view = runtime.CreateView(ViewDesc{RenderTransformId{62}, 60.0f, 0.1f, 100.0f});
    const auto exhausted_view = runtime.CreateView(ViewDesc{RenderTransformId{62}, 60.0f, 0.1f, 100.0f});
    if (!last_proxy || exhausted_proxy || !last_view || exhausted_view) return false;
    const auto old_visibility = runtime.GetProxyVisibility(last_proxy.Value());
    const auto bad_set = runtime.SetProxyVisibility(last_proxy.Value(), static_cast<RenderProxyVisibility>(999));
    return !bad_set && bad_set.GetError().HasCode("renderer.invalid_enum") && runtime.GetProxyVisibility(last_proxy.Value()) == old_visibility &&
           exhausted_proxy.GetError().HasCode("renderer.proxy_id_exhausted") && exhausted_view.GetError().HasCode("renderer.view_id_exhausted");
}

[[nodiscard]] bool TestPrepareFrameFailureDoesNotPublishStagedTransforms()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{63});
    scene.AddNode(RenderTransformId{64});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto first = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{63}, ResourceId::FromString("mesh/stage-a"), ResourceId::FromString("mat/stage-a")));
    const auto second = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{64}, ResourceId::FromString("mesh/stage-b"), ResourceId::FromString("mat/stage-b")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{63}, 60.0f, 0.1f, 100.0f});
    if (!first || !second || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame() || !runtime.RenderFrame()) return false;
    const auto before_a = epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, first.Value());
    const auto before_b = epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, second.Value());
    scene.SetRevision(10u);
    if (!runtime.MarkTransformDirty(first.Value()) || !runtime.MarkTransformDirty(second.Value())) return false;
    scene.nodes.erase(RenderTransformId{64});
    const auto failed = runtime.PrepareFrame();
    return !failed && failed.GetError().HasCode("renderer.transform_missing") &&
           epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, first.Value()) == before_a &&
           epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, second.Value()) == before_b &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(first.Value()), RenderProxyDirtyFlags::Transform) &&
           HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(second.Value()), RenderProxyDirtyFlags::Transform);
}

[[nodiscard]] bool TestSceneAndCommandExceptionsAreContainedAndAbortAfterBegin()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{65});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{65}, ResourceId::FromString("mesh/ex"), ResourceId::FromString("mat/ex")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{65}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value())) return false;

    scene.throw_on_read = true;
    const auto source_error = runtime.PrepareFrame();
    scene.throw_on_read = false;
    if (source_error || !source_error.GetError().HasCode("renderer.backend_exception")) return false;
    if (!runtime.PrepareFrame()) return false;

    sink.throw_submit = true;
    sink.throw_abort = true;
    const auto submit_error = runtime.RenderFrame();
    if (submit_error || !submit_error.GetError().HasCode("renderer.backend_exception") || sink.begin_count != 1 || sink.abort_count != 1) return false;

    sink.throw_submit = false;
    sink.throw_abort = false;
    if (!runtime.PrepareFrame()) return false;
    sink.throw_end = true;
    const auto end_error = runtime.RenderFrame();
    return !end_error && end_error.GetError().HasCode("renderer.backend_exception") && sink.abort_count == 3;
}

[[nodiscard]] bool TestInvalidOrderAndNoMainViewDoNotBecomeStickyFailed()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto early = runtime.RenderFrame();
    if (early || !early.GetError().HasCode("renderer.frame_not_prepared") || runtime.GetFrameState() != RenderFrameState::NotPrepared) return false;
    if (!runtime.PrepareFrame() || runtime.GetFrameState() != RenderFrameState::ReadyToRender) return false;
    const auto no_view = runtime.RenderFrame();
    return !no_view && no_view.GetError().HasCode("renderer.main_view_missing") && runtime.GetFrameState() == RenderFrameState::ReadyToRender;
}

[[nodiscard]] bool TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{66});
    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{66}, ResourceId::FromString("mesh/drain"), ResourceId::FromString("mat/drain")));
    if (!proxy || !runtime.DestroyProxy(proxy.Value())) return false;
    bridge.fail_release = true;
    const auto failed = runtime.FlushDeferredDestroys();
    if (failed || runtime.GetProxyLifecycle(proxy.Value()) != RenderProxyLifecycle::DestroyPending) return false;
    bridge.fail_release = false;
    const auto retry = runtime.FlushDeferredDestroys();
    return retry && runtime.GetProxyLifecycle(proxy.Value()) == RenderProxyLifecycle::Unregistered && bridge.release_count == 2;
}

[[nodiscard]] bool TestNonFiniteSceneTransformIsRejectedBeforeBackendUse()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{67});
    scene.return_non_finite = true;
    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{67}, ResourceId::FromString("mesh/nan"), ResourceId::FromString("mat/nan")));
    return !proxy && proxy.GetError().HasCode("renderer.invalid_transform") && bridge.acquire_count == 0;
}

[[nodiscard]] bool TestAbortFailureRetainsRecoveryAndBlocksNewBegin()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{68});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{68}, ResourceId::FromString("mesh/recover"), ResourceId::FromString("mat/recover")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{68}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;

    sink.fail_submit = true;
    sink.fail_abort = true;
    const auto failed = runtime.RenderFrame();
    if (failed || !failed.GetError().HasCode("renderer.submit_failed") || sink.begin_count != 1 || sink.abort_count != 1 ||
        !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime))
    {
        return false;
    }

    sink.fail_submit = false;
    const auto blocked = runtime.RenderFrame();
    if (blocked || !blocked.GetError().HasCode("renderer.abort_failed") || sink.begin_count != 1 || sink.abort_count != 2 ||
        !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime))
    {
        return false;
    }

    sink.fail_abort = false;
    const auto retry = runtime.RenderFrame();
    return retry && sink.abort_count == 3 && sink.begin_count == 2 && sink.end_count == 1 &&
           !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime) &&
           runtime.GetProxyDirtyFlags(proxy.Value()) == 0u;
}

[[nodiscard]] bool TestShutdownReconcilesOpenFrameBeforeResourceRelease()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{69});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{69}, ResourceId::FromString("mesh/shutdown-open"), ResourceId::FromString("mat/shutdown-open")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{69}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;

    sink.fail_submit = true;
    sink.fail_abort = true;
    if (runtime.RenderFrame()) return false;
    const auto failed_shutdown = runtime.Shutdown();
    if (failed_shutdown || !failed_shutdown.GetError().HasCode("renderer.abort_failed") || bridge.release_count != 0 || sink.abort_count != 2)
    {
        return false;
    }

    sink.fail_abort = false;
    const auto retry = runtime.Shutdown();
    return retry && sink.abort_count == 3 && bridge.release_count == 1 &&
           epidemic::runtime::renderer::RendererRuntimeTestAccess::IsShutdown(runtime);
}

[[nodiscard]] bool TestPoseReadIsStagedBeforeBeginAndTransformCommitIsAtomic()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestPoseSource poses;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{70});
    RendererRuntime runtime(&bridge, &scene, &poses, &sink);
    auto desc = MakeProxyDesc(RenderTransformId{70}, ResourceId::FromString("mesh/pose-stage"), ResourceId::FromString("mat/pose-stage"));
    desc.owner = RuntimeObjectId{70};
    const auto proxy = runtime.RegisterProxy(desc);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{70}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame() || !runtime.RenderFrame()) return false;

    scene.SetRevision(12u);
    if (!runtime.MarkTransformDirty(proxy.Value())) return false;
    const auto before = epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, proxy.Value());
    poses.fail_on_read = true;
    const auto failed = runtime.PrepareFrame();
    if (failed || !failed.GetError().HasCode("renderer.pose_failed") || sink.begin_count != 1 ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, proxy.Value()) != before ||
        !HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform))
    {
        return false;
    }

    poses.fail_on_read = false;
    if (!runtime.PrepareFrame()) return false;
    if (epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, proxy.Value()) != before) return false;
    const auto rendered = runtime.RenderFrame();
    return rendered && sink.begin_count == 2 && epidemic::runtime::renderer::RendererRuntimeTestAccess::CachedRevision(runtime, proxy.Value()) == 12u &&
           !HasRenderDirtyFlag(runtime.GetProxyDirtyFlags(proxy.Value()), RenderProxyDirtyFlags::Transform);
}

[[nodiscard]] bool TestPoseExceptionAndInvalidPayloadAreRejectedBeforeBegin()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestPoseSource poses;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{71});
    RendererRuntime runtime(&bridge, &scene, &poses, &sink);
    auto desc = MakeProxyDesc(RenderTransformId{71}, ResourceId::FromString("mesh/pose-invalid"), ResourceId::FromString("mat/pose-invalid"));
    desc.owner = RuntimeObjectId{71};
    const auto proxy = runtime.RegisterProxy(desc);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{71}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value())) return false;

    poses.throw_on_read = true;
    const auto thrown = runtime.PrepareFrame();
    if (thrown || !thrown.GetError().HasCode("renderer.backend_exception") || sink.begin_count != 0) return false;

    poses.throw_on_read = false;
    auto invalid = std::make_shared<RenderPoseBuffer>();
    invalid->owner = RuntimeObjectId{999};
    poses.pose = invalid;
    const auto bad_owner = runtime.PrepareFrame();
    if (bad_owner || !bad_owner.GetError().HasCode("renderer.invalid_pose") || sink.begin_count != 0) return false;

    auto invalid_transform = std::make_shared<RenderPoseBuffer>();
    invalid_transform->owner = RuntimeObjectId{71};
    Transform bad{};
    bad.position.x = std::numeric_limits<float>::infinity();
    invalid_transform->bone_transforms.push_back(bad);
    poses.pose = invalid_transform;
    const auto bad_pose = runtime.PrepareFrame();
    return !bad_pose && bad_pose.GetError().HasCode("renderer.invalid_pose") && sink.begin_count == 0;
}

[[nodiscard]] bool TestPermanentAcquireFailureIsFailedAndRetryable()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{72});
    bridge.fail_acquire = true;
    bridge.missing_mode = TestResourceBridge::MissingMode::Permanent;
    RendererRuntime runtime(&bridge, &scene);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{72}, ResourceId::FromString("mesh/acquire-fail"), ResourceId::FromString("mat/acquire-fail")));
    if (!proxy || runtime.GetProxyReadiness(proxy.Value()) != RenderProxyReadiness::Failed || bridge.acquire_count != 1) return false;

    bridge.fail_acquire = false;
    const auto prepared = runtime.PrepareFrame();
    return prepared && runtime.GetProxyReadiness(proxy.Value()) == RenderProxyReadiness::Ready && bridge.acquire_count == 2;
}

[[nodiscard]] bool TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{73});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{73}, ResourceId::FromString("mesh/dirty-commit"), ResourceId::FromString("mat/dirty-commit")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{73}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;
    const auto staged_flags = runtime.GetProxyDirtyFlags(proxy.Value());
    if (staged_flags == 0u) return false;

    sink.fail_submit = true;
    const auto failed = runtime.RenderFrame();
    if (failed || runtime.GetProxyDirtyFlags(proxy.Value()) != staged_flags) return false;

    sink.fail_submit = false;
    if (!runtime.PrepareFrame()) return false;
    const auto success = runtime.RenderFrame();
    return success && runtime.GetProxyDirtyFlags(proxy.Value()) == 0u;
}

[[nodiscard]] bool TestBeginFailureDoesNotAbortOrCreateRecoveryOwnership()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{74});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{74}, ResourceId::FromString("mesh/begin"), ResourceId::FromString("mat/begin")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{74}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;

    sink.fail_begin = true;
    const auto failed = runtime.RenderFrame();
    return !failed && failed.GetError().HasCode("renderer.begin_failed") && sink.begin_count == 1 && sink.abort_count == 0 &&
           !epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime);
}

[[nodiscard]] bool TestDeferredDestroyPreservesDeterministicFailedPrefix()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{75});
    scene.AddNode(RenderTransformId{76});
    scene.AddNode(RenderTransformId{77});
    RendererRuntime runtime(&bridge, &scene);
    const auto mesh_a = ResourceId::FromString("mesh/prefix-a");
    const auto mesh_b = ResourceId::FromString("mesh/prefix-b");
    const auto mesh_c = ResourceId::FromString("mesh/prefix-c");
    const auto a = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{75}, mesh_a, ResourceId::FromString("mat/prefix-a")));
    const auto b = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{76}, mesh_b, ResourceId::FromString("mat/prefix-b")));
    const auto c = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{77}, mesh_c, ResourceId::FromString("mat/prefix-c")));
    if (!a || !b || !c || !runtime.DestroyProxy(a.Value()) || !runtime.DestroyProxy(b.Value()) || !runtime.DestroyProxy(c.Value())) return false;

    bridge.fail_release_resources.insert(mesh_b);
    const auto failed = runtime.FlushDeferredDestroys();
    if (failed || bridge.released_meshes.size() != 2u || bridge.released_meshes[0] != mesh_a || bridge.released_meshes[1] != mesh_b ||
        runtime.GetProxyLifecycle(a.Value()) != RenderProxyLifecycle::Unregistered ||
        runtime.GetProxyLifecycle(b.Value()) != RenderProxyLifecycle::DestroyPending ||
        runtime.GetProxyLifecycle(c.Value()) != RenderProxyLifecycle::DestroyPending)
    {
        return false;
    }

    bridge.fail_release_resources.clear();
    const auto retry = runtime.FlushDeferredDestroys();
    return retry && bridge.released_meshes.size() == 4u && bridge.released_meshes[2] == mesh_b && bridge.released_meshes[3] == mesh_c &&
           runtime.GetProxyLifecycle(b.Value()) == RenderProxyLifecycle::Unregistered &&
           runtime.GetProxyLifecycle(c.Value()) == RenderProxyLifecycle::Unregistered;
}

[[nodiscard]] bool TestResourceBridgeExceptionsPreserveOwnedRetryState()
{
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{78});

    TestResourceBridge acquire_bridge;
    acquire_bridge.throw_acquire = true;
    RendererRuntime acquire_runtime(&acquire_bridge, &scene);
    const auto acquire_proxy = acquire_runtime.RegisterProxy(
        MakeProxyDesc(RenderTransformId{78}, ResourceId::FromString("mesh/acquire-ex"), ResourceId::FromString("mat/acquire-ex")));
    if (!acquire_proxy || acquire_runtime.GetProxyReadiness(acquire_proxy.Value()) != RenderProxyReadiness::Failed) return false;
    acquire_bridge.throw_acquire = false;
    if (!acquire_runtime.PrepareFrame() || acquire_runtime.GetProxyReadiness(acquire_proxy.Value()) != RenderProxyReadiness::Ready) return false;

    TestResourceBridge get_bridge;
    get_bridge.throw_get = true;
    RendererRuntime get_runtime(&get_bridge, &scene);
    const auto get_proxy = get_runtime.RegisterProxy(
        MakeProxyDesc(RenderTransformId{78}, ResourceId::FromString("mesh/get-ex"), ResourceId::FromString("mat/get-ex")));
    if (!get_proxy || get_runtime.GetProxyReadiness(get_proxy.Value()) != RenderProxyReadiness::Failed || get_bridge.acquire_count != 1) return false;
    get_bridge.throw_get = false;
    if (!get_runtime.PrepareFrame() || get_runtime.GetProxyReadiness(get_proxy.Value()) != RenderProxyReadiness::Ready || get_bridge.acquire_count != 1) return false;

    TestResourceBridge release_bridge;
    RendererRuntime release_runtime(&release_bridge, &scene);
    const auto release_proxy = release_runtime.RegisterProxy(
        MakeProxyDesc(RenderTransformId{78}, ResourceId::FromString("mesh/release-ex"), ResourceId::FromString("mat/release-ex")));
    if (!release_proxy || !release_runtime.DestroyProxy(release_proxy.Value())) return false;
    release_bridge.throw_release = true;
    const auto failed_release = release_runtime.FlushDeferredDestroys();
    if (failed_release || !failed_release.GetError().HasCode("renderer.backend_exception") ||
        release_runtime.GetProxyLifecycle(release_proxy.Value()) != RenderProxyLifecycle::DestroyPending) return false;
    release_bridge.throw_release = false;
    return release_runtime.FlushDeferredDestroys() &&
           release_runtime.GetProxyLifecycle(release_proxy.Value()) == RenderProxyLifecycle::Unregistered;
}

[[nodiscard]] bool TestProxyDescriptorValidationIsPrePublication()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    scene.AddNode(RenderTransformId{79});
    RendererRuntime runtime(&bridge, &scene);
    const auto start_id = epidemic::runtime::renderer::RendererRuntimeTestAccess::NextProxyValue(runtime);

    auto invalid_owner = MakeProxyDesc(RenderTransformId{79}, ResourceId::FromString("mesh/valid"), ResourceId::FromString("mat/valid"));
    invalid_owner.owner = {};
    auto invalid_mesh = MakeProxyDesc(RenderTransformId{79}, {}, ResourceId::FromString("mat/valid"));
    auto invalid_material = MakeProxyDesc(RenderTransformId{79}, ResourceId::FromString("mesh/valid"), {});
    auto invalid_transform = MakeProxyDesc({}, ResourceId::FromString("mesh/valid"), ResourceId::FromString("mat/valid"));

    const auto a = runtime.RegisterProxy(invalid_owner);
    const auto b = runtime.RegisterProxy(invalid_mesh);
    const auto c = runtime.RegisterProxy(invalid_material);
    const auto d = runtime.RegisterProxy(invalid_transform);
    return !a && !b && !c && !d && a.GetError().HasCode("renderer.invalid_owner") &&
           b.GetError().HasCode("renderer.invalid_resource") && c.GetError().HasCode("renderer.invalid_resource") &&
           d.GetError().HasCode("renderer.invalid_transform") && bridge.acquire_count == 0 &&
           epidemic::runtime::renderer::RendererRuntimeTestAccess::NextProxyValue(runtime) == start_id;
}

[[nodiscard]] bool TestMainViewDestroyPendingAndMissingBehavior()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{80});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{80}, 60.0f, 0.1f, 100.0f});
    if (!view || runtime.SetMainView({}) || runtime.SetMainView(epidemic::runtime::renderer::ViewId{999}) || !runtime.SetMainView(view.Value())) return false;
    if (!runtime.DestroyView(view.Value())) return false;
    const auto pending = runtime.PrepareFrame();
    if (pending || !pending.GetError().HasCode("renderer.invalid_main_view") || sink.begin_count != 0) return false;
    if (!runtime.FlushDeferredDestroys() || runtime.GetMainView().IsValid()) return false;
    if (!runtime.PrepareFrame()) return false;
    const auto missing = runtime.RenderFrame();
    return !missing && missing.GetError().HasCode("renderer.main_view_missing") && sink.begin_count == 0;
}

[[nodiscard]] bool TestRepeatedPrepareReplacesStagedFrameAndRepeatedRenderIsRejected()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestPoseSource poses;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{81});
    RendererRuntime runtime(&bridge, &scene, &poses, &sink);
    auto desc = MakeProxyDesc(RenderTransformId{81}, ResourceId::FromString("mesh/reprepare"), ResourceId::FromString("mat/reprepare"));
    desc.owner = RuntimeObjectId{81};
    const auto proxy = runtime.RegisterProxy(desc);
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{81}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;
    scene.SetRevision(9u);
    if (!runtime.MarkTransformDirty(proxy.Value()) || !runtime.PrepareFrame()) return false;
    if (sink.begin_count != 0 || poses.requests != 2 ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::PreparedSubmissionCount(runtime) != 1u) return false;
    if (!runtime.RenderFrame() || sink.last_revision != 9u || sink.begin_count != 1) return false;
    const auto repeated = runtime.RenderFrame();
    return !repeated && repeated.GetError().HasCode("renderer.frame_not_prepared") && sink.begin_count == 1;
}

[[nodiscard]] bool TestBeginAndEndExceptionFailureMatrix()
{
    TestResourceBridge bridge;
    TestSceneSource scene;
    TestCommandSink sink;
    scene.AddNode(RenderTransformId{82});
    RendererRuntime runtime(&bridge, &scene, &sink);
    const auto proxy = runtime.RegisterProxy(MakeProxyDesc(RenderTransformId{82}, ResourceId::FromString("mesh/cmd-matrix"), ResourceId::FromString("mat/cmd-matrix")));
    const auto view = runtime.CreateView(ViewDesc{RenderTransformId{82}, 60.0f, 0.1f, 100.0f});
    if (!proxy || !view || !runtime.SetMainView(view.Value()) || !runtime.PrepareFrame()) return false;

    sink.throw_begin = true;
    const auto begin_throw = runtime.RenderFrame();
    if (begin_throw || !begin_throw.GetError().HasCode("renderer.backend_exception") || sink.abort_count != 0 ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime)) return false;

    sink.throw_begin = false;
    if (!runtime.PrepareFrame()) return false;
    sink.fail_end = true;
    const auto end_fail = runtime.RenderFrame();
    if (end_fail || !end_fail.GetError().HasCode("renderer.end_failed") || sink.abort_count != 1 ||
        epidemic::runtime::renderer::RendererRuntimeTestAccess::IsFrameRecoveryPending(runtime)) return false;

    sink.fail_end = false;
    return runtime.PrepareFrame() && runtime.RenderFrame();
}

[[nodiscard]] bool TestStrictAndMockFactories()
{
    const auto strict_missing = CreateRendererServices({});
    const auto strict_even_with_mock_option = CreateRendererServices({true});
    if (strict_missing || !strict_missing.GetError().HasCode("renderer.resource_bridge_missing") ||
        strict_even_with_mock_option || !strict_even_with_mock_option.GetError().HasCode("renderer.resource_bridge_missing"))
    {
        return false;
    }
    auto bridge = std::make_shared<TestResourceBridge>();
    auto scene = std::make_shared<TestSceneSource>();
    RendererDependencies missing_scene_dependencies{};
    missing_scene_dependencies.resource_bridge = bridge;
    missing_scene_dependencies.command_sink = std::make_shared<TestCommandSink>();
    const auto missing_scene = CreateRendererServices({}, missing_scene_dependencies);
    if (missing_scene || !missing_scene.GetError().HasCode("renderer.scene_source_missing"))
    {
        return false;
    }

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
    static_assert(std::is_abstract_v<IRenderPoseSource>);
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
    if (!TestShutdownFailureStartsTerminalLifecycleAndIsRetryable()) return 16;
    if (!TestProxyPublicationFailureDoesNotAcquireOrConsumeId()) return 17;
    if (!TestRendererEnumValidationAndIdExhaustion()) return 18;
    if (!TestPrepareFrameFailureDoesNotPublishStagedTransforms()) return 19;
    if (!TestSceneAndCommandExceptionsAreContainedAndAbortAfterBegin()) return 20;
    if (!TestInvalidOrderAndNoMainViewDoNotBecomeStickyFailed()) return 21;
    if (!TestDeferredDestroyReleaseFailureIsRetryablePrefixProgress()) return 22;
    if (!TestNonFiniteSceneTransformIsRejectedBeforeBackendUse()) return 23;
    if (!TestAbortFailureRetainsRecoveryAndBlocksNewBegin()) return 24;
    if (!TestShutdownReconcilesOpenFrameBeforeResourceRelease()) return 25;
    if (!TestPoseReadIsStagedBeforeBeginAndTransformCommitIsAtomic()) return 26;
    if (!TestPoseExceptionAndInvalidPayloadAreRejectedBeforeBegin()) return 27;
    if (!TestPermanentAcquireFailureIsFailedAndRetryable()) return 28;
    if (!TestDirtyFlagsCommitOnlyAfterSuccessfulBackendFrame()) return 29;
    if (!TestBeginFailureDoesNotAbortOrCreateRecoveryOwnership()) return 30;
    if (!TestDeferredDestroyPreservesDeterministicFailedPrefix()) return 31;
    if (!TestResourceBridgeExceptionsPreserveOwnedRetryState()) return 32;
    if (!TestProxyDescriptorValidationIsPrePublication()) return 33;
    if (!TestMainViewDestroyPendingAndMissingBehavior()) return 34;
    if (!TestRepeatedPrepareReplacesStagedFrameAndRepeatedRenderIsRejected()) return 35;
    if (!TestBeginAndEndExceptionFailureMatrix()) return 36;
    return 0;
}
