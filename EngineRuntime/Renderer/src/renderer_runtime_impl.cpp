#include "renderer_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/Foundation/numeric_validation.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
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

[[nodiscard]] RenderProxyReadiness ReadinessForResourceFailure(const foundation::Error& error) noexcept
{
    return IsTemporaryResourceFailure(error) ? RenderProxyReadiness::Loading : RenderProxyReadiness::Failed;
}

[[nodiscard]] bool IsValidPose(const RenderPoseBuffer& pose, RuntimeObjectId expected_owner) noexcept
{
    if (!expected_owner.IsValid() || pose.owner != expected_owner)
    {
        return false;
    }
    return std::all_of(pose.bone_transforms.begin(), pose.bone_transforms.end(), [](const Transform& transform) {
        return IsValidTransform(transform);
    });
}

[[nodiscard]] constexpr bool IsValidRenderLayer(RenderLayer layer) noexcept
{
    switch (layer)
    {
    case RenderLayer::Opaque:
    case RenderLayer::Transparent:
    case RenderLayer::Sky:
    case RenderLayer::Water:
    case RenderLayer::Debug:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidProxyVisibility(RenderProxyVisibility visibility) noexcept
{
    switch (visibility)
    {
    case RenderProxyVisibility::Hidden:
    case RenderProxyVisibility::Visible:
    case RenderProxyVisibility::Culled:
        return true;
    }
    return false;
}

[[nodiscard]] foundation::Result<std::uint64_t> PeekRendererId(std::uint64_t next_value,
                                                                std::string_view code,
                                                                std::string_view message)
{
    if (next_value == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(foundation::Error::Create(code, message));
    }
    return foundation::Result<std::uint64_t>::Success(next_value);
}

void CommitRendererId(std::uint64_t& next_value) noexcept
{
    next_value = next_value == std::numeric_limits<std::uint64_t>::max() ? 0 : next_value + 1;
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
    try
    {
        (void)Shutdown();
    }
    catch (...)
    {
        // Destruction is best-effort and must never propagate backend exceptions.
    }
}

foundation::Result<RenderProxyId> RendererRuntime::RegisterProxy(const RenderProxyDesc& desc)
{
    const auto running = EnsureRunning();
    if (!running)
    {
        return foundation::Result<RenderProxyId>::Failure(running.GetError());
    }
    if (!desc.owner.IsValid())
    {
        return RendererFailureValue<RenderProxyId>("renderer.invalid_owner", "render proxy owner must be valid before registration");
    }
    if (!desc.mesh.IsValid() || !desc.material.IsValid())
    {
        return RendererFailureValue<RenderProxyId>("renderer.invalid_resource", "render proxy must reference valid mesh and material ids");
    }
    if (!IsValidRenderLayer(desc.layer) || !IsValidProxyVisibility(desc.visibility))
    {
        return RendererFailureValue<RenderProxyId>("renderer.invalid_enum", "render proxy contains an out-of-domain enum value");
    }
    const auto transform = GetTransform(desc.transform_node);
    if (!transform)
    {
        return foundation::Result<RenderProxyId>::Failure(transform.GetError());
    }

    const auto proxy_value = PeekRendererId(next_proxy_value_, "renderer.proxy_id_exhausted", "render proxy id allocator is exhausted");
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

    ProxyRecord* published = nullptr;
    try
    {
        if (fail_next_proxy_publication_for_testing_)
        {
            fail_next_proxy_publication_for_testing_ = false;
            throw std::bad_alloc{};
        }
        const auto [iterator, inserted] = proxies_.emplace(proxy_id, std::move(record));
        if (!inserted)
        {
            return RendererFailureValue<RenderProxyId>("renderer.duplicate_proxy_id", "allocated render proxy id already exists");
        }
        published = &iterator->second;
    }
    catch (...)
    {
        return RendererFailureValue<RenderProxyId>("renderer.allocation_failed", "failed to publish render proxy");
    }

    CommitRendererId(next_proxy_value_);
    const auto acquired = AcquireProxyResources(*published);
    if (acquired)
    {
        (void)RefreshProxyReadiness(*published);
    }
    else
    {
        published->readiness = ReadinessForResourceFailure(acquired.GetError());
    }
    return foundation::Result<RenderProxyId>::Success(proxy_id);
}

foundation::Result<void> RendererRuntime::DestroyProxy(RenderProxyId id)
{
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

    std::vector<RenderProxyId> proxies_to_remove;
    std::vector<ViewId> views_to_remove;
    try
    {
        proxies_to_remove.reserve(proxies_.size());
        views_to_remove.reserve(views_.size());
        for (const auto& [id, record] : proxies_)
        {
            if (record.lifecycle == RenderProxyLifecycle::DestroyPending)
            {
                proxies_to_remove.push_back(id);
            }
        }
        for (const auto& [id, record] : views_)
        {
            if (record.lifecycle == ViewLifecycle::DestroyPending)
            {
                views_to_remove.push_back(id);
            }
        }
        std::sort(proxies_to_remove.begin(), proxies_to_remove.end(), [](RenderProxyId left, RenderProxyId right) {
            return left.value < right.value;
        });
        std::sort(views_to_remove.begin(), views_to_remove.end(), [](ViewId left, ViewId right) {
            return left.value < right.value;
        });
    }
    catch (...)
    {
        return RendererFailure("renderer.allocation_failed", "failed to stage deferred destroy drain");
    }

    // Retryable prefix progress: successfully released proxies are erased; a failed proxy stays DestroyPending.
    for (RenderProxyId id : proxies_to_remove)
    {
        const auto released = ReleaseProxyResources(proxies_.at(id));
        if (!released)
        {
            return released;
        }
        proxies_.erase(id);
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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

    if (!IsValidProxyVisibility(visibility))
    {
        return RendererFailure("renderer.invalid_enum", "render proxy visibility enum is outside the supported domain");
    }
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
    const auto running = EnsureRunning();
    if (!running)
    {
        return foundation::Result<ViewId>::Failure(running.GetError());
    }

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

    const auto view_value = PeekRendererId(next_view_value_, "renderer.view_id_exhausted", "renderer view id allocator is exhausted");
    if (!view_value)
    {
        return foundation::Result<ViewId>::Failure(view_value.GetError());
    }
    const ViewId view_id{view_value.Value()};
    try
    {
        const auto [_, inserted] = views_.emplace(view_id, ViewRecord{desc, ViewLifecycle::Active});
        if (!inserted)
        {
            return RendererFailureValue<ViewId>("renderer.duplicate_view_id", "allocated renderer view id already exists");
        }
    }
    catch (...)
    {
        return RendererFailureValue<ViewId>("renderer.allocation_failed", "failed to publish renderer view");
    }
    CommitRendererId(next_view_value_);
    return foundation::Result<ViewId>::Success(view_id);
}

foundation::Result<void> RendererRuntime::DestroyView(ViewId view)
{
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

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
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

    if (frame_recovery_pending_)
    {
        const auto recovered = RecoverPendingFrame();
        if (!recovered)
        {
            frame_state_ = RenderFrameState::Failed;
            return recovered;
        }
    }

    std::optional<RenderFrameContext> next_context;
    std::vector<RenderProxyId> ordered_proxies;
    std::vector<PreparedProxyUpdate> next_updates;
    std::vector<RenderProxySubmission> next_submissions;
    try
    {
        ordered_proxies.reserve(proxies_.size());
        next_updates.reserve(proxies_.size());
        next_submissions.reserve(proxies_.size());
        for (const auto& [id, proxy] : proxies_)
        {
            if (proxy.lifecycle == RenderProxyLifecycle::Registered)
            {
                ordered_proxies.push_back(id);
            }
        }
        std::sort(ordered_proxies.begin(), ordered_proxies.end(), [](RenderProxyId left, RenderProxyId right) {
            return left.value < right.value;
        });
    }
    catch (...)
    {
        return RendererFailure("renderer.allocation_failed", "failed to allocate frame preparation staging");
    }

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
        next_context = RenderFrameContext{main_view_, main_view->desc, transform.Value()};
    }

    // Stage every fallible scene observation before publishing any prepared-frame state.
    for (RenderProxyId id : ordered_proxies)
    {
        const ProxyRecord& proxy = proxies_.at(id);
        PreparedProxyUpdate update{};
        update.id = id;
        update.transform = proxy.cached_transform;
        if (HasRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Transform))
        {
            const auto transform = GetTransform(proxy.desc.transform_node);
            if (!transform)
            {
                frame_state_ = RenderFrameState::Failed;
                return foundation::Result<void>::Failure(transform.GetError());
            }
            if (transform.Value().revision < proxy.cached_transform.revision)
            {
                frame_state_ = RenderFrameState::Failed;
                return RendererFailure("renderer.stale_transform", "scene source returned an older transform revision");
            }
            update.transform = transform.Value();
            update.clear_dirty_flags = AddRenderDirtyFlag(update.clear_dirty_flags, RenderProxyDirtyFlags::Transform);
        }
        if (HasRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Visibility))
        {
            update.clear_dirty_flags = AddRenderDirtyFlag(update.clear_dirty_flags, RenderProxyDirtyFlags::Visibility);
        }
        next_updates.push_back(std::move(update));
    }

    // Resource ownership is durable proxy state. A failed acquire/readiness refresh may update
    // readiness, but it never publishes a Ready proxy unless the bridge confirmed usable payloads.
    for (RenderProxyId id : ordered_proxies)
    {
        ProxyRecord& proxy = proxies_.at(id);
        if (!proxy.resources_acquired)
        {
            const auto acquired = AcquireProxyResources(proxy);
            if (!acquired)
            {
                proxy.readiness = ReadinessForResourceFailure(acquired.GetError());
                continue;
            }
        }
        const auto readiness = RefreshProxyReadiness(proxy);
        if (!readiness || proxy.readiness != RenderProxyReadiness::Ready)
        {
            continue;
        }
        if (HasRenderDirtyFlag(proxy.dirty_flags, RenderProxyDirtyFlags::Material))
        {
            const auto update = std::lower_bound(next_updates.begin(), next_updates.end(), id, [](const PreparedProxyUpdate& value, RenderProxyId key) {
                return value.id.value < key.value;
            });
            if (update != next_updates.end() && update->id == id)
            {
                update->clear_dirty_flags = AddRenderDirtyFlag(update->clear_dirty_flags, RenderProxyDirtyFlags::Material);
            }
        }
    }

    // Build the exact backend submission set and resolve all fallible pose reads before BeginFrame.
    for (const PreparedProxyUpdate& update : next_updates)
    {
        const ProxyRecord& proxy = proxies_.at(update.id);
        if (proxy.readiness != RenderProxyReadiness::Ready || proxy.visibility != RenderProxyVisibility::Visible)
        {
            continue;
        }

        std::shared_ptr<const RenderPoseBuffer> pose;
        if (pose_source_ != nullptr)
        {
            try
            {
                const auto resolved_pose = pose_source_->GetPose(proxy.desc.owner);
                if (!resolved_pose)
                {
                    frame_state_ = RenderFrameState::Failed;
                    return foundation::Result<void>::Failure(resolved_pose.GetError());
                }
                pose = resolved_pose.Value();
            }
            catch (...)
            {
                frame_state_ = RenderFrameState::Failed;
                return RendererFailure("renderer.backend_exception", "render pose source threw during frame preparation");
            }
            if (pose && !IsValidPose(*pose, proxy.desc.owner))
            {
                frame_state_ = RenderFrameState::Failed;
                return RendererFailure("renderer.invalid_pose", "render pose source returned an invalid pose buffer");
            }
        }

        next_submissions.push_back(
            RenderProxySubmission{update.id, update.transform, proxy.payloads, std::move(pose), proxy.desc.layer, proxy.visibility});
    }

    std::sort(next_submissions.begin(), next_submissions.end(), [](const RenderProxySubmission& left, const RenderProxySubmission& right) {
        return std::tuple{LayerRank(left.layer), left.proxy.value} < std::tuple{LayerRank(right.layer), right.proxy.value};
    });

    // Publication is no-fail: all allocations and external reads are complete before these swaps.
    prepared_context_ = std::move(next_context);
    prepared_updates_.swap(next_updates);
    prepared_submissions_.swap(next_submissions);
    frame_state_ = RenderFrameState::ReadyToRender;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::RenderFrame()
{
    const auto running = EnsureRunning();
    if (!running)
    {
        return running;
    }

    if (frame_recovery_pending_)
    {
        const auto recovered = RecoverPendingFrame();
        if (!recovered)
        {
            frame_state_ = RenderFrameState::Failed;
            return recovered;
        }
    }
    if (frame_state_ != RenderFrameState::ReadyToRender)
    {
        return RendererFailure("renderer.frame_not_prepared", "render frame requires PrepareFrame to complete successfully first");
    }
    if (command_sink_ == nullptr)
    {
        return RendererFailure("renderer.command_sink_missing", "render frame requires a command sink");
    }
    if (!prepared_context_)
    {
        return RendererFailure("renderer.main_view_missing", "render frame requires a prepared main view");
    }

    // Contract: failed/throwing BeginFrame does not open a backend frame, so no Abort is required.
    try
    {
        const auto begun = command_sink_->BeginFrame(*prepared_context_);
        if (!begun)
        {
            frame_state_ = RenderFrameState::Failed;
            return begun;
        }
    }
    catch (...)
    {
        frame_state_ = RenderFrameState::Failed;
        return RendererFailure("renderer.backend_exception", "render command sink threw while beginning frame");
    }

    frame_state_ = RenderFrameState::Rendering;
    for (const RenderProxySubmission& submission : prepared_submissions_)
    {
        try
        {
            const auto submitted = command_sink_->SubmitProxy(submission);
            if (!submitted)
            {
                AbortFrameAfterFailureNoThrow();
                frame_state_ = RenderFrameState::Failed;
                return submitted;
            }
        }
        catch (...)
        {
            AbortFrameAfterFailureNoThrow();
            frame_state_ = RenderFrameState::Failed;
            return RendererFailure("renderer.backend_exception", "render command sink threw while submitting proxy");
        }
    }

    try
    {
        const auto ended = command_sink_->EndFrame();
        if (!ended)
        {
            AbortFrameAfterFailureNoThrow();
            frame_state_ = RenderFrameState::Failed;
            return ended;
        }
    }
    catch (...)
    {
        AbortFrameAfterFailureNoThrow();
        frame_state_ = RenderFrameState::Failed;
        return RendererFailure("renderer.backend_exception", "render command sink threw while ending frame");
    }

    CommitPreparedFrameNoThrow();
    frame_state_ = RenderFrameState::Submitted;
    ClearPreparedFrameNoThrow();
    return foundation::Result<void>::Success();
}

foundation::Result<void> RendererRuntime::Shutdown()
{
    if (shutdown_)
    {
        return foundation::Result<void>::Success();
    }
    shutdown_started_ = true;

    // An uncertain backend frame owns its submitted resources until AbortFrame confirms closure.
    // Do not release proxy resources underneath that frame.
    if (frame_recovery_pending_)
    {
        const auto recovered = RecoverPendingFrame();
        if (!recovered)
        {
            return recovered;
        }
    }

    std::vector<RenderProxyId> release_order;
    try
    {
        release_order.reserve(proxies_.size());
        for (const auto& [id, record] : proxies_)
        {
            if (record.resources_acquired)
            {
                release_order.push_back(id);
            }
        }
        std::sort(release_order.begin(), release_order.end(), [](RenderProxyId left, RenderProxyId right) {
            return left.value < right.value;
        });
    }
    catch (...)
    {
        return RendererFailure("renderer.allocation_failed", "failed to stage deterministic renderer shutdown cleanup");
    }

    std::optional<foundation::Error> first_error;
    for (RenderProxyId id : release_order)
    {
        const auto released = ReleaseProxyResources(proxies_.at(id));
        if (!released && !first_error)
        {
            first_error = released.GetError();
        }
    }
    if (first_error)
    {
        return foundation::Result<void>::Failure(*first_error);
    }

    shutdown_ = true;
    ClearPreparedFrameNoThrow();
    frame_state_ = RenderFrameState::NotPrepared;
    return foundation::Result<void>::Success();
}

RenderFrameState RendererRuntime::GetFrameState() const
{
    return frame_state_;
}

foundation::Result<void> RendererRuntime::EnsureRunning() const
{
    if (shutdown_started_)
    {
        return RendererFailure(shutdown_ ? "renderer.shutdown_complete" : "renderer.shutdown_in_progress",
                               shutdown_ ? "renderer has completed shutdown and no longer accepts mutations"
                                         : "renderer shutdown has started and no longer accepts mutations");
    }
    return foundation::Result<void>::Success();
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
    try
    {
        const auto snapshot = scene_source_->GetTransformSnapshot(node);
        if (!snapshot)
        {
            return foundation::Result<RenderTransformSnapshot>::Failure(snapshot.GetError());
        }
        if (snapshot.Value().node != node || !IsValidTransform(snapshot.Value().world_transform))
        {
            return RendererFailureValue<RenderTransformSnapshot>("renderer.invalid_transform", "scene source returned an invalid transform snapshot");
        }
        return snapshot;
    }
    catch (...)
    {
        return RendererFailureValue<RenderTransformSnapshot>("renderer.backend_exception", "scene source threw while resolving transform");
    }
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
    try
    {
        const auto acquired = resource_bridge_->AcquirePayloads(record.desc.mesh, record.desc.material);
        if (!acquired)
        {
            return foundation::Result<void>::Failure(acquired.GetError());
        }
    }
    catch (...)
    {
        return RendererFailure("renderer.backend_exception", "resource bridge threw while acquiring payloads");
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
    try
    {
        const auto released = resource_bridge_->ReleasePayloads(record.desc.mesh, record.desc.material);
        if (!released)
        {
            return foundation::Result<void>::Failure(released.GetError());
        }
    }
    catch (...)
    {
        return RendererFailure("renderer.backend_exception", "resource bridge threw while releasing payloads");
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
    try
    {
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
    catch (...)
    {
        record.readiness = RenderProxyReadiness::Failed;
        return RendererFailure("renderer.backend_exception", "resource bridge threw while resolving payloads");
    }
}

foundation::Result<void> RendererRuntime::RecoverPendingFrame()
{
    if (!frame_recovery_pending_)
    {
        return foundation::Result<void>::Success();
    }
    if (command_sink_ == nullptr)
    {
        return RendererFailure("renderer.command_sink_missing", "frame recovery requires a command sink");
    }

    try
    {
        const auto aborted = command_sink_->AbortFrame();
        if (!aborted)
        {
            return aborted;
        }
    }
    catch (...)
    {
        return RendererFailure("renderer.backend_exception", "render command sink threw while recovering an open frame");
    }

    frame_recovery_pending_ = false;
    frame_state_ = prepared_context_ ? RenderFrameState::ReadyToRender : RenderFrameState::NotPrepared;
    return foundation::Result<void>::Success();
}

void RendererRuntime::AbortFrameAfterFailureNoThrow() noexcept
{
    // Publish recovery ownership before the external call. A failed/throwing Abort leaves
    // the marker set so no future BeginFrame can run until reconciliation succeeds.
    frame_recovery_pending_ = true;
    if (command_sink_ == nullptr)
    {
        return;
    }
    try
    {
        const auto aborted = command_sink_->AbortFrame();
        if (aborted)
        {
            frame_recovery_pending_ = false;
        }
    }
    catch (...)
    {
    }
}

void RendererRuntime::CommitPreparedFrameNoThrow() noexcept
{
    for (const PreparedProxyUpdate& update : prepared_updates_)
    {
        ProxyRecord* proxy = FindProxy(update.id);
        if (proxy == nullptr)
        {
            continue;
        }
        if (HasRenderDirtyFlag(update.clear_dirty_flags, RenderProxyDirtyFlags::Transform))
        {
            proxy->cached_transform = update.transform;
        }
        proxy->dirty_flags &= ~update.clear_dirty_flags;
    }
}

void RendererRuntime::ClearPreparedFrameNoThrow() noexcept
{
    prepared_context_.reset();
    prepared_updates_.clear();
    prepared_submissions_.clear();
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

