#pragma once

#include "Epidemic/Runtime/Renderer/render_resource_bridge.h"
#include "Epidemic/Runtime/Renderer/render_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_runtime.h"
#include "Epidemic/Runtime/Renderer/view_system.h"

#include <unordered_map>

namespace epidemic::runtime::renderer
{
// File note:
// In-memory Renderer foundation used for deterministic tests and early module integration.
// It stores proxies and views, validates Scene/Resources references and runs a mock frame flow.
class RendererRuntime final : public IRenderScene, public IViewSystem, public IRendererRuntime
{
  public:
    explicit RendererRuntime(
        IRenderResourceBridge* resource_bridge = nullptr,
        IRenderSceneSource* scene_source = nullptr);

    [[nodiscard]] foundation::Result<RenderProxyId> RegisterProxy(const RenderProxyDesc& desc) override;
    [[nodiscard]] foundation::Result<void> UnregisterProxy(RenderProxyId id) override;
    [[nodiscard]] foundation::Result<void> MarkTransformDirty(RenderProxyId id) override;
    [[nodiscard]] foundation::Result<void> MarkMaterialDirty(RenderProxyId id) override;
    [[nodiscard]] RenderProxyState GetProxyState(RenderProxyId id) const override;

    [[nodiscard]] foundation::Result<ViewId> CreateView(const ViewDesc& desc) override;
    [[nodiscard]] foundation::Result<void> SetMainView(ViewId view) override;
    [[nodiscard]] ViewId GetMainView() const override;

    [[nodiscard]] foundation::Result<void> PrepareFrame() override;
    [[nodiscard]] foundation::Result<void> RenderFrame() override;
    [[nodiscard]] RenderFrameState GetFrameState() const override;

  private:
    struct ProxyRecord
    {
        RenderProxyDesc desc{};
        RenderProxyState state = RenderProxyState::Unregistered;
        bool transform_dirty = false;
        bool material_dirty = false;
    };

    struct ViewRecord
    {
        ViewDesc desc{};
    };

    [[nodiscard]] bool HasValidTransform(SceneNodeId node) const;
    [[nodiscard]] bool IsResourceReady(ResourceId id) const;
    void RefreshProxyState(ProxyRecord& record);
    [[nodiscard]] ProxyRecord* FindProxy(RenderProxyId id);
    [[nodiscard]] const ProxyRecord* FindProxy(RenderProxyId id) const;
    [[nodiscard]] ViewRecord* FindView(ViewId id);
    [[nodiscard]] const ViewRecord* FindView(ViewId id) const;

    IRenderResourceBridge* resource_bridge_ = nullptr;
    IRenderSceneSource* scene_source_ = nullptr;
    std::unordered_map<RenderProxyId, ProxyRecord> proxies_;
    std::unordered_map<ViewId, ViewRecord> views_;
    ViewId main_view_{};
    RenderFrameState frame_state_ = RenderFrameState::NotPrepared;
    std::uint64_t next_proxy_value_ = 1;
    std::uint64_t next_view_value_ = 1;
};
} // namespace epidemic::runtime::renderer
