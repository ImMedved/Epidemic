#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/Foundation/numeric_validation.h"

#include <algorithm>
#include <optional>
#include <tuple>
#include <vector>
#include <string_view>

namespace epidemic::runtime::renderer
{
namespace
{
[[nodiscard]] int LayerRank(RenderLayer layer) noexcept
{
    switch (layer)
    {
    case RenderLayer::Sky: return 0;
    case RenderLayer::Opaque: return 1;
    case RenderLayer::Water: return 2;
    case RenderLayer::Transparent: return 3;
    case RenderLayer::Debug: return 4;
    }
    return 5;
}

[[nodiscard]] foundation::Result<void> RendererFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> RendererFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool IsTemporaryResourceFailure(const foundation::Error& error) noexcept
{
    return error.HasCode("renderer.resource_not_ready") || error.HasCode("resource.not_ready") ||
           error.HasCode("resource.loading") || error.HasCode("resource.queued");
}
} // namespace

RendererRuntime::RendererRuntime(IRenderResourceBridge* resource_bridge, IRenderSceneSource* scene_source)
    : resource_bridge_(resource_bridge), scene_source_(scene_source)
{
}

RendererRuntime::RendererRuntime(IRenderResourceBridge* resource_bridge, IRenderSceneSource* scene_source, IRenderCommandSink* command_sink)
    : resource_bridge_(resource_bridge), scene_source_(scene_source), command_sink_(command_sink)
{
}

RendererRuntime::RendererRuntime(IRenderResourceBridge* resource_bridge,
                                 IRenderSceneSource* scene_source,
                                 IRenderPoseSource* pose_source,
                                 IRenderCommandSink* command_sink)
    : resource_bridge_(resource_bridge), scene_source_(scene_source), pose_source_(pose_source), command_sink_(command_sink)
{
}

RendererRuntime::~RendererRuntime()
{
    (void)Shutdown();
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

    const auto proxy_value = AllocateMonotonicId(next_proxy_value_, "renderer.proxy_id_exhausted", "render proxy id allocator is exhausted");
    if (!proxy_value)
    {
        return foundation::Result<RenderProxyId>::Failure(proxy_value.GetError());
    }
    const RenderProxyId proxy_id{proxy_value.Value()};
    if (proxies_.contains(proxy_id))
    {
        return RendererFailureValue<RenderProxyId>("renderer.duplicate_proxy_id", "allocated render proxy id already exists");
    }
    ProxyRecord record{};
    record.desc = desc;
    record.lifecycle = RenderProxyLifecycle::Registered;
    record.visibility = desc.visibility;
    record.dirty_flags = ToRenderDirtyMask(RenderProxyDirtyFlags::Transform) | ToRenderDirtyMask(RenderProxyDirtyFlags::Material);
    record.cached_transform = transform.Value();
    const auto acquired = AcquireProxyResources(record);
    if (acquired)
    {
        (void)RefreshProxyReadiness(record);
    }
    else
    {
        record.readiness = RenderProxyReadiness::Loading;
    }
    const auto [_, inserted] = proxies_.emplace(proxy_id, std::move(record));
    if (!inserted)
    {
        return RendererFailureValue<RenderProxyId>("renderer.duplicate_proxy_id", "allocated render proxy id already exists");
    }
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
        const auto released = ReleaseProxyResources(proxies_.at(id));
        if (!released)
        {
            return released;
        }
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
    constexpr float kMaxPerspectiveFovDegrees = 180.0f;
    if (!IsFinitePositive(desc.vertical_fov) || desc.vertical_fov >= kMaxPerspectiveFovDegrees ||
        !IsFinitePositive(desc.near_plane) || !IsFinitePositive(desc.far_plane) || desc.far_plane <= desc.near_plane)
    {
        return RendererFailureValue<ViewId>("renderer.invalid_view", "view descriptor contains invalid clip or field-of-view values");
    }

    const auto transform = GetTransform(desc.transform_node);
    if (!transform)
    {
        return foundation::Result<ViewId>::Failure(transform.GetError());
    }

    const auto view_value = AllocateMonotonicId(next_view_value_, "renderer.view_id_exhausted", "renderer view id allocator is exhausted");
    if (!view_value)
    {
        return foundation::Result<ViewId>::Failure(view_value.GetError());
    }
    const ViewId view_id{view_value.Value()};
    const auto [_, inserted] = views_.emplace(view_id, ViewRecord{desc, ViewLifecycle::Active});
    if (!inserted)
    {
        return RendererFailureValue<ViewId>("renderer.duplicate_view_id", "allocated renderer view id already exists");
    }
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
    prepared_context_.reset();
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
        prepared_context_ = RenderFrameContext{main_view_, main_view->desc, transform.Value()};
    }

    for (auto& [id, proxy] : proxies_)
    {
        (void)id;
        if (proxy.lifecycle == RenderProxyLifecycle::Registered)
        {
            if (HasRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Transform))
            {
                const auto transform = RefreshProxyTransform(proxy);
                if (!transform)
                {
                    frame_state_ = RenderFrameState::Failed;
                    return transform;
                }
                proxy.dirty_flags = ClearRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Transform);
            }
            if (!proxy.resources_acquired)
            {
                const auto acquired = AcquireProxyResources(proxy);
                if (!acquired)
                {
                    proxy.readiness = RenderProxyReadiness::Loading;
                    continue;
                }
            }
            const auto readiness = RefreshProxyReadiness(proxy);
            if (!readiness)
            {
                continue;
            }
            if (proxy.readiness != RenderProxyReadiness::Ready)
            {
                continue;
            }
            proxy.dirty_flags = ClearRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Material);
            proxy.dirty_flags = ClearRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Visibility);
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
    if (command_sink_ == nullptr)
    {
        frame_state_ = RenderFrameState::Failed;
        return RendererFailure("renderer.command_sink_missing", "render frame requires a command sink");
    }
    if (command_sink_ != nullptr)
    {
        if (!prepared_context_)
        {
            frame_state_ = RenderFrameState::Failed;
            return RendererFailure("renderer.main_view_missing", "render frame requires a prepared main view");
        }
        const auto begun = command_sink_->BeginFrame(*prepared_context_);
        if (!begun)
        {
            frame_state_ = RenderFrameState::Failed;
            return begun;
        }
        std::vector<RenderProxyId> submissions;
        submissions.reserve(proxies_.size());
        for (const auto& [id, proxy] : proxies_)
        {
            if (proxy.lifecycle != RenderProxyLifecycle::Registered || proxy.readiness != RenderProxyReadiness::Ready ||
                proxy.visibility != RenderProxyVisibility::Visible)
            {
                continue;
            }
            submissions.push_back(id);
        }
        std::sort(submissions.begin(), submissions.end(), [this](RenderProxyId left, RenderProxyId right) {
            const ProxyRecord* left_proxy = FindProxy(left);
            const ProxyRecord* right_proxy = FindProxy(right);
            const int left_layer = left_proxy == nullptr ? 5 : LayerRank(left_proxy->desc.layer);
            const int right_layer = right_proxy == nullptr ? 5 : LayerRank(right_proxy->desc.layer);
            return std::tie(left_layer, left.value) < std::tie(right_layer, right.value);
        });
        for (RenderProxyId id : submissions)
        {
            const ProxyRecord& proxy = proxies_.at(id);
            std::shared_ptr<const RenderPoseBuffer> pose;
            if (pose_source_ != nullptr)
            {
                const auto resolved_pose = pose_source_->GetPose(proxy.desc.owner);
                if (!resolved_pose)
                {
                    (void)command_sink_->AbortFrame();
                    frame_state_ = RenderFrameState::Failed;
                    return foundation::Result<void>::Failure(resolved_pose.GetError());
                }
                pose = resolved_pose.Value();
            }
            const auto submitted = command_sink_->SubmitProxy(
                RenderProxySubmission{id, proxy.cached_transform, proxy.payloads, std::move(pose), proxy.desc.layer, proxy.visibility});
            if (!submitted)
            {
                (void)command_sink_->AbortFrame();
                frame_state_ = RenderFrameState::Failed;
                return submitted;
            }
        }
        const auto ended = command_sink_->EndFrame();
        if (!ended)
        {
            (void)command_sink_->AbortFrame();
            frame_state_ = RenderFrameState::Failed;
            return ended;
        }
    }
    frame_state_ = RenderFrameState::Submitted;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::Shutdown()
{
    if (shutdown_)
    {
        return foundation::Result<void>::Success();
    }
    std::optional<foundation::Error> first_error;
    for (auto& [id, record] : proxies_)
    {
        (void)id;
        const auto released = ReleaseProxyResources(record);
        if (!released && !first_error)
        {
            first_error = released.GetError();
        }
    }
    shutdown_ = true;
    if (first_error)
    {
        return foundation::Result<void>::Failure(*first_error);
    }
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

foundation::Result<void> RendererRuntime::ReleaseProxyResources(ProxyRecord& record)
{
    if (resource_bridge_ == nullptr || !record.resources_acquired)
    {
        return foundation::Result<void>::Success();
    }
    const auto released = resource_bridge_->ReleasePayloads(record.desc.mesh, record.desc.material);
    if (!released)
    {
        return released;
    }
    record.resources_acquired = false;
    record.payloads = {};
    record.readiness = RenderProxyReadiness::MissingResources;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::RefreshProxyTransform(ProxyRecord& record)
{
    const auto transform = GetTransform(record.desc.transform_node);
    if (!transform)
    {
        return foundation::Result<void>::Failure(transform.GetError());
    }
    if (transform.Value().revision < record.cached_transform.revision)
    {
        return RendererFailure("renderer.stale_transform", "scene source returned an older transform revision");
    }
    record.cached_transform = transform.Value();
    return foundation::Result<void>::Success();
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
        record.readiness = IsTemporaryResourceFailure(payloads.GetError()) ? RenderProxyReadiness::Loading : RenderProxyReadiness::Failed;
        return foundation::Result<void>::Failure(payloads.GetError());
    }
    record.payloads = payloads.Value();
    record.readiness = record.payloads.mesh && record.payloads.material ? RenderProxyReadiness::Ready : RenderProxyReadiness::Loading;
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

