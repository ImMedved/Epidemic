#pragma once

#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/view_system.h"

#include <optional>
#include <unordered_map>

namespace epidemic::runtime::renderer
{
class RendererRuntime final : public IRenderScene, public IViewSystem, public IRendererRuntime
{
  public:
    explicit RendererRuntime(IRenderResourceBridge* resource_bridge, IRenderSceneSource* scene_source);
    RendererRuntime(IRenderResourceBridge* resource_bridge, IRenderSceneSource* scene_source, IRenderCommandSink* command_sink);
    RendererRuntime(IRenderResourceBridge* resource_bridge,
                    IRenderSceneSource* scene_source,
                    IRenderPoseSource* pose_source,
                    IRenderCommandSink* command_sink);
    ~RendererRuntime() override;

    [[nodiscard]] foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyProxy(RenderProxyId id) override;
    [[nodiscard]] foundation::Result<void> FlushDeferredDestroys() override;
    [[nodiscard]] foundation::Result<void> MarkTransformDirty(RenderProxyId id) override;
    [[nodiscard]] foundation::Result<void> MarkMaterialDirty(RenderProxyId id) override;
    [[nodiscard]] foundation::Result<void> SetProxyVisibility(RenderProxyId id, RenderProxyVisibility visibility) override;
    [[nodiscard]] RenderProxyLifecycle GetProxyLifecycle(RenderProxyId id) const override;
    [[nodiscard]] RenderProxyReadiness GetProxyReadiness(RenderProxyId id) const override;
    [[nodiscard]] RenderProxyVisibility GetProxyVisibility(RenderProxyId id) const override;
    [[nodiscard]] RenderProxyDirtyMask GetProxyDirtyFlags(RenderProxyId id) const override;

    [[nodiscard]] foundation::Result<ViewId> CreateView(const ViewDesc& desc) override;
    [[nodiscard]] foundation::Result<void> DestroyView(ViewId view) override;
    [[nodiscard]] foundation::Result<void> SetMainView(ViewId view) override;
    [[nodiscard]] ViewId GetMainView() const override;
    [[nodiscard]] ViewLifecycle GetViewLifecycle(ViewId view) const override;

    [[nodiscard]] foundation::Result<void> PrepareFrame() override;
    [[nodiscard]] foundation::Result<void> RenderFrame() override;
    [[nodiscard]] foundation::Result<void> Shutdown() override;
    [[nodiscard]] RenderFrameState GetFrameState() const override;

  private:
    struct ProxyRecord
    {
        RenderProxyDesc desc{};
        RenderProxyLifecycle lifecycle = RenderProxyLifecycle::Unregistered;
        RenderProxyReadiness readiness = RenderProxyReadiness::MissingResources;
        RenderProxyVisibility visibility = RenderProxyVisibility::Visible;
        RenderProxyDirtyMask dirty_flags = 0;
        RenderResourcePayloads payloads{};
        RenderTransformSnapshot cached_transform{};
        bool resources_acquired = false;
    };

    struct ViewRecord
    {
        ViewDesc desc{};
        ViewLifecycle lifecycle = ViewLifecycle::Active;
    };

    [[nodiscard]] foundation::Result<RenderTransformSnapshot> GetTransform(RenderTransformId node) const;
    [[nodiscard]] foundation::Result<void> AcquireProxyResources(ProxyRecord& record);
    [[nodiscard]] foundation::Result<void> ReleaseProxyResources(ProxyRecord& record);
    [[nodiscard]] foundation::Result<void> RefreshProxyTransform(ProxyRecord& record);
    [[nodiscard]] foundation::Result<void> RefreshProxyReadiness(ProxyRecord& record);
    [[nodiscard]] ProxyRecord* FindProxy(RenderProxyId id);
    [[nodiscard]] const ProxyRecord* FindProxy(RenderProxyId id) const;
    [[nodiscard]] ViewRecord* FindView(ViewId id);
    [[nodiscard]] const ViewRecord* FindView(ViewId id) const;

    IRenderResourceBridge* resource_bridge_ = nullptr;
    IRenderSceneSource* scene_source_ = nullptr;
    IRenderPoseSource* pose_source_ = nullptr;
    IRenderCommandSink* command_sink_ = nullptr;
    std::unordered_map<RenderProxyId, ProxyRecord> proxies_;
    std::unordered_map<ViewId, ViewRecord> views_;
    ViewId main_view_{};
    std::optional<RenderFrameContext> prepared_context_{};
    RenderFrameState frame_state_ = RenderFrameState::NotPrepared;
    bool shutdown_ = false;
    std::uint64_t next_proxy_value_ = 1;
    std::uint64_t next_view_value_ = 1;
};
} // namespace epidemic::runtime::renderer
