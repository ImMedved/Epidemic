#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <vector>
#include <string_view>

namespace epidemic::runtime::renderer
{
namespace
{
[[nodiscard]] foundation::Result<void> RendererFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> RendererFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}
} // namespace

RendererRuntime::RendererRuntime(IRenderResourceBridge* resource_bridge, IRenderSceneSource* scene_source)
    : resource_bridge_(resource_bridge), scene_source_(scene_source)
{
}

RendererRuntime::~RendererRuntime()
{
    for (auto& [id, record] : proxies_)
    {
        (void)id;
        ReleaseProxyResources(record);
    }
}

foundation::Result<RenderProxyId> RendererRuntime::RegisterProxy(const RenderProxyDesc& desc)
{
    if (!desc.owner.IsValid())
    {
        return RendererFailureValue<RenderProxyId>("renderer.invalid_owner", "render proxy owner must be valid before registration");
    }
    if (!desc.mesh.IsValid() || !desc.material.IsValid())
    {
        return RendererFailureValue<RenderProxyId>("renderer.invalid_resource", "render proxy must reference valid mesh and material ids");
    }
    const auto transform = GetTransform(desc.transform_node);
    if (!transform)
    {
        return foundation::Result<RenderProxyId>::Failure(transform.GetError());
    }

    const RenderProxyId proxy_id{next_proxy_value_++};
    ProxyRecord record{};
    record.desc = desc;
    record.lifecycle = RenderProxyLifecycle::Registered;
    record.visibility = desc.visibility;
    record.dirty_flags = ToRenderDirtyMask(RenderProxyDirtyFlags::Transform) | ToRenderDirtyMask(RenderProxyDirtyFlags::Material);
    const auto acquired = AcquireProxyResources(record);
    if (!acquired)
    {
        return foundation::Result<RenderProxyId>::Failure(acquired.GetError());
    }
    const auto readiness = RefreshProxyReadiness(record);
    if (!readiness)
    {
        ReleaseProxyResources(record);
        return foundation::Result<RenderProxyId>::Failure(readiness.GetError());
    }
    proxies_.emplace(proxy_id, std::move(record));
    return foundation::Result<RenderProxyId>::Success(proxy_id);
}

foundation::Result<void> RendererRuntime::DestroyProxy(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr)
    {
        return RendererFailure("renderer.proxy_not_found", "render proxy was not found for destruction");
    }
    record->lifecycle = RenderProxyLifecycle::DestroyPending;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::FlushDeferredDestroys()
{
    std::vector<RenderProxyId> proxies_to_remove;
    for (const auto& [id, record] : proxies_)
    {
        if (record.lifecycle == RenderProxyLifecycle::DestroyPending)
        {
            proxies_to_remove.push_back(id);
        }
    }
    for (RenderProxyId id : proxies_to_remove)
    {
        ReleaseProxyResources(proxies_.at(id));
        proxies_.erase(id);
    }

    std::vector<ViewId> views_to_remove;
    for (const auto& [id, record] : views_)
    {
        if (record.lifecycle == ViewLifecycle::DestroyPending)
        {
            views_to_remove.push_back(id);
        }
    }
    for (ViewId id : views_to_remove)
    {
        if (main_view_ == id)
        {
            main_view_ = {};
        }
        views_.erase(id);
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::MarkTransformDirty(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr || record->lifecycle != RenderProxyLifecycle::Registered)
    {
        return RendererFailure("renderer.proxy_not_found", "render proxy was not found for transform dirty marking");
    }
    record->dirty_flags = AddRenderDirtyFlag(record->dirty_flags, RenderProxyDirtyFlags::Transform);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::MarkMaterialDirty(RenderProxyId id)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr || record->lifecycle != RenderProxyLifecycle::Registered)
    {
        return RendererFailure("renderer.proxy_not_found", "render proxy was not found for material dirty marking");
    }
    record->dirty_flags = AddRenderDirtyFlag(record->dirty_flags, RenderProxyDirtyFlags::Material);
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::SetProxyVisibility(RenderProxyId id, RenderProxyVisibility visibility)
{
    ProxyRecord* record = FindProxy(id);
    if (record == nullptr || record->lifecycle != RenderProxyLifecycle::Registered)
    {
        return RendererFailure("renderer.proxy_not_found", "render proxy was not found for visibility update");
    }
    record->visibility = visibility;
    record->dirty_flags = AddRenderDirtyFlag(record->dirty_flags, RenderProxyDirtyFlags::Visibility);
    return foundation::Result<void>::Success();
}

RenderProxyLifecycle RendererRuntime::GetProxyLifecycle(RenderProxyId id) const
{
    const ProxyRecord* record = FindProxy(id);
    return record == nullptr ? RenderProxyLifecycle::Unregistered : record->lifecycle;
}

RenderProxyReadiness RendererRuntime::GetProxyReadiness(RenderProxyId id) const
{
    const ProxyRecord* record = FindProxy(id);
    return record == nullptr ? RenderProxyReadiness::MissingResources : record->readiness;
}

RenderProxyVisibility RendererRuntime::GetProxyVisibility(RenderProxyId id) const
{
    const ProxyRecord* record = FindProxy(id);
    return record == nullptr ? RenderProxyVisibility::Hidden : record->visibility;
}

RenderProxyDirtyMask RendererRuntime::GetProxyDirtyFlags(RenderProxyId id) const
{
    const ProxyRecord* record = FindProxy(id);
    return record == nullptr ? 0u : record->dirty_flags;
}

foundation::Result<ViewId> RendererRuntime::CreateView(const ViewDesc& desc)
{
    const auto transform = GetTransform(desc.transform_node);
    if (!transform)
    {
        return foundation::Result<ViewId>::Failure(transform.GetError());
    }
    if (desc.vertical_fov <= 0.0f || desc.near_plane <= 0.0f || desc.far_plane <= desc.near_plane)
    {
        return RendererFailureValue<ViewId>("renderer.invalid_view", "view descriptor contains invalid clip or field-of-view values");
    }

    const ViewId view_id{next_view_value_++};
    views_.emplace(view_id, ViewRecord{desc, ViewLifecycle::Active});
    return foundation::Result<ViewId>::Success(view_id);
}

foundation::Result<void> RendererRuntime::DestroyView(ViewId view)
{
    ViewRecord* record = FindView(view);
    if (record == nullptr)
    {
        return RendererFailure("renderer.view_not_found", "view was not found for destruction");
    }
    record->lifecycle = ViewLifecycle::DestroyPending;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::SetMainView(ViewId view)
{
    ViewRecord* record = FindView(view);
    if (!view.IsValid() || record == nullptr || record->lifecycle != ViewLifecycle::Active)
    {
        return RendererFailure("renderer.view_not_found", "active view was not found for main view selection");
    }
    main_view_ = view;
    return foundation::Result<void>::Success();
}

ViewId RendererRuntime::GetMainView() const
{
    return main_view_;
}

ViewLifecycle RendererRuntime::GetViewLifecycle(ViewId view) const
{
    const ViewRecord* record = FindView(view);
    return record == nullptr ? ViewLifecycle::Destroyed : record->lifecycle;
}

foundation::Result<void> RendererRuntime::PrepareFrame()
{
    frame_state_ = RenderFrameState::Preparing;
    if (main_view_.IsValid())
    {
        const ViewRecord* main_view = FindView(main_view_);
        if (main_view == nullptr || main_view->lifecycle != ViewLifecycle::Active)
        {
            frame_state_ = RenderFrameState::Failed;
            return RendererFailure("renderer.invalid_main_view", "main view is missing or not active");
        }
        const auto transform = GetTransform(main_view->desc.transform_node);
        if (!transform)
        {
            frame_state_ = RenderFrameState::Failed;
            return foundation::Result<void>::Failure(transform.GetError());
        }
    }

    for (auto& [id, proxy] : proxies_)
    {
        (void)id;
        if (proxy.lifecycle == RenderProxyLifecycle::Registered)
        {
            const auto readiness = RefreshProxyReadiness(proxy);
            if (!readiness)
            {
                frame_state_ = RenderFrameState::Failed;
                return readiness;
            }
            proxy.dirty_flags = 0;
        }
    }
    frame_state_ = RenderFrameState::ReadyToRender;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::RenderFrame()
{
    if (frame_state_ != RenderFrameState::ReadyToRender)
    {
        frame_state_ = RenderFrameState::Failed;
        return RendererFailure("renderer.frame_not_prepared", "render frame requires PrepareFrame to complete successfully first");
    }
    frame_state_ = RenderFrameState::Rendering;
    frame_state_ = RenderFrameState::Submitted;
    return foundation::Result<void>::Success();
}

RenderFrameState RendererRuntime::GetFrameState() const
{
    return frame_state_;
}

foundation::Result<RenderTransformSnapshot> RendererRuntime::GetTransform(RenderTransformId node) const
{
    if (!node.IsValid())
    {
        return RendererFailureValue<RenderTransformSnapshot>("renderer.invalid_transform", "scene transform node must be valid");
    }
    if (scene_source_ == nullptr)
    {
        return RendererFailureValue<RenderTransformSnapshot>("renderer.scene_source_missing", "renderer requires a scene source");
    }
    return scene_source_->GetTransformSnapshot(node);
}

foundation::Result<void> RendererRuntime::AcquireProxyResources(ProxyRecord& record)
{
    if (resource_bridge_ == nullptr)
    {
        return RendererFailure("renderer.resource_bridge_missing", "renderer requires a resource bridge");
    }
    if (record.resources_acquired)
    {
        return foundation::Result<void>::Success();
    }
    const auto acquired = resource_bridge_->AcquirePayloads(record.desc.mesh, record.desc.material);
    if (!acquired)
    {
        return foundation::Result<void>::Failure(acquired.GetError());
    }
    record.resources_acquired = true;
    return foundation::Result<void>::Success();
}

void RendererRuntime::ReleaseProxyResources(ProxyRecord& record)
{
    if (resource_bridge_ == nullptr || !record.resources_acquired)
    {
        return;
    }
    resource_bridge_->ReleasePayloads(record.desc.mesh, record.desc.material);
    record.resources_acquired = false;
    record.payloads = {};
}

foundation::Result<void> RendererRuntime::RefreshProxyReadiness(ProxyRecord& record)
{
    if (resource_bridge_ == nullptr)
    {
        return RendererFailure("renderer.resource_bridge_missing", "renderer requires a resource bridge");
    }
    const auto payloads = resource_bridge_->GetPayloads(record.desc.mesh, record.desc.material);
    if (!payloads)
    {
        record.readiness = RenderProxyReadiness::MissingResources;
        return foundation::Result<void>::Failure(payloads.GetError());
    }
    record.payloads = payloads.Value();
    record.readiness = record.payloads.mesh && record.payloads.material ? RenderProxyReadiness::Ready : RenderProxyReadiness::MissingResources;
    return foundation::Result<void>::Success();
}

RendererRuntime::ProxyRecord* RendererRuntime::FindProxy(RenderProxyId id)
{
    const auto iterator = proxies_.find(id);
    return iterator == proxies_.end() ? nullptr : &iterator->second;
}

const RendererRuntime::ProxyRecord* RendererRuntime::FindProxy(RenderProxyId id) const
{
    const auto iterator = proxies_.find(id);
    return iterator == proxies_.end() ? nullptr : &iterator->second;
}

RendererRuntime::ViewRecord* RendererRuntime::FindView(ViewId id)
{
    const auto iterator = views_.find(id);
    return iterator == views_.end() ? nullptr : &iterator->second;
}

const RendererRuntime::ViewRecord* RendererRuntime::FindView(ViewId id) const
{
    const auto iterator = views_.find(id);
    return iterator == views_.end() ? nullptr : &iterator->second;
}
} // namespace epidemic::runtime::renderer

