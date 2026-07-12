#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime::renderer
{
RendererRuntime::RendererRuntime(
    IRenderResourceBridge* resource_bridge,
    IRenderSceneSource* scene_source)
    : resource_bridge_(resource_bridge),
      scene_source_(scene_source)
{
}

foundation::Result<RenderProxyId> RendererRuntime::RegisterProxy(const RenderProxyDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return foundation::Result<RenderProxyId>::Failure(
            foundation::Error::Create("renderer.invalid_owner", "render proxy owner must be valid before registration"));
    }

    if (!HasValidTransform(desc.transform_node))
    {
        return foundation::Result<RenderProxyId>::Failure(
            foundation::Error::Create("renderer.invalid_transform", "render proxy must reference a valid scene transform node"));
    }

    const RenderProxyId proxy_id{next_proxy_value_++};
    ProxyRecord record{};
    record.desc = desc;
    record.state = RenderProxyState::Registered;
    RefreshProxyState(record);
    proxies_.emplace(proxy_id, record);
    return foundation::Result<RenderProxyId>::Success(proxy_id);
}

foundation::Result<void> RendererRuntime::UnregisterProxy(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.proxy_not_found", "render proxy was not found for unregistration"));
    }

    record->state = RenderProxyState::Destroyed;
    proxies_.erase(id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::MarkTransformDirty(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.proxy_not_found", "render proxy was not found for transform dirty marking"));
    }

    record->transform_dirty = true;
    record->state = RenderProxyState::DirtyTransform;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::MarkMaterialDirty(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.proxy_not_found", "render proxy was not found for material dirty marking"));
    }

    record->material_dirty = true;
    record->state = RenderProxyState::DirtyMaterial;
    return foundation::Result<void>::Success();
}

RenderProxyState RendererRuntime::GetProxyState(RenderProxyId id) const
{
    const ProxyRecord* record = FindProxy(id);
    if (record == nullptr)
    {
        return RenderProxyState::Unregistered;
    }

    return record->state;
}

foundation::Result<ViewId> RendererRuntime::CreateView(const ViewDesc& desc)
{
    if (!HasValidTransform(desc.transform_node))
    {
        return foundation::Result<ViewId>::Failure(
            foundation::Error::Create("renderer.invalid_view_transform", "view must reference a valid scene transform node"));
    }

    if (desc.vertical_fov <= 0.0f || desc.near_plane <= 0.0f || desc.far_plane <= desc.near_plane)
    {
        return foundation::Result<ViewId>::Failure(
            foundation::Error::Create("renderer.invalid_view", "view descriptor contains invalid clip or field-of-view values"));
    }

    const ViewId view_id{next_view_value_++};
    views_.emplace(view_id, ViewRecord{desc});
    return foundation::Result<ViewId>::Success(view_id);
}

foundation::Result<void> RendererRuntime::SetMainView(ViewId view)
{
    if (!view.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.invalid_view", "main view id must be valid before selection"));
    }

    if (FindView(view) == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.view_not_found", "view was not found for main view selection"));
    }

    main_view_ = view;
    return foundation::Result<void>::Success();
}

ViewId RendererRuntime::GetMainView() const
{
    return main_view_;
}

foundation::Result<void> RendererRuntime::PrepareFrame()
{
    frame_state_ = RenderFrameState::Preparing;

    if (main_view_.IsValid())
    {
        const ViewRecord* main_view = FindView(main_view_);
        if (main_view == nullptr || !HasValidTransform(main_view->desc.transform_node))
        {
            frame_state_ = RenderFrameState::Failed;
            return foundation::Result<void>::Failure(
                foundation::Error::Create("renderer.invalid_main_view", "main view is missing or references an invalid scene node"));
        }
    }

    for (auto& [proxy_id, proxy] : proxies_)
    {
        (void)proxy_id;
        RefreshProxyState(proxy);
    }

    frame_state_ = RenderFrameState::ReadyToRender;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::RenderFrame()
{
    if (frame_state_ != RenderFrameState::ReadyToRender)
    {
        frame_state_ = RenderFrameState::Failed;
        return foundation::Result<void>::Failure(
            foundation::Error::Create("renderer.frame_not_prepared", "render frame requires PrepareFrame to complete successfully first"));
    }

    frame_state_ = RenderFrameState::Rendering;
    for (auto& [proxy_id, proxy] : proxies_)
    {
        (void)proxy_id;
        if (proxy.state == RenderProxyState::Ready)
        {
            proxy.state = RenderProxyState::Visible;
        }
    }

    frame_state_ = RenderFrameState::Presented;
    return foundation::Result<void>::Success();
}

RenderFrameState RendererRuntime::GetFrameState() const
{
    return frame_state_;
}

bool RendererRuntime::HasValidTransform(SceneNodeId node) const
{
    if (!node.IsValid())
    {
        return false;
    }

    if (scene_source_ == nullptr)
    {
        return true;
    }

    return scene_source_->HasNode(node);
}

bool RendererRuntime::IsResourceReady(ResourceId id) const
{
    if (!id.IsValid())
    {
        return false;
    }

    if (resource_bridge_ == nullptr)
    {
        return true;
    }

    return resource_bridge_->GetResourceState(id) == ResourceState::Ready;
}

void RendererRuntime::RefreshProxyState(ProxyRecord& record)
{
    if (!IsResourceReady(record.desc.mesh) || !IsResourceReady(record.desc.material))
    {
        record.state = RenderProxyState::ResourceMissing;
        return;
    }

    if (record.transform_dirty)
    {
        record.transform_dirty = false;
        record.state = RenderProxyState::DirtyTransform;
        return;
    }

    if (record.material_dirty)
    {
        record.material_dirty = false;
        record.state = RenderProxyState::DirtyMaterial;
        return;
    }

    record.state = RenderProxyState::Ready;
}

RendererRuntime::ProxyRecord* RendererRuntime::FindProxy(RenderProxyId id)
{
    const auto iterator = proxies_.find(id);
    if (iterator == proxies_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const RendererRuntime::ProxyRecord* RendererRuntime::FindProxy(RenderProxyId id) const
{
    const auto iterator = proxies_.find(id);
    if (iterator == proxies_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

RendererRuntime::ViewRecord* RendererRuntime::FindView(ViewId id)
{
    const auto iterator = views_.find(id);
    if (iterator == views_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const RendererRuntime::ViewRecord* RendererRuntime::FindView(ViewId id) const
{
    const auto iterator = views_.find(id);
    if (iterator == views_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}
} // namespace epidemic::runtime::renderer
