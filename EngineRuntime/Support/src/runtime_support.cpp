#include "Epidemic/Runtime/Support/runtime_support.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Error SupportError(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> Failure(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(SupportError(code, message));
}

[[nodiscard]] foundation::Result<void> FailureVoid(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(SupportError(code, message));
}

struct RuntimeSupportTestFaults
{
    bool fail_next_render_lease_publication = false;
    std::optional<std::size_t> fail_streaming_lease_publication_after{};
    bool fail_next_streaming_plan_construction = false;
    bool fail_next_persistence_exception = false;
    bool fail_next_streaming_cleanup_world_transition = false;
    bool fail_next_main_view_creation = false;
    bool fail_next_main_view_publication = false;
};

RuntimeSupportTestFaults g_runtime_support_test_faults{};

[[nodiscard]] bool ConsumeTestFault(bool& fault) noexcept
{
    if (!fault)
    {
        return false;
    }
    fault = false;
    return true;
}

[[nodiscard]] bool ConsumeStreamingLeasePublicationFault() noexcept
{
    auto& countdown = g_runtime_support_test_faults.fail_streaming_lease_publication_after;
    if (!countdown)
    {
        return false;
    }
    if (*countdown == 0)
    {
        countdown.reset();
        return true;
    }
    --*countdown;
    return false;
}

template <typename TService>
[[nodiscard]] foundation::Result<void> Require(core::Application& app, std::string_view name)
{
    if (!app.Services().Contains<TService>())
    {
        return FailureVoid("runtime_support.missing_dependency",
                           std::string("required runtime service is not registered: ") + std::string(name));
    }
    return foundation::Result<void>::Success();
}

template <typename TService>
[[nodiscard]] foundation::Result<void> RegisterShared(core::Application& app,
                                                      std::shared_ptr<TService> service,
                                                      std::string_view name)
{
    if (app.Services().Contains<TService>())
    {
        return FailureVoid("runtime_support.duplicate_registration",
                           std::string("runtime service is already registered: ") + std::string(name));
    }
    try
    {
        app.Services().RegisterInstance<TService>(std::move(service));
    }
    catch (const std::exception& exception)
    {
        return FailureVoid("runtime_support.registration_failed", exception.what());
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] ResourceType MeshType()
{
    return ResourceType{foundation::StringId::FromString("mesh")};
}

[[nodiscard]] ResourceType MaterialType()
{
    return ResourceType{foundation::StringId::FromString("material")};
}

[[nodiscard]] ResourceType SkeletonType()
{
    return ResourceType{foundation::StringId::FromString("animation.skeleton")};
}

[[nodiscard]] ResourceType AnimationClipType()
{
    return ResourceType{foundation::StringId::FromString("animation.clip")};
}

[[nodiscard]] ResourceType AudioClipType()
{
    return ResourceType{foundation::StringId::FromString("audio.clip")};
}

class RuntimeRenderMeshResource final : public renderer::IRenderMeshResource
{
  public:
    explicit RuntimeRenderMeshResource(ResourcePayloadPtr payload) : payload_(std::move(payload)) {}

  private:
    ResourcePayloadPtr payload_;
};

class RuntimeRenderMaterialResource final : public renderer::IRenderMaterialResource
{
  public:
    explicit RuntimeRenderMaterialResource(ResourcePayloadPtr payload) : payload_(std::move(payload)) {}

  private:
    ResourcePayloadPtr payload_;
};

class RuntimeRenderResourceBridge final : public renderer::IRenderResourceBridge,
                                          public IRuntimeAdapterLifecycle
{
  public:
    explicit RuntimeRenderResourceBridge(std::shared_ptr<IResourceManager> manager) : manager_(std::move(manager)) {}

    ~RuntimeRenderResourceBridge() override
    {
        // Explicit coordinator-driven Shutdown() is the ownership protocol. Destruction is
        // only an emergency best-effort fallback because a failed manager Release() must
        // remain retryable while the manager is still alive.
        if (!shutdown_complete_)
        {
            (void)Shutdown();
        }
#ifndef NDEBUG
        assert(leases_.empty() && "RuntimeRenderResourceBridge destroyed with unreleased resource leases");
#endif
    }

    [[nodiscard]] foundation::Result<void> AcquirePayloads(ResourceId mesh, ResourceId material) override
    {
        if (shutdown_started_)
        {
            return FailureVoid("runtime_support.adapter_shutdown", "render resource bridge is shutting down");
        }
        const auto mesh_result = AcquireResource(mesh, MeshType());
        if (!mesh_result)
        {
            return mesh_result;
        }
        const auto material_result = AcquireResource(material, MaterialType());
        if (!material_result)
        {
            const auto rollback = ReleaseResource(mesh, MeshType());
            if (!rollback)
            {
                return FailureVoid("runtime_support.render_payload_cleanup_pending",
                                   "material acquisition failed and mesh rollback is still pending");
            }
            return material_result;
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> ReleasePayloads(ResourceId mesh, ResourceId material) override
    {
        std::optional<foundation::Error> first;
        if (const auto released = ReleaseResource(mesh, MeshType()); !released)
        {
            first = released.GetError();
        }
        if (const auto released = ReleaseResource(material, MaterialType()); !released && !first)
        {
            first = released.GetError();
        }
        return first ? foundation::Result<void>::Failure(*first) : foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<renderer::RenderResourcePayloads>
        GetPayloads(ResourceId mesh, ResourceId material) const override
    {
        const auto mesh_payload = GetPayload(mesh, MeshType());
        if (!mesh_payload)
        {
            return foundation::Result<renderer::RenderResourcePayloads>::Failure(mesh_payload.GetError());
        }
        const auto material_payload = GetPayload(material, MaterialType());
        if (!material_payload)
        {
            return foundation::Result<renderer::RenderResourcePayloads>::Failure(material_payload.GetError());
        }
        renderer::RenderResourcePayloads result{};
        result.mesh = std::make_shared<RuntimeRenderMeshResource>(mesh_payload.Value());
        result.material = std::make_shared<RuntimeRenderMaterialResource>(material_payload.Value());
        return foundation::Result<renderer::RenderResourcePayloads>::Success(std::move(result));
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        std::optional<foundation::Error> first;
        for (auto iterator = leases_.begin(); iterator != leases_.end();)
        {
            const auto released = ReleaseManagerLease(iterator->second.lease);
            if (released)
            {
                iterator = leases_.erase(iterator);
            }
            else
            {
                if (!first)
                {
                    first = released.GetError();
                }
                ++iterator;
            }
        }
        shutdown_complete_ = leases_.empty();
        return first ? foundation::Result<void>::Failure(*first) : foundation::Result<void>::Success();
    }

  private:
    struct Key
    {
        ResourceId id{};
        ResourceType type{};
        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };

    struct KeyHash
    {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept
        {
            return std::hash<std::uint64_t>{}(key.id.Raw()) ^
                   (std::hash<std::uint64_t>{}(key.type.value.Raw()) << 1u);
        }
    };

    struct Entry
    {
        ResourceLease lease{};
        std::size_t references = 0;
    };

    [[nodiscard]] foundation::Result<ResourceLease> RequestManagerLease(ResourceRequest request)
    {
        try
        {
            return manager_->RequestLease(request);
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<ResourceLease>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while acquiring a render lease",
                                          exception.what()));
        }
        catch (...)
        {
            return foundation::Result<ResourceLease>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while acquiring a render lease"));
        }
    }

    [[nodiscard]] foundation::Result<void> ReleaseManagerLease(ResourceLease lease)
    {
        try
        {
            return manager_->Release(lease);
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while releasing a render lease",
                                          exception.what()));
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while releasing a render lease"));
        }
    }

    [[nodiscard]] foundation::Result<void> AcquireResource(ResourceId id, ResourceType type)
    {
        if (!id.IsValid() || !type.IsValid())
        {
            return FailureVoid("renderer.invalid_resource", "render resource id and type must be valid");
        }
        const Key key{id, type};
        if (auto iterator = leases_.find(key); iterator != leases_.end())
        {
            if (iterator->second.references == std::numeric_limits<std::size_t>::max())
            {
                return FailureVoid("runtime_support.reference_overflow", "render resource reference count overflow");
            }
            ++iterator->second.references;
            return foundation::Result<void>::Success();
        }

        // Allocate the local ownership slot before touching the external manager. This
        // removes the map-node allocation from the post-acquisition failure window.
        decltype(leases_)::iterator slot{};
        try
        {
            const auto inserted = leases_.try_emplace(key, Entry{});
            slot = inserted.first;
            if (!inserted.second)
            {
                return FailureVoid("runtime_support.render_lease_publication_conflict",
                                   "render resource ownership slot already exists");
            }
        }
        catch (const std::bad_alloc&)
        {
            return FailureVoid("runtime_support.allocation_failure",
                               "could not allocate render resource ownership state");
        }

        const auto lease = RequestManagerLease(ResourceRequest{id, type});
        if (!lease)
        {
            leases_.erase(slot);
            return foundation::Result<void>::Failure(lease.GetError());
        }

        if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_render_lease_publication))
        {
            const auto rollback = ReleaseManagerLease(lease.Value());
            if (rollback)
            {
                leases_.erase(slot);
                return FailureVoid("runtime_support.allocation_failure",
                                   "injected render lease publication failure");
            }
            slot->second = Entry{lease.Value(), 1};
            return FailureVoid("runtime_support.render_payload_cleanup_pending",
                               "injected render lease publication failure left cleanup pending");
        }

        // Publishing into an already allocated slot is non-allocating. Keep the acquired
        // lease in that slot if an unexpected publication exception ever occurs, so the
        // caller can explicitly ReleasePayloads()/Shutdown() instead of losing ownership.
        try
        {
            slot->second = Entry{lease.Value(), 1};
        }
        catch (...)
        {
            const auto rollback = ReleaseManagerLease(lease.Value());
            if (rollback)
            {
                leases_.erase(slot);
                return FailureVoid("runtime_support.render_lease_publication_failed",
                                   "render lease could not be published locally");
            }
            slot->second = Entry{lease.Value(), 1};
            return FailureVoid("runtime_support.render_payload_cleanup_pending",
                               "render lease publication failed and rollback is pending");
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> ReleaseResource(ResourceId id, ResourceType type)
    {
        const auto iterator = leases_.find(Key{id, type});
        if (iterator == leases_.end())
        {
            return foundation::Result<void>::Success();
        }
        if (iterator->second.references > 1)
        {
            --iterator->second.references;
            return foundation::Result<void>::Success();
        }
        const auto released = ReleaseManagerLease(iterator->second.lease);
        if (!released)
        {
            return foundation::Result<void>::Failure(released.GetError());
        }
        leases_.erase(iterator);
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<ResourcePayloadPtr> GetPayload(ResourceId id, ResourceType type) const
    {
        const auto iterator = leases_.find(Key{id, type});
        if (iterator == leases_.end())
        {
            return Failure<ResourcePayloadPtr>("renderer.resource_not_ready", "render resource was not acquired");
        }
        const ResourceState state = manager_->GetState(iterator->second.lease.resource);
        if (state != ResourceState::Ready)
        {
            const bool permanent = state == ResourceState::Failed || state == ResourceState::Evicted ||
                                   state == ResourceState::Unknown;
            return Failure<ResourcePayloadPtr>(permanent ? "renderer.resource_failed" : "renderer.resource_not_ready",
                                               permanent ? "render resource failed" : "render resource is loading");
        }
        ResourcePayloadPtr payload = manager_->GetPayload(iterator->second.lease.resource);
        if (!payload)
        {
            return Failure<ResourcePayloadPtr>("renderer.resource_failed", "ready render resource has no payload");
        }
        return foundation::Result<ResourcePayloadPtr>::Success(std::move(payload));
    }

    std::shared_ptr<IResourceManager> manager_;
    std::unordered_map<Key, Entry, KeyHash> leases_;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};

class RuntimeRenderSceneSource final : public renderer::IRenderSceneSource
{
  public:
    RuntimeRenderSceneSource(std::shared_ptr<ISceneNodeRegistry> nodes,
                             std::shared_ptr<ITransformRegistry> transforms)
        : nodes_(std::move(nodes)), transforms_(std::move(transforms))
    {
    }

    [[nodiscard]] foundation::Result<renderer::RenderTransformSnapshot>
        GetTransformSnapshot(renderer::RenderTransformId node) const override
    {
        const SceneNodeId scene_node{node.Raw()};
        if (!nodes_->Exists(scene_node))
        {
            return Failure<renderer::RenderTransformSnapshot>("renderer.transform_missing", "scene node is not registered");
        }
        const auto transform = transforms_->GetWorldTransform(scene_node);
        if (!transform)
        {
            return Failure<renderer::RenderTransformSnapshot>("renderer.transform_missing", "scene node has no world transform");
        }
        const auto record = nodes_->GetNode(scene_node);
        return foundation::Result<renderer::RenderTransformSnapshot>::Success(
            renderer::RenderTransformSnapshot{node, *transform, record ? record->revision : 0u});
    }

  private:
    std::shared_ptr<ISceneNodeRegistry> nodes_;
    std::shared_ptr<ITransformRegistry> transforms_;
};

class RuntimeSceneTransformAdapter final : public physics::IPhysicsTransformSource,
                                           public physics::IPhysicsTransformSink,
                                           public audio::IAudioTransformSource,
                                           public ISceneProjectionQueue,
                                           public IRuntimeAdapterLifecycle
{
  public:
    explicit RuntimeSceneTransformAdapter(std::shared_ptr<ITransformRegistry> transforms)
        : transforms_(std::move(transforms))
    {
    }

    [[nodiscard]] foundation::Result<Transform> ReadTransform(RuntimeObjectId id) const override
    {
        const auto transform = transforms_->GetWorldTransform(SceneNodeId{id.Raw()});
        if (!transform)
        {
            return Failure<Transform>("runtime_support.transform_missing", "scene transform is unavailable");
        }
        return foundation::Result<Transform>::Success(*transform);
    }

    [[nodiscard]] foundation::Result<void>
        WriteTransform(physics::PhysicsTransformId id, const Transform& transform) override
    {
        if (shutdown_started_)
        {
            return FailureVoid("runtime_support.adapter_shutdown", "scene projection adapter is shutting down");
        }
        if (!id.IsValid())
        {
            return FailureVoid("runtime_support.invalid_transform", "physics transform id must be valid");
        }
        pending_.push_back(PendingSceneProjection{SceneNodeId{id.Raw()}, transform});
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<std::size_t> Flush() override
    {
        std::sort(pending_.begin(), pending_.end(), [](const PendingSceneProjection& left,
                                                       const PendingSceneProjection& right) {
            return left.node.Raw() < right.node.Raw();
        });

        std::vector<PendingSceneProjection> retry;
        retry.reserve(pending_.size());
        std::optional<foundation::Error> first;
        std::size_t applied = 0;
        for (const PendingSceneProjection& projection : pending_)
        {
            const auto result = transforms_->SetWorldTransform(projection.node, projection.world_transform);
            if (result)
            {
                ++applied;
            }
            else
            {
                if (!first)
                {
                    first = result.GetError();
                }
                retry.push_back(projection);
            }
        }
        pending_.swap(retry);
        if (first)
        {
            return foundation::Result<std::size_t>::Failure(*first);
        }
        return foundation::Result<std::size_t>::Success(applied);
    }

    [[nodiscard]] std::size_t PendingCount() const noexcept override { return pending_.size(); }

    [[nodiscard]] foundation::Result<void> DiscardPending() override
    {
        pending_.clear();
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        if (!pending_.empty())
        {
            return FailureVoid("runtime_support.pending_scene_projections",
                               "scene projection queue still contains unapplied writes");
        }
        return foundation::Result<void>::Success();
    }

  private:
    std::shared_ptr<ITransformRegistry> transforms_;
    std::vector<PendingSceneProjection> pending_;
    bool shutdown_started_ = false;
};

class RuntimeSimulationClockAdapter final : public simulation::ISimulationClock
{
  public:
    explicit RuntimeSimulationClockAdapter(std::shared_ptr<IGameClock> clock) : clock_(std::move(clock)) {}
    [[nodiscard]] simulation::SimulationTime Now() const override { return clock_->Now(); }

  private:
    std::shared_ptr<IGameClock> clock_;
};

class RuntimeSimulationCommitTarget final : public simulation::ISimulationCommitTarget,
                                            public IReferenceSimulationCommitLog
{
  public:
    [[nodiscard]] foundation::Result<void> Commit(const simulation::SimulationProposalBatch& batch) override
    {
        for (const simulation::SimulationProposal& proposal : batch.proposals)
        {
            if (proposal.domain != foundation::StringId::FromString("simulation"))
            {
                return FailureVoid("runtime_support.unsupported_simulation_domain",
                                   "reference commit target accepts only neutral simulation proposals");
            }
        }
        committed_.push_back(batch);
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] std::span<const simulation::SimulationProposalBatch>
        CommittedBatches() const noexcept override
    {
        return committed_;
    }

  private:
    std::vector<simulation::SimulationProposalBatch> committed_;
};

class RuntimeAnimationEvaluator final : public animation::IAnimationEvaluatorBackend
{
  public:
    [[nodiscard]] foundation::Result<animation::PoseBuffer>
        EvaluatePose(const animation::AnimationEvaluationRequest& request) const override
    {
        animation::PoseBuffer pose{};
        pose.animator = request.animator;
        pose.owner = request.owner;
        pose.bone_transforms.resize(request.skeleton.joint_count);
        pose.revision = request.revision;
        return foundation::Result<animation::PoseBuffer>::Success(std::move(pose));
    }
};

class RuntimeAnimationPoseBridge final : public animation::IAnimationPoseSink,
                                         public renderer::IRenderPoseSource,
                                         public IRuntimePoseCache,
                                         public IRuntimeAdapterLifecycle
{
  public:
    [[nodiscard]] foundation::Result<void>
        Publish(std::shared_ptr<const animation::PoseBuffer> pose) override
    {
        if (shutdown_started_)
        {
            return FailureVoid("runtime_support.adapter_shutdown", "animation pose bridge is shutting down");
        }
        if (!pose || !pose->animator.IsValid() || !pose->owner.IsValid())
        {
            return FailureVoid("runtime_support.invalid_pose", "published animation pose must have valid animator and owner");
        }
        const auto animator_iterator = poses_by_animator_.find(pose->animator.id.value);
        if (animator_iterator != poses_by_animator_.end())
        {
            const auto& previous = animator_iterator->second;
            if (previous->animator.generation == pose->animator.generation)
            {
                if (previous->revision > pose->revision)
                {
                    return FailureVoid("runtime_support.stale_pose", "animation pose revision moved backwards");
                }
            }
            else
            {
                const auto old_owner = poses_by_owner_.find(previous->owner.Raw());
                if (old_owner != poses_by_owner_.end() && old_owner->second->animator == previous->animator)
                {
                    poses_by_owner_.erase(old_owner);
                }
            }
        }

        const auto owner_iterator = poses_by_owner_.find(pose->owner.Raw());
        if (owner_iterator != poses_by_owner_.end())
        {
            const auto& previous = owner_iterator->second;
            if (previous->animator == pose->animator)
            {
                if (previous->revision > pose->revision)
                {
                    return FailureVoid("runtime_support.stale_pose", "animation pose owner revision moved backwards");
                }
            }
            else
            {
                const auto old_animator = poses_by_animator_.find(previous->animator.id.value);
                if (old_animator != poses_by_animator_.end() && old_animator->second->animator == previous->animator)
                {
                    poses_by_animator_.erase(old_animator);
                }
            }
        }

        poses_by_animator_[pose->animator.id.value] = pose;
        poses_by_owner_[pose->owner.Raw()] = pose;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<std::shared_ptr<const animation::PoseBuffer>>
        GetPose(animation::AnimatorHandle animator) const override
    {
        const auto iterator = poses_by_animator_.find(animator.id.value);
        if (iterator == poses_by_animator_.end() || iterator->second->animator.generation != animator.generation)
        {
            return Failure<std::shared_ptr<const animation::PoseBuffer>>(
                "runtime_support.pose_not_found", "animation pose is not available for this handle");
        }
        return foundation::Result<std::shared_ptr<const animation::PoseBuffer>>::Success(iterator->second);
    }

    [[nodiscard]] foundation::Result<std::shared_ptr<const renderer::RenderPoseBuffer>>
        GetPose(RuntimeObjectId owner) const override
    {
        if (!owner.IsValid())
        {
            return Failure<std::shared_ptr<const renderer::RenderPoseBuffer>>(
                "runtime_support.invalid_pose_owner", "render pose owner must be valid");
        }
        const auto iterator = poses_by_owner_.find(owner.Raw());
        if (iterator == poses_by_owner_.end())
        {
            return foundation::Result<std::shared_ptr<const renderer::RenderPoseBuffer>>::Success({});
        }
        const auto& pose = iterator->second;
        auto rendered = std::make_shared<renderer::RenderPoseBuffer>();
        rendered->owner = pose->owner;
        rendered->bone_transforms = pose->bone_transforms;
        rendered->revision = pose->revision;
        return foundation::Result<std::shared_ptr<const renderer::RenderPoseBuffer>>::Success(std::move(rendered));
    }

    [[nodiscard]] std::size_t PoseCount() const noexcept override { return poses_by_animator_.size(); }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        poses_by_animator_.clear();
        poses_by_owner_.clear();
        return foundation::Result<void>::Success();
    }

  private:
    std::map<std::uint64_t, std::shared_ptr<const animation::PoseBuffer>> poses_by_animator_;
    std::map<std::uint64_t, std::shared_ptr<const animation::PoseBuffer>> poses_by_owner_;
    bool shutdown_started_ = false;
};

class DefaultAnimationResourceMapper final : public IAnimationResourceMapper
{
  public:
    [[nodiscard]] foundation::Result<ResourceRequest>
        ResolveSkeleton(animation::SkeletonId id) const override
    {
        if (!id.IsValid())
        {
            return Failure<ResourceRequest>("animation.invalid_skeleton", "skeleton id must be valid");
        }
        const std::string name = "animation.skeleton." + std::to_string(id.value);
        return foundation::Result<ResourceRequest>::Success(
            ResourceRequest{ResourceId::FromString(name.c_str()), SkeletonType()});
    }

    [[nodiscard]] foundation::Result<ResourceRequest>
        ResolveClip(animation::AnimationClipId id) const override
    {
        if (!id.IsValid())
        {
            return Failure<ResourceRequest>("animation.invalid_clip", "animation clip id must be valid");
        }
        const std::string name = "animation.clip." + std::to_string(id.value);
        return foundation::Result<ResourceRequest>::Success(
            ResourceRequest{ResourceId::FromString(name.c_str()), AnimationClipType()});
    }
};

class RuntimeAnimationResourceSource final : public animation::IAnimationResourceSource,
                                             public IRuntimeAdapterLifecycle
{
  public:
    RuntimeAnimationResourceSource(std::shared_ptr<IResourceManager> manager,
                                   std::shared_ptr<IAnimationResourceMapper> mapper)
        : manager_(std::move(manager)), mapper_(std::move(mapper))
    {
    }

    [[nodiscard]] foundation::Result<animation::SkeletonDesc>
        LoadSkeleton(animation::SkeletonId id) const override
    {
        if (const auto cached = skeleton_cache_.find(id.value); cached != skeleton_cache_.end())
        {
            return foundation::Result<animation::SkeletonDesc>::Success(cached->second);
        }
        const auto request = mapper_->ResolveSkeleton(id);
        if (!request)
        {
            return foundation::Result<animation::SkeletonDesc>::Failure(request.GetError());
        }
        const auto payload = ResolvePayload(request.Value(), skeleton_leases_);
        if (!payload)
        {
            return foundation::Result<animation::SkeletonDesc>::Failure(payload.GetError());
        }
        const auto typed = std::dynamic_pointer_cast<const IAnimationSkeletonResourcePayload>(payload.Value());
        if (!typed)
        {
            TryRelease(request.Value().resource_id, skeleton_leases_);
            return Failure<animation::SkeletonDesc>("animation.resource_payload_unsupported",
                                                    "resource payload does not expose a skeleton descriptor");
        }
        const animation::SkeletonDesc desc = typed->GetSkeleton();
        if (desc.id != id || desc.joint_count == 0)
        {
            TryRelease(request.Value().resource_id, skeleton_leases_);
            return Failure<animation::SkeletonDesc>("animation.invalid_skeleton",
                                                    "resource payload returned an invalid skeleton descriptor");
        }
        skeleton_cache_[id.value] = desc;
        TryRelease(request.Value().resource_id, skeleton_leases_);
        return foundation::Result<animation::SkeletonDesc>::Success(desc);
    }

    [[nodiscard]] foundation::Result<animation::AnimationClipDesc>
        LoadClip(animation::AnimationClipId id) const override
    {
        if (const auto cached = clip_cache_.find(id.value); cached != clip_cache_.end())
        {
            return foundation::Result<animation::AnimationClipDesc>::Success(cached->second);
        }
        const auto request = mapper_->ResolveClip(id);
        if (!request)
        {
            return foundation::Result<animation::AnimationClipDesc>::Failure(request.GetError());
        }
        const auto payload = ResolvePayload(request.Value(), clip_leases_);
        if (!payload)
        {
            return foundation::Result<animation::AnimationClipDesc>::Failure(payload.GetError());
        }
        const auto typed = std::dynamic_pointer_cast<const IAnimationClipResourcePayload>(payload.Value());
        if (!typed)
        {
            TryRelease(request.Value().resource_id, clip_leases_);
            return Failure<animation::AnimationClipDesc>("animation.resource_payload_unsupported",
                                                         "resource payload does not expose an animation clip descriptor");
        }
        const animation::AnimationClipDesc desc = typed->GetClip();
        if (desc.id != id || !desc.skeleton.IsValid() || desc.duration_seconds <= 0.0f)
        {
            TryRelease(request.Value().resource_id, clip_leases_);
            return Failure<animation::AnimationClipDesc>("animation.invalid_clip",
                                                         "resource payload returned an invalid animation clip descriptor");
        }
        clip_cache_[id.value] = desc;
        TryRelease(request.Value().resource_id, clip_leases_);
        return foundation::Result<animation::AnimationClipDesc>::Success(desc);
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        std::optional<foundation::Error> first;
        ReleaseMap(skeleton_leases_, first);
        ReleaseMap(clip_leases_, first);
        if (first)
        {
            return foundation::Result<void>::Failure(*first);
        }
        skeleton_cache_.clear();
        clip_cache_.clear();
        return foundation::Result<void>::Success();
    }

  private:
    struct HeldResource
    {
        ResourceLease lease{};
        ResourcePayloadPtr payload{};
    };

    using HeldMap = std::map<std::uint64_t, HeldResource>;

    [[nodiscard]] foundation::Result<ResourcePayloadPtr>
        ResolvePayload(const ResourceRequest& request, HeldMap& held) const
    {
        if (shutdown_started_)
        {
            return Failure<ResourcePayloadPtr>("runtime_support.adapter_shutdown",
                                               "animation resource adapter is shutting down");
        }
        const std::uint64_t key = request.resource_id.Raw();
        auto iterator = held.find(key);
        if (iterator == held.end())
        {
            const auto lease = manager_->RequestLease(request);
            if (!lease)
            {
                return foundation::Result<ResourcePayloadPtr>::Failure(lease.GetError());
            }
            iterator = held.emplace(key, HeldResource{lease.Value(), {}}).first;
        }

        const ResourceState state = manager_->GetState(iterator->second.lease.resource);
        if (state != ResourceState::Ready)
        {
            const bool permanent = state == ResourceState::Failed || state == ResourceState::Evicted ||
                                   state == ResourceState::Unknown;
            if (permanent)
            {
                TryRelease(request.resource_id, held);
            }
            return Failure<ResourcePayloadPtr>(permanent ? "animation.resource_failed" : "animation.resource_not_ready",
                                               permanent ? "animation resource failed" : "animation resource is loading");
        }
        if (!iterator->second.payload)
        {
            iterator->second.payload = manager_->GetPayload(iterator->second.lease.resource);
        }
        if (!iterator->second.payload)
        {
            TryRelease(request.resource_id, held);
            return Failure<ResourcePayloadPtr>("animation.resource_failed", "ready animation resource has no payload");
        }
        return foundation::Result<ResourcePayloadPtr>::Success(iterator->second.payload);
    }

    void TryRelease(ResourceId resource, HeldMap& held) const
    {
        const auto iterator = held.find(resource.Raw());
        if (iterator == held.end())
        {
            return;
        }
        const auto released = manager_->Release(iterator->second.lease);
        if (released)
        {
            held.erase(iterator);
        }
    }

    void ReleaseMap(HeldMap& held, std::optional<foundation::Error>& first)
    {
        for (auto iterator = held.begin(); iterator != held.end();)
        {
            const auto released = manager_->Release(iterator->second.lease);
            if (released)
            {
                iterator = held.erase(iterator);
            }
            else
            {
                if (!first)
                {
                    first = released.GetError();
                }
                ++iterator;
            }
        }
    }

    std::shared_ptr<IResourceManager> manager_;
    std::shared_ptr<IAnimationResourceMapper> mapper_;
    mutable HeldMap skeleton_leases_;
    mutable HeldMap clip_leases_;
    mutable std::map<std::uint64_t, animation::SkeletonDesc> skeleton_cache_;
    mutable std::map<std::uint64_t, animation::AnimationClipDesc> clip_cache_;
    mutable bool shutdown_started_ = false;
};

class DefaultAudioResourceMapper final : public IAudioResourceMapper
{
  public:
    [[nodiscard]] foundation::Result<ResourceRequest> ResolveSound(audio::SoundId id) const override
    {
        if (!id.IsValid())
        {
            return Failure<ResourceRequest>("audio.invalid_sound", "sound id must be valid");
        }
        const std::string name = "audio.clip." + std::to_string(id.value);
        return foundation::Result<ResourceRequest>::Success(
            ResourceRequest{ResourceId::FromString(name.c_str()), AudioClipType()});
    }
};

class RuntimeAudioLeaseState
{
  public:
    explicit RuntimeAudioLeaseState(std::shared_ptr<IResourceManager> manager)
        : manager_(std::move(manager))
    {
    }

    void AddWrapper() noexcept { ++active_wrappers_; }

    void ReleaseWrapper(ResourceLease lease) noexcept
    {
        if (lease.IsValid())
        {
            const auto released = manager_->Release(lease);
            if (!released)
            {
                pending_releases_.push_back(lease);
            }
        }
        if (active_wrappers_ > 0)
        {
            --active_wrappers_;
        }
    }

    void ReleaseOrQueue(ResourceLease lease) noexcept
    {
        if (!lease.IsValid())
        {
            return;
        }
        const auto released = manager_->Release(lease);
        if (!released)
        {
            pending_releases_.push_back(lease);
        }
    }

    [[nodiscard]] foundation::Result<void> Drain()
    {
        std::optional<foundation::Error> first;
        std::vector<ResourceLease> retry;
        retry.reserve(pending_releases_.size());
        for (const ResourceLease& lease : pending_releases_)
        {
            const auto released = manager_->Release(lease);
            if (!released)
            {
                if (!first)
                {
                    first = released.GetError();
                }
                retry.push_back(lease);
            }
        }
        pending_releases_.swap(retry);
        if (first)
        {
            return foundation::Result<void>::Failure(*first);
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] std::size_t ActiveWrappers() const noexcept { return active_wrappers_; }

  private:
    std::shared_ptr<IResourceManager> manager_;
    std::vector<ResourceLease> pending_releases_;
    std::size_t active_wrappers_ = 0;
};

class LeasedAudioClipResource final : public audio::IAudioClipResource
{
  public:
    LeasedAudioClipResource(std::shared_ptr<const audio::IAudioClipResource> resource,
                            ResourceLease lease,
                            std::shared_ptr<RuntimeAudioLeaseState> lease_state)
        : resource_(std::move(resource)), lease_(lease), lease_state_(std::move(lease_state))
    {
        lease_state_->AddWrapper();
    }

    ~LeasedAudioClipResource() override
    {
        lease_state_->ReleaseWrapper(lease_);
    }

    [[nodiscard]] audio::AudioClipFormat GetFormat() const override { return resource_->GetFormat(); }
    [[nodiscard]] audio::AudioClipStorage GetStorage() const override { return resource_->GetStorage(); }
    [[nodiscard]] std::span<const std::byte> GetEncodedData() const override { return resource_->GetEncodedData(); }
    [[nodiscard]] std::shared_ptr<audio::IAudioStreamSource> GetStreamSource() const override
    {
        return resource_->GetStreamSource();
    }

  private:
    std::shared_ptr<const audio::IAudioClipResource> resource_;
    ResourceLease lease_{};
    std::shared_ptr<RuntimeAudioLeaseState> lease_state_;
};

class RuntimeAudioResourceSource final : public audio::IAudioResourceSource,
                                         public IRuntimeAdapterLifecycle
{
  public:
    RuntimeAudioResourceSource(std::shared_ptr<IResourceManager> manager,
                               std::shared_ptr<IAudioResourceMapper> mapper)
        : manager_(std::move(manager)),
          mapper_(std::move(mapper)),
          lease_state_(std::make_shared<RuntimeAudioLeaseState>(manager_))
    {
    }

    [[nodiscard]] audio::SoundState GetSoundState(audio::SoundId id) const override
    {
        if (shutdown_started_)
        {
            return audio::SoundState::Failed;
        }
        const auto request = mapper_->ResolveSound(id);
        if (!request)
        {
            return audio::SoundState::Missing;
        }
        auto iterator = held_.find(id.value);
        if (iterator == held_.end())
        {
            const auto lease = manager_->RequestLease(request.Value());
            if (!lease)
            {
                return audio::SoundState::Failed;
            }
            iterator = held_.emplace(id.value, lease.Value()).first;
        }

        switch (manager_->GetState(iterator->second.resource))
        {
        case ResourceState::Ready:
            lease_state_->ReleaseOrQueue(iterator->second);
            held_.erase(iterator);
            return audio::SoundState::Ready;
        case ResourceState::Failed:
        case ResourceState::Evicted:
        case ResourceState::Unknown:
            lease_state_->ReleaseOrQueue(iterator->second);
            held_.erase(iterator);
            return audio::SoundState::Failed;
        default:
            return audio::SoundState::Loading;
        }
    }

    [[nodiscard]] foundation::Result<audio::AudioClipPayload> LoadClip(audio::SoundId id) const override
    {
        if (shutdown_started_)
        {
            return Failure<audio::AudioClipPayload>("runtime_support.adapter_shutdown",
                                                    "audio resource adapter is shutting down");
        }
        const auto request = mapper_->ResolveSound(id);
        if (!request)
        {
            return foundation::Result<audio::AudioClipPayload>::Failure(request.GetError());
        }
        auto iterator = held_.find(id.value);
        if (iterator == held_.end())
        {
            const auto lease = manager_->RequestLease(request.Value());
            if (!lease)
            {
                return foundation::Result<audio::AudioClipPayload>::Failure(lease.GetError());
            }
            iterator = held_.emplace(id.value, lease.Value()).first;
        }

        const ResourceState state = manager_->GetState(iterator->second.resource);
        if (state != ResourceState::Ready)
        {
            const bool permanent = state == ResourceState::Failed || state == ResourceState::Evicted ||
                                   state == ResourceState::Unknown;
            if (permanent)
            {
                lease_state_->ReleaseOrQueue(iterator->second);
                held_.erase(iterator);
            }
            return Failure<audio::AudioClipPayload>(permanent ? "audio.resource_failed" : "audio.resource_not_ready",
                                                    permanent ? "audio resource failed" : "audio resource is loading");
        }

        const ResourcePayloadPtr payload = manager_->GetPayload(iterator->second.resource);
        if (!payload)
        {
            lease_state_->ReleaseOrQueue(iterator->second);
            held_.erase(iterator);
            return Failure<audio::AudioClipPayload>("audio.resource_failed", "ready audio resource has no payload");
        }
        const auto clip = std::dynamic_pointer_cast<const audio::IAudioClipResource>(payload);
        if (!clip)
        {
            lease_state_->ReleaseOrQueue(iterator->second);
            held_.erase(iterator);
            return Failure<audio::AudioClipPayload>("audio.resource_payload_unsupported",
                                                    "resource payload does not implement IAudioClipResource");
        }

        const ResourceLease transferred = iterator->second;
        held_.erase(iterator);
        auto leased_clip = std::make_shared<LeasedAudioClipResource>(clip, transferred, lease_state_);
        return foundation::Result<audio::AudioClipPayload>::Success(
            audio::AudioClipPayload{id, audio::SoundState::Ready, std::move(leased_clip)});
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        for (const auto& [id, lease] : held_)
        {
            (void)id;
            lease_state_->ReleaseOrQueue(lease);
        }
        held_.clear();

        const auto drained = lease_state_->Drain();
        if (!drained)
        {
            return drained;
        }
        if (lease_state_->ActiveWrappers() != 0)
        {
            return FailureVoid("runtime_support.audio_resource_in_use",
                               "audio resource leases are still owned by active clip consumers");
        }
        return foundation::Result<void>::Success();
    }

  private:
    std::shared_ptr<IResourceManager> manager_;
    std::shared_ptr<IAudioResourceMapper> mapper_;
    std::shared_ptr<RuntimeAudioLeaseState> lease_state_;
    mutable std::map<std::uint64_t, ResourceLease> held_;
    mutable bool shutdown_started_ = false;
};

class ReferenceAudioBackend final : public audio::IAudioBackend
{
  public:
    [[nodiscard]] bool IsEnabled() const override { return enabled_; }

    [[nodiscard]] foundation::Result<void> Initialize(const audio::AudioBackendOptions& options) override
    {
        enabled_ = options.enabled;
        initialized_ = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<audio::BackendVoiceHandle>
        CreateVoice(const audio::AudioVoiceDesc& desc) override
    {
        if (!initialized_ || !enabled_)
        {
            return Failure<audio::BackendVoiceHandle>("audio.backend_disabled", "reference audio backend is disabled");
        }
        const auto voice_value = AllocateMonotonicId(
            next_voice_,
            "audio.voice_id_overflow",
            "reference audio voice id allocator is exhausted");
        if (!voice_value)
        {
            return foundation::Result<audio::BackendVoiceHandle>::Failure(voice_value.GetError());
        }
        const audio::BackendVoiceHandle handle{voice_value.Value()};
        Voice voice{};
        voice.desc = desc;
        voice.gain = desc.initial_gain;
        voice.spatial = desc.spatial;
        const auto [voice_iterator, inserted] = voices_.emplace(handle.value, std::move(voice));
        if (!inserted)
        {
            return Failure<audio::BackendVoiceHandle>("audio.duplicate_voice_id", "allocated reference audio voice id already exists");
        }
        (void)voice_iterator;
        return foundation::Result<audio::BackendVoiceHandle>::Success(handle);
    }

    [[nodiscard]] foundation::Result<void> DestroyVoice(audio::BackendVoiceHandle handle) override
    {
        if (voices_.erase(handle.value) == 0)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Play(audio::BackendVoiceHandle handle) override
    {
        Voice* voice = Find(handle);
        if (!voice)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        voice->playing = true;
        voice->paused = false;
        voice->updates = 0;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Pause(audio::BackendVoiceHandle handle) override
    {
        Voice* voice = Find(handle);
        if (!voice)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        voice->playing = false;
        voice->paused = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Stop(audio::BackendVoiceHandle handle) override
    {
        Voice* voice = Find(handle);
        if (!voice)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        voice->playing = false;
        voice->paused = false;
        voice->finished = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] bool IsVoiceFinished(audio::BackendVoiceHandle handle) const override
    {
        const Voice* voice = Find(handle);
        return voice == nullptr || voice->finished || (!voice->desc.loop && voice->updates > 0);
    }

    [[nodiscard]] foundation::Result<void> SetGain(audio::BackendVoiceHandle handle, float gain) override
    {
        Voice* voice = Find(handle);
        if (!voice)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        if (!std::isfinite(gain) || gain < 0.0f)
        {
            return FailureVoid("audio.invalid_gain", "reference audio voice gain is invalid");
        }
        voice->gain = gain;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void>
        SetSpatialState(audio::BackendVoiceHandle handle, const audio::AudioSpatialState& state) override
    {
        Voice* voice = Find(handle);
        if (!voice)
        {
            return FailureVoid("audio.voice_not_found", "reference audio voice does not exist");
        }
        voice->spatial = state;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void>
        CreateBackendListener(audio::AudioListenerHandle handle, const Transform& transform) override
    {
        if (!handle.IsValid())
        {
            return FailureVoid("audio.invalid_listener", "reference listener handle is invalid");
        }
        if (listeners_.contains(handle.id.value))
        {
            return FailureVoid("audio.listener_exists", "reference listener already exists");
        }
        listeners_.emplace(handle.id.value, Listener{handle, transform});
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> DestroyBackendListener(audio::AudioListenerHandle handle) override
    {
        const auto iterator = listeners_.find(handle.id.value);
        if (iterator == listeners_.end() || iterator->second.handle.generation != handle.generation)
        {
            return FailureVoid("audio.listener_not_found", "reference listener does not exist");
        }
        listeners_.erase(iterator);
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void>
        SetBackendListenerTransform(audio::AudioListenerHandle handle, const Transform& transform) override
    {
        const auto iterator = listeners_.find(handle.id.value);
        if (iterator == listeners_.end() || iterator->second.handle.generation != handle.generation)
        {
            return FailureVoid("audio.listener_not_found", "reference listener does not exist");
        }
        iterator->second.transform = transform;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Update(RuntimeFrameDuration delta) override
    {
        if (delta.IsNegative())
        {
            return FailureVoid("audio.invalid_delta", "audio update delta must not be negative");
        }
        for (auto& [id, voice] : voices_)
        {
            (void)id;
            if (voice.playing)
            {
                ++voice.updates;
                if (!voice.desc.loop && voice.updates > 0)
                {
                    voice.finished = true;
                    voice.playing = false;
                }
            }
        }
        return foundation::Result<void>::Success();
    }

  private:
    struct Voice
    {
        audio::AudioVoiceDesc desc{};
        audio::AudioSpatialState spatial{};
        float gain = 1.0f;
        bool playing = false;
        bool paused = false;
        bool finished = false;
        std::uint32_t updates = 0;
    };

    struct Listener
    {
        audio::AudioListenerHandle handle{};
        Transform transform{};
    };

    [[nodiscard]] Voice* Find(audio::BackendVoiceHandle handle)
    {
        const auto iterator = voices_.find(handle.value);
        return iterator == voices_.end() ? nullptr : &iterator->second;
    }

    [[nodiscard]] const Voice* Find(audio::BackendVoiceHandle handle) const
    {
        const auto iterator = voices_.find(handle.value);
        return iterator == voices_.end() ? nullptr : &iterator->second;
    }

    bool initialized_ = false;
    bool enabled_ = true;
    std::uint64_t next_voice_ = 1;
    std::map<std::uint64_t, Voice> voices_;
    std::map<std::uint64_t, Listener> listeners_;
};

class ReferenceChunkStreamingManifestSource final : public IChunkStreamingManifestSource
{
  public:
    explicit ReferenceChunkStreamingManifestSource(std::shared_ptr<IChunkRegistry> chunks)
        : chunks_(std::move(chunks))
    {
    }

    [[nodiscard]] foundation::Result<ChunkStreamingManifest> GetManifest(ChunkId chunk) const override
    {
        const auto descriptor = chunks_->FindChunk(chunk);
        if (!descriptor)
        {
            return Failure<ChunkStreamingManifest>("runtime_support.chunk_not_found",
                                                   "streaming manifest requested for an unknown chunk");
        }
        ChunkStreamingManifest manifest{};
        manifest.chunk = chunk;
        manifest.persistence_location = PersistenceLocation{
            descriptor->region_id,
            descriptor->id,
            foundation::StringId::FromString("chunk")};
        return foundation::Result<ChunkStreamingManifest>::Success(std::move(manifest));
    }

  private:
    std::shared_ptr<IChunkRegistry> chunks_;
};

class RuntimeStreamingAdapter final : public streaming::IStreamingDataSource,
                                      public streaming::IStreamingCommitTarget,
                                      public streaming::IStreamingPriorityProvider,
                                      public streaming::IResidencyController,
                                      public streaming::IStreamingWorldSource,
                                      public streaming::IStreamingPersistenceSource,
                                      public streaming::IStreamingResourceSource,
                                      public IStreamingPreparedChunkDataQuery,
                                      public IRuntimeAdapterLifecycle
{
  public:
    RuntimeStreamingAdapter(std::shared_ptr<WorldServices> world,
                            std::shared_ptr<ResourceServices> resources,
                            std::shared_ptr<PersistenceServices> persistence,
                            std::shared_ptr<IChunkStreamingManifestSource> manifests)
        : world_(std::move(world)),
          resources_(std::move(resources)),
          persistence_(std::move(persistence)),
          manifests_(std::move(manifests))
    {
    }

    [[nodiscard]] foundation::Result<void> SetPreparedRevisionForTesting(std::uint64_t request_id,
                                                                         std::uint64_t revision)
    {
        const auto iterator = records_.find(request_id);
        if (iterator == records_.end())
        {
            return FailureVoid("runtime_support.streaming_record_missing",
                               "streaming request has no preparation record");
        }
        iterator->second.prepared_revision = revision;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<streaming::ProgressiveLoadPlan>
        BuildLoadPlan(const streaming::StreamingRequest& request) override
    {
        if (shutdown_started_)
        {
            return Failure<streaming::ProgressiveLoadPlan>("runtime_support.adapter_shutdown",
                                                           "streaming adapter is shutting down");
        }
        const auto chunk = ChunkFrom(request);
        if (!chunk)
        {
            return Failure<streaming::ProgressiveLoadPlan>("streaming.unsupported_target",
                                                           "runtime streaming adapter supports chunk targets only");
        }
        if (!request.id.IsValid() || !request.handle.IsValid() || request.handle.id != request.id)
        {
            return Failure<streaming::ProgressiveLoadPlan>("runtime_support.invalid_streaming_request",
                                                           "streaming request identity must be valid and consistent");
        }
        if (records_.contains(request.id.value))
        {
            return Failure<streaming::ProgressiveLoadPlan>("runtime_support.duplicate_streaming_request",
                                                           "streaming request id already has a preparation record");
        }
        const auto snapshot = world_->chunks->GetChunkSnapshot(*chunk);
        if (!snapshot)
        {
            return foundation::Result<streaming::ProgressiveLoadPlan>::Failure(snapshot.GetError());
        }
        const auto manifest = manifests_->GetManifest(*chunk);
        if (!manifest)
        {
            return foundation::Result<streaming::ProgressiveLoadPlan>::Failure(manifest.GetError());
        }
        if (manifest.Value().chunk != *chunk)
        {
            return Failure<streaming::ProgressiveLoadPlan>("runtime_support.invalid_streaming_manifest",
                                                           "streaming manifest chunk does not match request");
        }

        std::size_t resource_bytes = 0;
        for (const ChunkResourceRequirement& requirement : manifest.Value().resources)
        {
            if (requirement.estimated_bytes > std::numeric_limits<std::size_t>::max() - resource_bytes)
            {
                return Failure<streaming::ProgressiveLoadPlan>("streaming.byte_counter_overflow",
                                                               "streaming manifest byte estimate overflow");
            }
            resource_bytes += requirement.estimated_bytes;
        }

        try
        {
            PreparedRecord record{};
            record.request = request.handle;
            record.chunk = *chunk;
            record.original = snapshot.Value();
            record.manifest = manifest.Value();
            // Reserve all transient lease bookkeeping before any resource acquisition.
            record.temporary_leases.reserve(record.manifest.resources.size());

            streaming::ProgressiveLoadPlan plan{};
            plan.steps.reserve(4);
            plan.steps.push_back(streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::ResolveTarget, 1});
            plan.steps.push_back(streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::PrepareData, 1});
            plan.steps.push_back(streaming::StreamingPlanStepRecord{
                streaming::StreamingPlanStep::PrepareResources,
                std::max<std::size_t>(resource_bytes, 1)});
            plan.steps.push_back(streaming::StreamingPlanStepRecord{streaming::StreamingPlanStep::Commit, 1});

            if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_streaming_plan_construction))
            {
                throw std::bad_alloc{};
            }

            // Publish the fully constructed record only after the returned plan is also
            // complete. try_emplace preserves any pre-existing record on failure.
            const auto [iterator, inserted] = records_.try_emplace(request.id.value, std::move(record));
            (void)iterator;
            if (!inserted)
            {
                return Failure<streaming::ProgressiveLoadPlan>("runtime_support.duplicate_streaming_request",
                                                               "streaming request id already has a preparation record");
            }
            return foundation::Result<streaming::ProgressiveLoadPlan>::Success(std::move(plan));
        }
        catch (const std::bad_alloc&)
        {
            return Failure<streaming::ProgressiveLoadPlan>("runtime_support.allocation_failure",
                                                           "could not allocate streaming preparation state");
        }
    }

    [[nodiscard]] foundation::Result<streaming::StreamingStepResult>
        ExecuteStep(const streaming::StreamingRequest& request,
                    streaming::StreamingPlanStepRecord step,
                    const RuntimeBudget& available_budget) override
    {
        PreparedRecord* record = FindRecord(request);
        if (!record)
        {
            return Failure<streaming::StreamingStepResult>("runtime_support.streaming_record_missing",
                                                           "streaming request has no preparation record");
        }
        switch (step.step)
        {
        case streaming::StreamingPlanStep::ResolveTarget:
        {
            const auto resolved = ResolveTarget(*record);
            if (!resolved)
            {
                return foundation::Result<streaming::StreamingStepResult>::Failure(resolved.GetError());
            }
            return CompletedStep(step, available_budget);
        }
        case streaming::StreamingPlanStep::PrepareData:
        {
            const auto prepared = PrepareData(*record);
            if (!prepared)
            {
                return foundation::Result<streaming::StreamingStepResult>::Failure(prepared.GetError());
            }
            return CompletedStep(step, available_budget);
        }
        case streaming::StreamingPlanStep::PrepareResources:
            return PrepareResources(*record, step, available_budget);
        case streaming::StreamingPlanStep::Commit:
            return CompletedStep(step, available_budget);
        case streaming::StreamingPlanStep::Rollback:
        case streaming::StreamingPlanStep::Release:
            return CompletedStep(step, available_budget);
        }
        return Failure<streaming::StreamingStepResult>("streaming.invalid_step", "unknown streaming plan step");
    }

    [[nodiscard]] foundation::Result<void> Commit(const streaming::StreamingRequest& request) override
    {
        PreparedRecord* record = FindRecord(request);
        if (!record)
        {
            return FailureVoid("runtime_support.streaming_record_missing", "streaming commit record is missing");
        }
        if (record->committed)
        {
            return foundation::Result<void>::Success();
        }
        const auto snapshot = world_->chunks->GetChunkSnapshot(record->chunk);
        if (!snapshot)
        {
            return foundation::Result<void>::Failure(snapshot.GetError());
        }
        if (snapshot.Value().state != ChunkState::Loading)
        {
            return FailureVoid("runtime_support.invalid_chunk_state", "streaming commit requires Loading chunk state");
        }
        // Reserve the local active index before the World mutation.  Publication into
        // std::map is the last fallible local operation; if it cannot be prepared,
        // the external chunk state remains Loading.
        bool active_index_inserted = false;
        try
        {
            const auto [iterator, inserted] = active_by_chunk_.try_emplace(record->chunk.Raw(), request.id.value);
            if (!inserted && iterator->second != request.id.value)
            {
                return FailureVoid("runtime_support.chunk_already_active",
                                   "streaming chunk is already owned by another active request");
            }
            active_index_inserted = inserted;
        }
        catch (const std::bad_alloc&)
        {
            return FailureVoid("runtime_support.allocation_failure",
                               "could not reserve active streaming ownership before World commit");
        }

        foundation::Result<void> changed = foundation::Result<void>::Success();
        try
        {
            changed = world_->chunks->SetChunkState(
                ChangeChunkStateCommand{record->chunk, snapshot.Value().revision, ChunkState::Resident});
        }
        catch (const std::exception&)
        {
            changed = FailureVoid("runtime_support.world_exception", "World chunk transition threw an exception");
        }
        catch (...)
        {
            changed = FailureVoid("runtime_support.world_exception", "World chunk transition threw a non-standard exception");
        }
        if (!changed)
        {
            if (active_index_inserted)
            {
                active_by_chunk_.erase(record->chunk.Raw());
            }
            return changed;
        }
        record->active_leases = std::move(record->temporary_leases);
        record->committed = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Rollback(const streaming::StreamingRequest& request) override
    {
        PreparedRecord* record = FindRecord(request);
        if (!record)
        {
            return foundation::Result<void>::Success();
        }

        const auto cleaned = CleanupRecord(*record);
        if (!cleaned)
        {
            return cleaned;
        }

        active_by_chunk_.erase(record->chunk.Raw());
        records_.erase(request.id.value);
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] streaming::StreamingPriorityClass
        GetPriority(const streaming::StreamingTarget& target) const override
    {
        const auto chunk = std::get_if<streaming::ChunkStreamingTarget>(&target);
        if (!chunk || !chunk->chunk.IsValid())
        {
            return streaming::StreamingPriorityClass::Background;
        }
        const auto snapshot = world_->chunks->GetChunkSnapshot(chunk->chunk);
        if (!snapshot)
        {
            return streaming::StreamingPriorityClass::Background;
        }
        return snapshot.Value().state == ChunkState::Active
                   ? streaming::StreamingPriorityClass::High
                   : streaming::StreamingPriorityClass::Normal;
    }

    [[nodiscard]] foundation::Result<void> ActivateChunk(ChunkId chunk) override
    {
        return TransitionChunk(chunk, {ChunkState::Resident, ChunkState::Sleeping}, ChunkState::Active,
                               "runtime_support.chunk_not_resident");
    }

    [[nodiscard]] foundation::Result<void> DeactivateChunk(ChunkId chunk) override
    {
        return TransitionChunk(chunk,
                               {ChunkState::Active, ChunkState::Resident, ChunkState::Sleeping},
                               ChunkState::Unloading,
                               "runtime_support.chunk_not_loaded");
    }

    [[nodiscard]] foundation::Result<void> UnloadChunk(ChunkId chunk) override
    {
        const auto active = active_by_chunk_.find(chunk.Raw());
        if (active != active_by_chunk_.end())
        {
            const auto record = records_.find(active->second);
            if (record != records_.end())
            {
                const auto cleaned = CleanupRecord(record->second);
                if (!cleaned)
                {
                    return cleaned;
                }
                records_.erase(record);
                active_by_chunk_.erase(active);
                return foundation::Result<void>::Success();
            }
        }
        const auto result = TransitionChunk(chunk, {ChunkState::Unloading}, ChunkState::Unloaded,
                                            "runtime_support.chunk_not_unloading");
        if (!result)
        {
            return result;
        }
        if (active != active_by_chunk_.end())
        {
            records_.erase(active->second);
            active_by_chunk_.erase(active);
        }
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] std::optional<RegionId> ResolveRegion(ChunkId chunk) const override
    {
        const auto descriptor = world_->chunks->FindChunk(chunk);
        return descriptor ? std::optional<RegionId>{descriptor->region_id} : std::nullopt;
    }

    [[nodiscard]] foundation::Result<void>
        PrepareChunkData(const streaming::StreamingRequest& request) override
    {
        PreparedRecord* record = FindRecord(request);
        return record ? PrepareData(*record)
                      : FailureVoid("runtime_support.streaming_record_missing", "streaming record is missing");
    }

    [[nodiscard]] foundation::Result<void>
        PrepareChunkResources(const streaming::StreamingRequest& request) override
    {
        PreparedRecord* record = FindRecord(request);
        if (!record)
        {
            return FailureVoid("runtime_support.streaming_record_missing", "streaming record is missing");
        }
        const auto result = PrepareResources(*record, streaming::StreamingPlanStepRecord{
                                                          streaming::StreamingPlanStep::PrepareResources,
                                                          std::numeric_limits<std::size_t>::max()},
                                             RuntimeBudget{});
        return result ? foundation::Result<void>::Success()
                      : foundation::Result<void>::Failure(result.GetError());
    }

    [[nodiscard]] foundation::Result<void> ReleaseChunkResources(ChunkId chunk) override
    {
        const auto active = active_by_chunk_.find(chunk.Raw());
        if (active == active_by_chunk_.end())
        {
            return foundation::Result<void>::Success();
        }
        const auto record = records_.find(active->second);
        if (record == records_.end())
        {
            active_by_chunk_.erase(active);
            return foundation::Result<void>::Success();
        }
        if (record->second.cleanup_resources_released)
        {
            return foundation::Result<void>::Success();
        }
        std::optional<foundation::Error> first;
        ReleaseLeases(record->second.active_leases, first);
        if (first)
        {
            return foundation::Result<void>::Failure(*first);
        }
        record->second.cleanup_resources_released = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<PreparedChunkDataSnapshot>
        GetPreparedData(ChunkId chunk) const override
    {
        for (const auto& [id, record] : records_)
        {
            (void)id;
            if (record.chunk == chunk)
            {
                return foundation::Result<PreparedChunkDataSnapshot>::Success(
                    PreparedChunkDataSnapshot{record.chunk,
                                              record.manifest.persistence_location,
                                              record.persisted,
                                              record.prepared_revision});
            }
        }
        return Failure<PreparedChunkDataSnapshot>("runtime_support.prepared_chunk_missing",
                                                  "prepared streaming data is not available for this chunk");
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        shutdown_started_ = true;
        std::optional<foundation::Error> first;

        for (auto iterator = records_.begin(); iterator != records_.end();)
        {
            const auto cleaned = CleanupRecord(iterator->second);
            if (cleaned)
            {
                active_by_chunk_.erase(iterator->second.chunk.Raw());
                iterator = records_.erase(iterator);
            }
            else
            {
                if (!first)
                {
                    first = cleaned.GetError();
                }
                ++iterator;
            }
        }

        if (first)
        {
            return foundation::Result<void>::Failure(*first);
        }
        active_by_chunk_.clear();
        return foundation::Result<void>::Success();
    }

  private:
    struct PreparedRecord
    {
        streaming::StreamingRequestHandle request{};
        ChunkId chunk{};
        ChunkSnapshot original{};
        ChunkStreamingManifest manifest{};
        std::optional<ZoneOverrideSnapshot> persisted{};
        std::vector<ResourceLease> temporary_leases;
        std::vector<ResourceLease> active_leases;
        std::size_t acquired_resource_count = 0;
        std::size_t accounted_resource_bytes = 0;
        bool world_loading = false;
        bool committed = false;
        bool cleanup_resources_released = false;
        bool cleanup_world_unloaded = false;
        std::optional<ResourceLease> pending_acquisition_rollback{};
        std::uint64_t prepared_revision = 0;
    };

    [[nodiscard]] static std::optional<ChunkId> ChunkFrom(const streaming::StreamingRequest& request)
    {
        if (const auto* target = std::get_if<streaming::ChunkStreamingTarget>(&request.target))
        {
            return target->chunk;
        }
        return std::nullopt;
    }

    [[nodiscard]] PreparedRecord* FindRecord(const streaming::StreamingRequest& request)
    {
        const auto iterator = records_.find(request.id.value);
        return iterator == records_.end() ? nullptr : &iterator->second;
    }

    [[nodiscard]] foundation::Result<void> ResolveTarget(PreparedRecord& record)
    {
        if (record.world_loading)
        {
            return foundation::Result<void>::Success();
        }
        const auto snapshot = world_->chunks->GetChunkSnapshot(record.chunk);
        if (!snapshot)
        {
            return foundation::Result<void>::Failure(snapshot.GetError());
        }
        if (snapshot.Value().state != ChunkState::Unloaded)
        {
            return FailureVoid("runtime_support.invalid_chunk_state",
                               "new streaming load requires an Unloaded chunk");
        }
        const auto changed = world_->chunks->SetChunkState(
            ChangeChunkStateCommand{record.chunk, snapshot.Value().revision, ChunkState::Loading});
        if (!changed)
        {
            return changed;
        }
        record.world_loading = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> PrepareData(PreparedRecord& record)
    {
        if (record.prepared_revision == std::numeric_limits<std::uint64_t>::max())
        {
            return FailureVoid("runtime_support.prepared_revision_overflow",
                               "prepared streaming data revision is exhausted");
        }

        std::optional<ZoneOverrideSnapshot> candidate;
        try
        {
            if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_persistence_exception))
            {
                throw std::runtime_error("injected persistence callback failure");
            }
            candidate = persistence_->query->FindZoneOverride(record.manifest.persistence_location);
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.persistence_exception",
                                          "persistence query threw while preparing chunk data",
                                          exception.what()));
        }
        catch (...)
        {
            return FailureVoid("runtime_support.persistence_exception",
                               "persistence query threw while preparing chunk data");
        }

        record.persisted.swap(candidate);
        ++record.prepared_revision;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<streaming::StreamingStepResult>
        PrepareResources(PreparedRecord& record,
                         streaming::StreamingPlanStepRecord step,
                         const RuntimeBudget& available_budget)
    {
        if (record.pending_acquisition_rollback)
        {
            const auto rolled_back = ReleaseLease(*record.pending_acquisition_rollback);
            if (!rolled_back)
            {
                return foundation::Result<streaming::StreamingStepResult>::Failure(rolled_back.GetError());
            }
            record.pending_acquisition_rollback.reset();
        }

        if (record.temporary_leases.capacity() < record.manifest.resources.size())
        {
            try
            {
                record.temporary_leases.reserve(record.manifest.resources.size());
            }
            catch (const std::bad_alloc&)
            {
                return Failure<streaming::StreamingStepResult>("runtime_support.allocation_failure",
                                                               "could not reserve streaming lease bookkeeping");
            }
        }

        while (record.acquired_resource_count < record.manifest.resources.size())
        {
            const ChunkResourceRequirement& requirement =
                record.manifest.resources[record.acquired_resource_count];
            const auto lease = RequestLease(ResourceRequest{requirement.resource, requirement.type});
            if (!lease)
            {
                return foundation::Result<streaming::StreamingStepResult>::Failure(lease.GetError());
            }

            if (ConsumeStreamingLeasePublicationFault())
            {
                const auto rollback = ReleaseLease(lease.Value());
                if (!rollback)
                {
                    record.pending_acquisition_rollback = lease.Value();
                    return Failure<streaming::StreamingStepResult>(
                        "runtime_support.streaming_cleanup_pending",
                        "injected streaming lease publication failure left cleanup pending");
                }
                return Failure<streaming::StreamingStepResult>("runtime_support.allocation_failure",
                                                               "injected streaming lease publication failure");
            }

            try
            {
                record.temporary_leases.push_back(lease.Value());
            }
            catch (...)
            {
                const auto rollback = ReleaseLease(lease.Value());
                if (!rollback)
                {
                    record.pending_acquisition_rollback = lease.Value();
                    return Failure<streaming::StreamingStepResult>(
                        "runtime_support.streaming_cleanup_pending",
                        "streaming lease publication failed and rollback is pending");
                }
                return Failure<streaming::StreamingStepResult>("runtime_support.allocation_failure",
                                                               "could not publish acquired streaming lease");
            }
            ++record.acquired_resource_count;
        }

        for (const ResourceLease& lease : record.temporary_leases)
        {
            ResourceState state = ResourceState::Unknown;
            try
            {
                state = resources_->manager->GetState(lease.resource);
            }
            catch (const std::exception& exception)
            {
                return foundation::Result<streaming::StreamingStepResult>::Failure(
                    foundation::Error::Create("runtime_support.resource_manager_exception",
                                              "resource manager threw while reading streaming resource state",
                                              exception.what()));
            }
            catch (...)
            {
                return Failure<streaming::StreamingStepResult>("runtime_support.resource_manager_exception",
                                                               "resource manager threw while reading streaming resource state");
            }
            if (state == ResourceState::Failed || state == ResourceState::Evicted || state == ResourceState::Unknown)
            {
                return Failure<streaming::StreamingStepResult>("runtime_support.streaming_resource_failed",
                                                               "chunk resource failed to load");
            }
            if (state != ResourceState::Ready)
            {
                return foundation::Result<streaming::StreamingStepResult>::Success(
                    streaming::StreamingStepResult{0, false});
            }
        }

        const std::size_t total = std::max<std::size_t>(step.estimated_bytes, 1);
        const std::size_t already = std::min(record.accounted_resource_bytes, total);
        const std::size_t remaining = total - already;
        const std::size_t allowed = available_budget.HasByteLimit()
                                        ? static_cast<std::size_t>(std::min<std::uint64_t>(remaining,
                                                                                          available_budget.max_bytes))
                                        : remaining;
        record.accounted_resource_bytes += allowed;
        return foundation::Result<streaming::StreamingStepResult>::Success(
            streaming::StreamingStepResult{allowed, record.accounted_resource_bytes >= total});
    }

    [[nodiscard]] static foundation::Result<streaming::StreamingStepResult>
        CompletedStep(streaming::StreamingPlanStepRecord step, const RuntimeBudget& available_budget)
    {
        const std::size_t total = std::max<std::size_t>(step.estimated_bytes, 1);
        const std::size_t remaining = total > step.processed_bytes ? total - step.processed_bytes : 0;
        const std::size_t allowed = available_budget.HasByteLimit()
                                        ? static_cast<std::size_t>(std::min<std::uint64_t>(remaining,
                                                                                          available_budget.max_bytes))
                                        : remaining;
        return foundation::Result<streaming::StreamingStepResult>::Success(
            streaming::StreamingStepResult{allowed, allowed >= remaining});
    }

    [[nodiscard]] foundation::Result<void> CleanupRecord(PreparedRecord& record)
    {
        auto snapshot = world_->chunks->GetChunkSnapshot(record.chunk);
        if (!snapshot)
        {
            return foundation::Result<void>::Failure(snapshot.GetError());
        }

        ChunkSnapshot current = snapshot.Value();
        if (current.state == ChunkState::Resident || current.state == ChunkState::Active ||
            current.state == ChunkState::Sleeping)
        {
            const auto unloading = world_->chunks->SetChunkState(
                ChangeChunkStateCommand{record.chunk, current.revision, ChunkState::Unloading});
            if (!unloading)
            {
                return unloading;
            }
            snapshot = world_->chunks->GetChunkSnapshot(record.chunk);
            if (!snapshot)
            {
                return foundation::Result<void>::Failure(snapshot.GetError());
            }
            current = snapshot.Value();
        }

        std::optional<foundation::Error> first;
        if (current.state == ChunkState::Loading)
        {
            if (!record.world_loading)
            {
                return FailureVoid("runtime_support.streaming_ownership_mismatch",
                                   "streaming adapter does not own the existing Loading transition");
            }
            if (!record.cleanup_resources_released)
            {
                ReleaseLeases(record.temporary_leases, first);
                if (first)
                {
                    return foundation::Result<void>::Failure(*first);
                }
                record.cleanup_resources_released = true;
            }
            if (!record.cleanup_world_unloaded)
            {
                if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_streaming_cleanup_world_transition))
                {
                    return FailureVoid("runtime_support.injected_world_transition_failure",
                                       "injected streaming cleanup World transition failure");
                }
                const auto unloaded = world_->chunks->SetChunkState(
                    ChangeChunkStateCommand{record.chunk, current.revision, ChunkState::Unloaded});
                if (!unloaded)
                {
                    return unloaded;
                }
                record.cleanup_world_unloaded = true;
            }
            record.persisted.reset();
            return foundation::Result<void>::Success();
        }

        if (current.state == ChunkState::Unloading)
        {
            if (!record.cleanup_resources_released)
            {
                ReleaseLeases(record.active_leases, first);
                ReleaseLeases(record.temporary_leases, first);
                if (first)
                {
                    return foundation::Result<void>::Failure(*first);
                }
                record.cleanup_resources_released = true;
            }
            if (!record.cleanup_world_unloaded)
            {
                if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_streaming_cleanup_world_transition))
                {
                    return FailureVoid("runtime_support.injected_world_transition_failure",
                                       "injected streaming cleanup World transition failure");
                }
                const auto unloaded = world_->chunks->SetChunkState(
                    ChangeChunkStateCommand{record.chunk, current.revision, ChunkState::Unloaded});
                if (!unloaded)
                {
                    return unloaded;
                }
                record.cleanup_world_unloaded = true;
            }
            record.persisted.reset();
            return foundation::Result<void>::Success();
        }

        if (current.state == ChunkState::Unloaded)
        {
            if (!record.cleanup_resources_released)
            {
                ReleaseLeases(record.temporary_leases, first);
                ReleaseLeases(record.active_leases, first);
                if (first)
                {
                    return foundation::Result<void>::Failure(*first);
                }
                record.cleanup_resources_released = true;
            }
            record.cleanup_world_unloaded = true;
            record.persisted.reset();
            return foundation::Result<void>::Success();
        }

        return FailureVoid("runtime_support.streaming_cleanup_state",
                           "streaming adapter cannot clean a chunk from its current World state");
    }

    [[nodiscard]] foundation::Result<void>
        TransitionChunk(ChunkId chunk,
                        std::initializer_list<ChunkState> allowed,
                        ChunkState target,
                        std::string_view error_code)
    {
        const auto snapshot = world_->chunks->GetChunkSnapshot(chunk);
        if (!snapshot)
        {
            return foundation::Result<void>::Failure(snapshot.GetError());
        }
        if (snapshot.Value().state == target)
        {
            return foundation::Result<void>::Success();
        }
        if (std::find(allowed.begin(), allowed.end(), snapshot.Value().state) == allowed.end())
        {
            return FailureVoid(error_code, "chunk is not in a state accepted by the residency transition");
        }
        return world_->chunks->SetChunkState(
            ChangeChunkStateCommand{chunk, snapshot.Value().revision, target});
    }

    void ReleaseLeases(std::vector<ResourceLease>& leases, std::optional<foundation::Error>& first)
    {
        std::vector<ResourceLease> retry;
        try
        {
            retry.reserve(leases.size());
        }
        catch (const std::bad_alloc&)
        {
            if (!first)
            {
                first = foundation::Error::Create("runtime_support.allocation_failure",
                                                  "could not allocate streaming cleanup bookkeeping");
            }
            return;
        }
        for (const ResourceLease& lease : leases)
        {
            const auto released = ReleaseLease(lease);
            if (!released)
            {
                if (!first)
                {
                    first = released.GetError();
                }
                retry.push_back(lease);
            }
        }
        leases.swap(retry);
    }

    [[nodiscard]] foundation::Result<ResourceLease> RequestLease(ResourceRequest request)
    {
        try
        {
            return resources_->manager->RequestLease(request);
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<ResourceLease>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while acquiring a streaming lease",
                                          exception.what()));
        }
        catch (...)
        {
            return foundation::Result<ResourceLease>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while acquiring a streaming lease"));
        }
    }

    [[nodiscard]] foundation::Result<void> ReleaseLease(ResourceLease lease)
    {
        try
        {
            return resources_->manager->Release(lease);
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while releasing a streaming lease",
                                          exception.what()));
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.resource_manager_exception",
                                          "resource manager threw while releasing a streaming lease"));
        }
    }

    std::shared_ptr<WorldServices> world_;
    std::shared_ptr<ResourceServices> resources_;
    std::shared_ptr<PersistenceServices> persistence_;
    std::shared_ptr<IChunkStreamingManifestSource> manifests_;
    std::map<std::uint64_t, PreparedRecord> records_;
    std::map<std::uint64_t, std::uint64_t> active_by_chunk_;
    bool shutdown_started_ = false;
};

class RuntimeNavigationEnvironmentAdapter final : public navigation::INavigationDataSource,
                                                   public navigation::INavCostProvider,
                                                   public navigation::INavigationObstacleSource
{
  public:
    explicit RuntimeNavigationEnvironmentAdapter(std::shared_ptr<EnvironmentServices> environment)
        : environment_(std::move(environment))
    {
    }

    [[nodiscard]] navigation::NavigationRevision CurrentRevision(RegionId region) const override
    {
        const auto revision = environment_->query->GetRegionRevision(region);
        return navigation::NavigationRevision{revision ? revision.Value() : 0};
    }

    [[nodiscard]] float GetTraversalCost(const navigation::NavCostQuery& query) const override
    {
        float cost = 1.0f;
        if (query.surface.IsValid())
        {
            const auto surface = environment_->query->GetSurfaceState(query.surface);
            if (surface)
            {
                switch (surface.Value().condition)
                {
                case SurfaceConditionKind::Dry:
                    cost = 1.0f;
                    break;
                case SurfaceConditionKind::Wet:
                    cost = 1.1f;
                    break;
                case SurfaceConditionKind::Muddy:
                    cost = 1.5f;
                    break;
                case SurfaceConditionKind::SnowCovered:
                    cost = 1.35f;
                    break;
                case SurfaceConditionKind::Icy:
                    cost = 1.65f;
                    break;
                case SurfaceConditionKind::Frozen:
                    cost = 1.2f;
                    break;
                case SurfaceConditionKind::Thawing:
                    cost = 1.4f;
                    break;
                case SurfaceConditionKind::Drying:
                    cost = 1.15f;
                    break;
                }
                return cost;
            }
        }
        const auto projection = environment_->query->BuildProjection(query.region);
        if (projection)
        {
            cost += std::clamp(projection.Value().weather.precipitation, 0.0f, 1.0f) * 0.25f;
            cost += std::clamp(projection.Value().weather.wind_speed / 100.0f, 0.0f, 0.5f);
        }
        return cost;
    }

    [[nodiscard]] std::span<const navigation::DynamicObstacle>
        ObstaclesForRegion(RegionId) const override
    {
        return obstacles_;
    }

  private:
    std::shared_ptr<EnvironmentServices> environment_;
    std::vector<navigation::DynamicObstacle> obstacles_;
};

class InMemoryRuntimeEventSink final : public IRuntimeEventSink
{
  public:
    [[nodiscard]] foundation::Result<void> Publish(const RuntimeFrameEvents& events) override
    {
        last_ = events;
        return foundation::Result<void>::Success();
    }

  private:
    RuntimeFrameEvents last_{};
};

class ReferenceRenderCommandSink final : public renderer::IRenderCommandSink
{
  public:
    [[nodiscard]] foundation::Result<void> BeginFrame(const renderer::RenderFrameContext& context) override
    {
        if (in_frame_)
        {
            return FailureVoid("runtime_support.renderer_frame_active", "reference render frame is already active");
        }
        current_context_ = context;
        current_submissions_.clear();
        in_frame_ = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void>
        SubmitProxy(const renderer::RenderProxySubmission& submission) override
    {
        if (!in_frame_)
        {
            return FailureVoid("runtime_support.renderer_frame_not_started", "BeginFrame must be called first");
        }
        current_submissions_.push_back(submission);
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> EndFrame() override
    {
        if (!in_frame_)
        {
            return FailureVoid("runtime_support.renderer_frame_not_started", "no reference render frame is active");
        }
        last_context_ = current_context_;
        last_submissions_ = current_submissions_;
        in_frame_ = false;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> AbortFrame() override
    {
        current_submissions_.clear();
        in_frame_ = false;
        return foundation::Result<void>::Success();
    }

  private:
    bool in_frame_ = false;
    renderer::RenderFrameContext current_context_{};
    renderer::RenderFrameContext last_context_{};
    std::vector<renderer::RenderProxySubmission> current_submissions_;
    std::vector<renderer::RenderProxySubmission> last_submissions_;
};

struct RuntimeServiceSnapshot
{
    std::shared_ptr<TimeServices> time;
    std::shared_ptr<ResourceServices> resources;
    std::shared_ptr<PersistenceServices> persistence;
    std::shared_ptr<EnvironmentServices> environment;
    std::shared_ptr<SceneServices> scene;
    std::shared_ptr<WorldServices> world;
    std::shared_ptr<streaming::StreamingServices> streaming;
    std::shared_ptr<simulation::SimulationServices> simulation;
    std::shared_ptr<navigation::NavigationServices> navigation;
    std::shared_ptr<animation::AnimationServices> animation;
    std::shared_ptr<physics::PhysicsServices> physics;
    std::shared_ptr<audio::AudioServices> audio;
    std::shared_ptr<renderer::RendererServices> renderer;
};

class EngineRuntimeCoordinator final : public IEngineRuntimeCoordinator
{
  public:
    EngineRuntimeCoordinator(RuntimeServiceSnapshot services,
                             std::shared_ptr<RuntimeIntegrationServices> integrations,
                             EngineRuntimeOptions options)
        : services_(std::move(services)),
          integrations_(std::move(integrations)),
          options_(std::move(options))
    {
    }

    [[nodiscard]] foundation::Result<RuntimeTickResult> Tick(const RuntimeFrameInput& input) override
    {
        if (shutdown_started_)
        {
            return Failure<RuntimeTickResult>("runtime_support.coordinator_shutdown",
                                              "runtime coordinator no longer accepts frames");
        }
        if (input.real_delta.IsNegative())
        {
            return Failure<RuntimeTickResult>("runtime_support.invalid_frame_delta",
                                              "runtime frame delta must not be negative");
        }

        RuntimeTickResult result{};
        integrations_->last_update_order.clear();
        auto record_step = [&](RuntimeUpdateStep step) {
            integrations_->last_update_order.push_back(step);
            result.executed_steps.push_back(step);
        };
        auto record_failure = [&](RuntimeUpdateStep step, const foundation::Error& error) {
            result.failures.push_back(RuntimePhaseFailure{step, error});
        };

        if (services_.time)
        {
            record_step(RuntimeUpdateStep::Time);
            const auto advanced = services_.time->runtime->Advance(input.real_delta.value);
            if (advanced)
            {
                result.time = advanced.Value();
            }
            else
            {
                record_failure(RuntimeUpdateStep::Time, advanced.GetError());
            }
        }

        if (services_.simulation)
        {
            record_step(RuntimeUpdateStep::MainThreadCommits);
            const auto main_thread = services_.simulation->runtime->ProcessMainThreadCommits();
            if (!main_thread)
            {
                record_failure(RuntimeUpdateStep::MainThreadCommits, main_thread.GetError());
            }

            const std::uint32_t commit_limit = input.max_simulation_commits != 0
                                                   ? input.max_simulation_commits
                                                   : options_.max_simulation_commits_per_frame;
            std::uint32_t committed = 0;
            while (!services_.simulation->proposals->PendingBatches().empty() &&
                   (commit_limit == 0 || committed < commit_limit))
            {
                const auto commit = services_.simulation->proposals->CommitNext();
                if (!commit)
                {
                    record_failure(RuntimeUpdateStep::MainThreadCommits, commit.GetError());
                    break;
                }
                ++committed;
            }
        }

        if (services_.resources)
        {
            record_step(RuntimeUpdateStep::Resources);
            const auto processed = services_.resources->manager->ProcessPendingLoads(input.resource_budget);
            if (!processed)
            {
                record_failure(RuntimeUpdateStep::Resources, processed.GetError());
            }
        }

        if (services_.streaming)
        {
            record_step(RuntimeUpdateStep::Streaming);
            const streaming::StreamingBudget budget = input.streaming_budget.IsUnlimited()
                                                          ? options_.streaming
                                                          : input.streaming_budget;
            services_.streaming->runtime->SetBudget(budget);
            const streaming::StreamingTickResult tick = services_.streaming->runtime->Tick();
            for (const streaming::StreamingTickFailure& failure : tick.failures)
            {
                record_failure(RuntimeUpdateStep::Streaming, failure.error);
            }
        }

        if (services_.simulation)
        {
            record_step(RuntimeUpdateStep::Simulation);
            const auto tick = services_.simulation->runtime->Tick();
            if (!tick)
            {
                record_failure(RuntimeUpdateStep::Simulation, tick.GetError());
            }
            else
            {
                for (const simulation::SimulationTickFailure& failure : tick.Value().failures)
                {
                    record_failure(RuntimeUpdateStep::Simulation, failure.error);
                }
            }
        }

        if (services_.navigation)
        {
            record_step(RuntimeUpdateStep::Navigation);
            (void)services_.navigation->runtime->Tick(input.navigation_budget);
        }

        if (services_.animation)
        {
            record_step(RuntimeUpdateStep::Animation);
            const auto tick = services_.animation->runtime->Tick(input.real_delta, input.max_animators);
            if (!tick)
            {
                record_failure(RuntimeUpdateStep::Animation, tick.GetError());
            }
        }

        if (services_.physics)
        {
            record_step(RuntimeUpdateStep::Physics);
            const auto tick = services_.physics->stepper->Tick(input.real_delta);
            if (!tick)
            {
                record_failure(RuntimeUpdateStep::Physics, tick.GetError());
            }
        }

        if (integrations_->scene_projections)
        {
            record_step(RuntimeUpdateStep::SceneProjectionCommit);
            const auto flushed = integrations_->scene_projections->Flush();
            if (!flushed)
            {
                record_failure(RuntimeUpdateStep::SceneProjectionCommit, flushed.GetError());
            }
        }

        if (services_.audio)
        {
            record_step(RuntimeUpdateStep::Audio);
            const auto tick = services_.audio->runtime->Tick(input.real_delta);
            if (!tick)
            {
                record_failure(RuntimeUpdateStep::Audio, tick.GetError());
            }
        }

        if (services_.renderer)
        {
            record_step(RuntimeUpdateStep::Renderer);
            const auto prepared = services_.renderer->runtime->PrepareFrame();
            if (!prepared)
            {
                record_failure(RuntimeUpdateStep::Renderer, prepared.GetError());
            }
            else
            {
                const auto rendered = services_.renderer->runtime->RenderFrame();
                if (!rendered)
                {
                    record_failure(RuntimeUpdateStep::Renderer, rendered.GetError());
                }
            }
        }

        if (integrations_->event_sink)
        {
            record_step(RuntimeUpdateStep::DiagnosticsEvents);
            RuntimeFrameEvents events{};
            if (services_.physics && services_.physics->events)
            {
                const auto contacts = services_.physics->events->Contacts();
                events.physics_contacts.assign(contacts.begin(), contacts.end());
            }
            if (services_.animation && services_.animation->events)
            {
                const auto animation_events = services_.animation->events->Events();
                events.animation_events.assign(animation_events.begin(), animation_events.end());
            }
            if (services_.audio && services_.audio->events)
            {
                const auto audio_events = services_.audio->events->Events();
                events.audio_events.assign(audio_events.begin(), audio_events.end());
            }
            events.phase_failures = result.failures;

            const auto published = integrations_->event_sink->Publish(events);
            if (!published)
            {
                record_failure(RuntimeUpdateStep::DiagnosticsEvents, published.GetError());
            }
            else
            {
                if (services_.physics && services_.physics->events)
                {
                    services_.physics->events->Clear();
                }
                if (services_.animation && services_.animation->events)
                {
                    services_.animation->events->Clear();
                }
                if (services_.audio && services_.audio->events)
                {
                    services_.audio->events->Clear();
                }
            }
        }

        return foundation::Result<RuntimeTickResult>::Success(std::move(result));
    }

    [[nodiscard]] foundation::Result<void> Shutdown() override
    {
        if (shutdown_complete_)
        {
            return foundation::Result<void>::Success();
        }

        shutdown_started_ = true;
        integrations_->last_shutdown_order.clear();
        std::optional<foundation::Error> first;
        auto record_step = [&](RuntimeShutdownStep step) { integrations_->last_shutdown_order.push_back(step); };
        auto capture = [&](const foundation::Result<void>& result) -> bool {
            if (!result && !first)
            {
                first = result.GetError();
            }
            return result.HasValue();
        };

        record_step(RuntimeShutdownStep::StopNewWork);

        record_step(RuntimeShutdownStep::ResolveSimulationWork);
        bool simulation_shutdown_ok = true;
        if (services_.simulation)
        {
            switch (options_.shutdown_proposal_policy)
            {
            case ShutdownProposalPolicy::CommitValid:
            {
                const auto main_thread = services_.simulation->runtime->ProcessMainThreadCommits();
                if (!main_thread)
                {
                    simulation_shutdown_ok = false;
                    capture(foundation::Result<void>::Failure(main_thread.GetError()));
                }
                while (simulation_shutdown_ok && !services_.simulation->proposals->PendingBatches().empty())
                {
                    const auto committed = services_.simulation->proposals->CommitNext();
                    if (!capture(committed))
                    {
                        simulation_shutdown_ok = false;
                    }
                }
                break;
            }
            case ShutdownProposalPolicy::DiscardWithReason:
                simulation_shutdown_ok = capture(
                    services_.simulation->proposals->DiscardAll(simulation::ProposalDiscardReason::Shutdown));
                break;
            case ShutdownProposalPolicy::FailIfPending:
                if (!services_.simulation->proposals->PendingBatches().empty())
                {
                    simulation_shutdown_ok = false;
                    capture(FailureVoid("runtime_support.pending_simulation_proposals",
                                        "simulation proposals remain during shutdown"));
                }
                break;
            }
            const bool runtime_stopped = capture(services_.simulation->runtime->Shutdown());
            simulation_shutdown_ok = simulation_shutdown_ok && runtime_stopped;
        }

        record_step(RuntimeShutdownStep::ShutdownStreaming);
        bool streaming_shutdown_ok = true;
        if (services_.streaming && services_.streaming->controller)
        {
            streaming_shutdown_ok = capture(services_.streaming->controller->Shutdown());
        }

        record_step(RuntimeShutdownStep::ShutdownAudio);
        bool audio_shutdown_ok = true;
        if (services_.audio)
        {
            audio_shutdown_ok = capture(services_.audio->runtime->Shutdown());
        }

        record_step(RuntimeShutdownStep::ShutdownPhysics);
        bool physics_shutdown_ok = true;
        if (services_.physics && services_.physics->lifecycle)
        {
            physics_shutdown_ok = capture(services_.physics->lifecycle->Shutdown());
        }

        record_step(RuntimeShutdownStep::FlushSceneProjections);
        bool scene_projection_ok = true;
        if (integrations_->scene_projections)
        {
            const auto flushed = integrations_->scene_projections->Flush();
            if (!flushed)
            {
                scene_projection_ok = false;
                capture(foundation::Result<void>::Failure(flushed.GetError()));
            }
            if (scene_projection_ok)
            {
                scene_projection_ok = capture(
                    std::dynamic_pointer_cast<IRuntimeAdapterLifecycle>(integrations_->scene_projections)
                        ? std::dynamic_pointer_cast<IRuntimeAdapterLifecycle>(integrations_->scene_projections)->Shutdown()
                        : foundation::Result<void>::Success());
            }
        }

        record_step(RuntimeShutdownStep::ReleaseAnimationResources);
        bool animation_resources_ok = ShutdownAdapter(integrations_->animation_resources, capture);
        const bool animation_poses_ok = ShutdownAdapter(integrations_->animation_pose_sink, capture);
        animation_resources_ok = animation_resources_ok && animation_poses_ok;

        record_step(RuntimeShutdownStep::ShutdownRenderer);
        bool renderer_shutdown_ok = true;
        if (services_.renderer)
        {
            renderer_shutdown_ok = capture(services_.renderer->runtime->Shutdown());
        }

        record_step(RuntimeShutdownStep::ReleaseResourceAdapters);
        bool resource_adapters_ok = true;
        if (renderer_shutdown_ok)
        {
            resource_adapters_ok = ShutdownAdapter(integrations_->render_resources, capture) && resource_adapters_ok;
        }
        else
        {
            resource_adapters_ok = false;
        }
        if (audio_shutdown_ok)
        {
            resource_adapters_ok = ShutdownAdapter(integrations_->audio_resources, capture) && resource_adapters_ok;
        }
        else
        {
            resource_adapters_ok = false;
        }
        if (streaming_shutdown_ok)
        {
            resource_adapters_ok = ShutdownAdapter(integrations_->streaming_data_source, capture) && resource_adapters_ok;
        }
        else
        {
            resource_adapters_ok = false;
        }

        record_step(RuntimeShutdownStep::ClearNavigationAndEvents);
        if (physics_shutdown_ok && services_.physics && services_.physics->events)
        {
            services_.physics->events->Clear();
        }
        if (animation_resources_ok && services_.animation && services_.animation->events)
        {
            services_.animation->events->Clear();
        }
        if (audio_shutdown_ok && services_.audio && services_.audio->events)
        {
            services_.audio->events->Clear();
        }

        const bool all_cleanup_complete = simulation_shutdown_ok && streaming_shutdown_ok &&
                                          audio_shutdown_ok && physics_shutdown_ok &&
                                          scene_projection_ok && animation_resources_ok &&
                                          renderer_shutdown_ok && resource_adapters_ok;
        if (first || !all_cleanup_complete)
        {
            return first ? foundation::Result<void>::Failure(*first)
                         : FailureVoid("runtime_support.shutdown_incomplete",
                                       "runtime shutdown has unfinished cleanup");
        }

        record_step(RuntimeShutdownStep::DestroyAdapters);
        integrations_->owned_adapters.clear();
        integrations_->render_resources.reset();
        integrations_->render_scene.reset();
        integrations_->render_pose_source.reset();
        integrations_->render_commands.reset();
        integrations_->physics_transform_source.reset();
        integrations_->physics_transform_sink.reset();
        integrations_->audio_transforms.reset();
        integrations_->scene_projections.reset();
        integrations_->animation_resources.reset();
        integrations_->animation_pose_sink.reset();
        integrations_->animation_evaluator.reset();
        integrations_->animation_pose_cache.reset();
        integrations_->audio_backend.reset();
        integrations_->audio_resources.reset();
        integrations_->streaming_data_source.reset();
        integrations_->streaming_commit_target.reset();
        integrations_->streaming_priority_provider.reset();
        integrations_->streaming_residency_controller.reset();
        integrations_->streaming_world_source.reset();
        integrations_->streaming_persistence_source.reset();
        integrations_->streaming_resource_source.reset();
        integrations_->streaming_prepared_data.reset();
        integrations_->simulation_clock.reset();
        integrations_->simulation_commit_target.reset();
        integrations_->simulation_commit_log.reset();
        integrations_->navigation_data_source.reset();
        integrations_->navigation_costs.reset();
        integrations_->navigation_obstacles.reset();
        integrations_->event_sink.reset();

        record_step(RuntimeShutdownStep::Complete);
        shutdown_complete_ = true;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] bool IsShutdownStarted() const noexcept override { return shutdown_started_; }
    [[nodiscard]] bool IsShutdownComplete() const noexcept override { return shutdown_complete_; }

  private:
    template <typename TInterface, typename TCapture>
    static bool ShutdownAdapter(const std::shared_ptr<TInterface>& interface_ptr, TCapture&& capture)
    {
        if (!interface_ptr)
        {
            return true;
        }
        const auto lifecycle = std::dynamic_pointer_cast<IRuntimeAdapterLifecycle>(interface_ptr);
        return lifecycle ? capture(lifecycle->Shutdown()) : true;
    }

    RuntimeServiceSnapshot services_;
    std::shared_ptr<RuntimeIntegrationServices> integrations_;
    EngineRuntimeOptions options_;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};

[[nodiscard]] foundation::Result<void> ValidateOptionDependencies(const EngineRuntimeOptions& options)
{
    if (options.enable_streaming && (!options.enable_world || !options.enable_resources || !options.enable_persistence))
    {
        return FailureVoid("runtime_support.missing_dependency",
                           "Streaming requires World, Resources and Persistence");
    }
    if (options.enable_simulation && !options.enable_time)
    {
        return FailureVoid("runtime_support.missing_dependency", "Simulation requires Time");
    }
    if (options.enable_navigation && !options.enable_environment)
    {
        return FailureVoid("runtime_support.missing_dependency", "Navigation requires Environment");
    }
    if (options.enable_animation && !options.enable_resources)
    {
        return FailureVoid("runtime_support.missing_dependency", "Animation requires Resources");
    }
    if (options.enable_physics && !options.enable_scene)
    {
        return FailureVoid("runtime_support.missing_dependency", "Physics requires Scene");
    }
    if (options.enable_audio && (!options.enable_resources || !options.enable_scene))
    {
        return FailureVoid("runtime_support.missing_dependency", "Audio requires Resources and Scene");
    }
    if (options.enable_renderer && (!options.enable_resources || !options.enable_scene))
    {
        return FailureVoid("runtime_support.missing_dependency", "Renderer requires Resources and Scene");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] bool HasAnyStreamingCoreDependency(const streaming::StreamingDependencies& dependencies)
{
    return dependencies.data_source || dependencies.commit_target || dependencies.residency_controller ||
           dependencies.world_source || dependencies.persistence_source || dependencies.resource_source;
}

[[nodiscard]] bool HasAllStreamingCoreDependencies(const streaming::StreamingDependencies& dependencies)
{
    return dependencies.data_source && dependencies.commit_target && dependencies.residency_controller &&
           dependencies.world_source && dependencies.persistence_source && dependencies.resource_source;
}

[[nodiscard]] foundation::Result<void>
    ValidateProfileDependencies(const EngineRuntimeOptions& options,
                                const EngineRuntimeDependencies& dependencies)
{
    if (HasAnyStreamingCoreDependency(dependencies.streaming) &&
        !HasAllStreamingCoreDependencies(dependencies.streaming))
    {
        return FailureVoid("runtime_support.incomplete_streaming_dependencies",
                           "Streaming core integration roles must be supplied together");
    }

    if (options.profile != RuntimeProfile::Production)
    {
        return foundation::Result<void>::Success();
    }

    if (options.enable_renderer && !dependencies.renderer.command_sink)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Renderer requires an external command sink");
    }
    if (options.enable_physics && !dependencies.physics.backend)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Physics requires an external backend");
    }
    if (options.enable_navigation && !dependencies.navigation.backend)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Navigation requires an external backend");
    }
    if (options.enable_animation && !dependencies.animation.evaluator)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Animation requires an evaluator backend");
    }
    if (options.enable_audio && !dependencies.audio.backend)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Audio requires an external backend");
    }
    if (options.enable_streaming && !HasAllStreamingCoreDependencies(dependencies.streaming) &&
        !dependencies.chunk_manifests)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Streaming standard composition requires a chunk manifest source");
    }
    if (options.enable_simulation && !dependencies.simulation.commit_target)
    {
        return FailureVoid("runtime_support.production_dependency_missing",
                           "Production Simulation requires a commit target");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateApplicationIsClean(core::Application& app)
{
#define EPIDEMIC_CHECK_ABSENT(type, name) \
    if (app.Services().Contains<type>()) \
    { \
        return FailureVoid("runtime_support.duplicate_registration", name " is already registered"); \
    }

    EPIDEMIC_CHECK_ABSENT(EngineRuntimeServices, "EngineRuntime");
    EPIDEMIC_CHECK_ABSENT(RuntimeFoundationRegistration, "RuntimeFoundation");
    EPIDEMIC_CHECK_ABSENT(AssetServices, "Assets");
    EPIDEMIC_CHECK_ABSENT(SerializationServices, "Serialization");
    EPIDEMIC_CHECK_ABSENT(ResourceServices, "Resources");
    EPIDEMIC_CHECK_ABSENT(PersistenceServices, "Persistence");
    EPIDEMIC_CHECK_ABSENT(TimeServices, "Time");
    EPIDEMIC_CHECK_ABSENT(EnvironmentServices, "Environment");
    EPIDEMIC_CHECK_ABSENT(SceneServices, "Scene");
    EPIDEMIC_CHECK_ABSENT(WorldServices, "World");
    EPIDEMIC_CHECK_ABSENT(streaming::StreamingServices, "Streaming");
    EPIDEMIC_CHECK_ABSENT(simulation::SimulationServices, "Simulation");
    EPIDEMIC_CHECK_ABSENT(navigation::NavigationServices, "Navigation");
    EPIDEMIC_CHECK_ABSENT(animation::AnimationServices, "Animation");
    EPIDEMIC_CHECK_ABSENT(physics::PhysicsServices, "Physics");
    EPIDEMIC_CHECK_ABSENT(audio::AudioServices, "Audio");
    EPIDEMIC_CHECK_ABSENT(renderer::RendererServices, "Renderer");
#undef EPIDEMIC_CHECK_ABSENT
    return foundation::Result<void>::Success();
}

void AddAdapter(RuntimeIntegrationServices& integrations, RuntimeAdapterKind kind)
{
    if (std::find(integrations.owned_adapters.begin(), integrations.owned_adapters.end(), kind) ==
        integrations.owned_adapters.end())
    {
        integrations.owned_adapters.push_back(kind);
    }
}

[[nodiscard]] RuntimeServiceSnapshot MakeSnapshot(const EngineRuntimeServices& services)
{
    RuntimeServiceSnapshot snapshot{};
    snapshot.time = services.time;
    snapshot.resources = services.resources;
    snapshot.persistence = services.persistence;
    snapshot.environment = services.environment;
    snapshot.scene = services.scene;
    snapshot.world = services.world;
    snapshot.streaming = services.streaming;
    snapshot.simulation = services.simulation;
    snapshot.navigation = services.navigation;
    snapshot.animation = services.animation;
    snapshot.physics = services.physics;
    snapshot.audio = services.audio;
    snapshot.renderer = services.renderer;
    return snapshot;
}

[[nodiscard]] foundation::Result<void>
    EnsureMainView(renderer::RendererServices& renderer_services, const std::shared_ptr<SceneServices>& scene)
{
    if (renderer_services.views->GetMainView().IsValid())
    {
        return foundation::Result<void>::Success();
    }
    foundation::Result<SceneNodeId> node = foundation::Result<SceneNodeId>::Failure(
        foundation::Error::Create("runtime_support.main_view_creation_failed", "main-view node creation did not run"));
    try
    {
        node = scene->nodes->CreateNode();
    }
    catch (const std::exception& exception)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.scene_exception",
                                      "scene threw while creating the main-view node",
                                      exception.what()));
    }
    catch (...)
    {
        return FailureVoid("runtime_support.scene_exception",
                           "scene threw while creating the main-view node");
    }
    if (!node)
    {
        return foundation::Result<void>::Failure(node.GetError());
    }

    auto rollback_node = [&]() -> foundation::Result<void> {
        try
        {
            return scene->nodes->DestroyNode(node.Value());
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("runtime_support.scene_exception",
                                          "scene threw while rolling back the main-view node",
                                          exception.what()));
        }
        catch (...)
        {
            return FailureVoid("runtime_support.scene_exception",
                               "scene threw while rolling back the main-view node");
        }
    };

    foundation::Result<renderer::ViewId> view = foundation::Result<renderer::ViewId>::Failure(
        foundation::Error::Create("runtime_support.main_view_creation_failed", "main view creation did not run"));
    if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_main_view_creation))
    {
        const auto cleanup = rollback_node();
        return cleanup ? FailureVoid("runtime_support.injected_view_creation_failure",
                                     "injected main-view creation failure")
                       : FailureVoid("runtime_support.main_view_cleanup_pending",
                                     "injected view creation failure and scene-node rollback failed");
    }
    try
    {
        view = renderer_services.views->CreateView(
            renderer::ViewDesc{RuntimeObjectId{node.Value().Raw()}});
    }
    catch (const std::exception& exception)
    {
        const auto cleanup = rollback_node();
        if (!cleanup)
        {
            return FailureVoid("runtime_support.main_view_cleanup_pending",
                               "view creation threw and scene-node rollback failed");
        }
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.renderer_exception",
                                      "renderer threw while creating the main view",
                                      exception.what()));
    }
    catch (...)
    {
        const auto cleanup = rollback_node();
        if (!cleanup)
        {
            return FailureVoid("runtime_support.main_view_cleanup_pending",
                               "view creation threw and scene-node rollback failed");
        }
        return FailureVoid("runtime_support.renderer_exception",
                           "renderer threw while creating the main view");
    }
    if (!view)
    {
        const auto cleanup = rollback_node();
        return cleanup ? foundation::Result<void>::Failure(view.GetError())
                       : FailureVoid("runtime_support.main_view_cleanup_pending",
                                     "view creation failed and scene-node rollback failed");
    }

    auto rollback_view_and_node = [&]() -> foundation::Result<void> {
        std::optional<foundation::Error> first;
        try
        {
            const auto destroyed = renderer_services.views->DestroyView(view.Value());
            if (!destroyed)
            {
                first = destroyed.GetError();
            }
        }
        catch (const std::exception& exception)
        {
            first = foundation::Error::Create("runtime_support.renderer_exception",
                                              "renderer threw while rolling back the main view",
                                              exception.what());
        }
        catch (...)
        {
            first = foundation::Error::Create("runtime_support.renderer_exception",
                                              "renderer threw while rolling back the main view");
        }

        const auto node_cleanup = rollback_node();
        if (!node_cleanup && !first)
        {
            first = node_cleanup.GetError();
        }
        return first ? foundation::Result<void>::Failure(*first) : foundation::Result<void>::Success();
    };

    foundation::Result<void> published = foundation::Result<void>::Failure(
        foundation::Error::Create("runtime_support.main_view_creation_failed", "main-view publication did not run"));
    if (ConsumeTestFault(g_runtime_support_test_faults.fail_next_main_view_publication))
    {
        const auto cleanup = rollback_view_and_node();
        return cleanup ? FailureVoid("runtime_support.injected_main_view_publication_failure",
                                     "injected main-view publication failure")
                       : FailureVoid("runtime_support.main_view_cleanup_pending",
                                     "injected main-view publication failure and rollback failed");
    }
    try
    {
        published = renderer_services.views->SetMainView(view.Value());
    }
    catch (const std::exception& exception)
    {
        const auto cleanup = rollback_view_and_node();
        if (!cleanup)
        {
            return FailureVoid("runtime_support.main_view_cleanup_pending",
                               "main-view publication threw and rollback failed");
        }
        return foundation::Result<void>::Failure(
            foundation::Error::Create("runtime_support.renderer_exception",
                                      "renderer threw while publishing the main view",
                                      exception.what()));
    }
    catch (...)
    {
        const auto cleanup = rollback_view_and_node();
        if (!cleanup)
        {
            return FailureVoid("runtime_support.main_view_cleanup_pending",
                               "main-view publication threw and rollback failed");
        }
        return FailureVoid("runtime_support.renderer_exception",
                           "renderer threw while publishing the main view");
    }

    if (!published)
    {
        const auto cleanup = rollback_view_and_node();
        return cleanup ? published
                       : FailureVoid("runtime_support.main_view_cleanup_pending",
                                     "main-view publication failed and rollback did not complete");
    }
    return foundation::Result<void>::Success();
}

} // namespace

namespace support_testing
{
std::shared_ptr<renderer::IRenderResourceBridge>
CreateRenderResourceBridge(std::shared_ptr<IResourceManager> manager)
{
    return std::make_shared<RuntimeRenderResourceBridge>(std::move(manager));
}

std::shared_ptr<streaming::IStreamingDataSource>
CreateStreamingAdapter(std::shared_ptr<WorldServices> world,
                       std::shared_ptr<ResourceServices> resources,
                       std::shared_ptr<PersistenceServices> persistence,
                       std::shared_ptr<IChunkStreamingManifestSource> manifests)
{
    return std::make_shared<RuntimeStreamingAdapter>(std::move(world),
                                                     std::move(resources),
                                                     std::move(persistence),
                                                     std::move(manifests));
}

void FailNextRenderLeasePublication() noexcept
{
    g_runtime_support_test_faults.fail_next_render_lease_publication = true;
}

void FailStreamingLeasePublicationAfter(std::size_t successful_publications) noexcept
{
    g_runtime_support_test_faults.fail_streaming_lease_publication_after = successful_publications;
}

void FailNextStreamingLeasePublication() noexcept
{
    FailStreamingLeasePublicationAfter(0);
}

void FailNextStreamingPlanConstruction() noexcept
{
    g_runtime_support_test_faults.fail_next_streaming_plan_construction = true;
}

void FailNextPersistenceException() noexcept
{
    g_runtime_support_test_faults.fail_next_persistence_exception = true;
}

void FailNextStreamingCleanupWorldTransition() noexcept
{
    g_runtime_support_test_faults.fail_next_streaming_cleanup_world_transition = true;
}

void FailNextMainViewCreation() noexcept
{
    g_runtime_support_test_faults.fail_next_main_view_creation = true;
}

void FailNextMainViewPublication() noexcept
{
    g_runtime_support_test_faults.fail_next_main_view_publication = true;
}

foundation::Result<void> EnsureMainView(renderer::RendererServices& renderer_services,
                                        const std::shared_ptr<SceneServices>& scene)
{
    return ::epidemic::runtime::EnsureMainView(renderer_services, scene);
}

foundation::Result<void> SetPreparedRevision(streaming::IStreamingDataSource& source,
                                             std::uint64_t request_id,
                                             std::uint64_t revision)
{
    auto* adapter = dynamic_cast<RuntimeStreamingAdapter*>(&source);
    if (!adapter)
    {
        return FailureVoid("runtime_support.test_invalid_adapter", "streaming test adapter type mismatch");
    }
    return adapter->SetPreparedRevisionForTesting(request_id, revision);
}
} // namespace support_testing

foundation::Result<PreparedEngineRuntime> PrepareEngineRuntime(const EngineRuntimeOptions& options,
                                                               EngineRuntimeDependencies dependencies)
{
    if (const auto valid = ValidateOptionDependencies(options); !valid)
    {
        return foundation::Result<PreparedEngineRuntime>::Failure(valid.GetError());
    }
    if (const auto valid = ValidateProfileDependencies(options, dependencies); !valid)
    {
        return foundation::Result<PreparedEngineRuntime>::Failure(valid.GetError());
    }

    EngineRuntimeServices services{};
    services.profile = options.profile;
    services.foundation = std::make_shared<RuntimeFoundationRegistration>();
    services.registered_majors.push_back("RuntimeFoundation");
    auto integrations = std::make_shared<RuntimeIntegrationServices>();

    auto add_major = [&](std::string_view name) { services.registered_majors.emplace_back(name); };

#define EPIDEMIC_CREATE_MAJOR(enabled, member, type, name, expression) \
    if (enabled) \
    { \
        const auto created = (expression); \
        if (!created) \
        { \
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError()); \
        } \
        services.member = std::make_shared<type>(created.Value()); \
        add_major(name); \
    }

    EPIDEMIC_CREATE_MAJOR(options.enable_assets,
                          assets,
                          AssetServices,
                          "Assets",
                          CreateAssetServices(options.assets));
    EPIDEMIC_CREATE_MAJOR(options.enable_serialization,
                          serialization,
                          SerializationServices,
                          "Serialization",
                          CreateSerializationServices(options.serialization));
    EPIDEMIC_CREATE_MAJOR(options.enable_resources,
                          resources,
                          ResourceServices,
                          "Resources",
                          CreateResourceServices(options.resources));
    EPIDEMIC_CREATE_MAJOR(options.enable_persistence,
                          persistence,
                          PersistenceServices,
                          "Persistence",
                          CreatePersistenceServices(options.persistence));
    EPIDEMIC_CREATE_MAJOR(options.enable_time,
                          time,
                          TimeServices,
                          "Time",
                          CreateTimeServices(options.time));
    EPIDEMIC_CREATE_MAJOR(options.enable_environment,
                          environment,
                          EnvironmentServices,
                          "Environment",
                          CreateEnvironmentServices(options.environment));
    EPIDEMIC_CREATE_MAJOR(options.enable_scene,
                          scene,
                          SceneServices,
                          "Scene",
                          CreateSceneServices(options.scene));
    EPIDEMIC_CREATE_MAJOR(options.enable_world,
                          world,
                          WorldServices,
                          "World",
                          CreateWorldServices(options.world));

#undef EPIDEMIC_CREATE_MAJOR

    std::shared_ptr<RuntimeSceneTransformAdapter> scene_transform_adapter;
    std::shared_ptr<RuntimeAnimationPoseBridge> animation_pose_bridge;
    if (services.scene && (options.enable_physics || options.enable_audio))
    {
        scene_transform_adapter =
            std::make_shared<RuntimeSceneTransformAdapter>(services.scene->transforms);
        integrations->scene_projections = scene_transform_adapter;
    }

    if (options.enable_streaming)
    {
        streaming::StreamingDependencies streaming_dependencies = dependencies.streaming;
        std::shared_ptr<RuntimeStreamingAdapter> support_streaming;
        if (!HasAllStreamingCoreDependencies(streaming_dependencies))
        {
            std::shared_ptr<IChunkStreamingManifestSource> manifests = dependencies.chunk_manifests;
            if (!manifests)
            {
                if (options.profile == RuntimeProfile::Production)
                {
                    return foundation::Result<PreparedEngineRuntime>::Failure(
                        SupportError("runtime_support.production_dependency_missing",
                                     "Production Streaming standard composition requires a chunk manifest source"));
                }
                manifests = std::make_shared<ReferenceChunkStreamingManifestSource>(services.world->chunks);
            }
            support_streaming = std::make_shared<RuntimeStreamingAdapter>(
                services.world,
                services.resources,
                services.persistence,
                std::move(manifests));
            streaming_dependencies.data_source = support_streaming;
            streaming_dependencies.commit_target = support_streaming;
            streaming_dependencies.residency_controller = support_streaming;
            streaming_dependencies.world_source = support_streaming;
            streaming_dependencies.persistence_source = support_streaming;
            streaming_dependencies.resource_source = support_streaming;
        }
        if (!streaming_dependencies.priority_provider)
        {
            if (support_streaming)
            {
                streaming_dependencies.priority_provider = support_streaming;
            }
            else
            {
                return foundation::Result<PreparedEngineRuntime>::Failure(
                    SupportError("runtime_support.streaming_priority_missing",
                                 "external Streaming composition requires a priority provider"));
            }
        }
        const auto created = streaming::CreateStreamingServices(streaming_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.streaming = std::make_shared<streaming::StreamingServices>(created.Value());
        services.streaming->runtime->SetBudget(options.streaming);
        integrations->streaming_data_source = streaming_dependencies.data_source;
        integrations->streaming_commit_target = streaming_dependencies.commit_target;
        integrations->streaming_priority_provider = streaming_dependencies.priority_provider;
        integrations->streaming_residency_controller = streaming_dependencies.residency_controller;
        integrations->streaming_world_source = streaming_dependencies.world_source;
        integrations->streaming_persistence_source = streaming_dependencies.persistence_source;
        integrations->streaming_resource_source = streaming_dependencies.resource_source;
        if (support_streaming)
        {
            integrations->streaming_prepared_data = support_streaming;
            AddAdapter(*integrations, RuntimeAdapterKind::WorldResourcesPersistenceToStreaming);
        }
        add_major("Streaming");
    }

    if (options.enable_simulation)
    {
        simulation::SimulationDependencies simulation_dependencies = dependencies.simulation;
        if (!simulation_dependencies.clock)
        {
            auto clock = std::make_shared<RuntimeSimulationClockAdapter>(services.time->clock);
            simulation_dependencies.clock = clock;
            integrations->simulation_clock = clock;
            AddAdapter(*integrations, RuntimeAdapterKind::TimeToSimulation);
        }
        else
        {
            integrations->simulation_clock = simulation_dependencies.clock;
        }

        if (!simulation_dependencies.commit_target)
        {
            auto target = std::make_shared<RuntimeSimulationCommitTarget>();
            simulation_dependencies.commit_target = target;
            integrations->simulation_commit_target = target;
            integrations->simulation_commit_log = target;
            AddAdapter(*integrations, RuntimeAdapterKind::SimulationToDomain);
        }
        else
        {
            integrations->simulation_commit_target = simulation_dependencies.commit_target;
        }

        const auto created = simulation::CreateSimulationServices(options.simulation, simulation_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.simulation = std::make_shared<simulation::SimulationServices>(created.Value());
        add_major("Simulation");
    }

    if (options.enable_navigation)
    {
        navigation::NavigationDependencies navigation_dependencies = dependencies.navigation;
        std::shared_ptr<RuntimeNavigationEnvironmentAdapter> environment_adapter;
        if (!navigation_dependencies.data_source || !navigation_dependencies.cost_provider ||
            !navigation_dependencies.obstacle_source)
        {
            environment_adapter =
                std::make_shared<RuntimeNavigationEnvironmentAdapter>(services.environment);
        }
        if (!navigation_dependencies.data_source)
        {
            navigation_dependencies.data_source = environment_adapter;
        }
        if (!navigation_dependencies.cost_provider)
        {
            navigation_dependencies.cost_provider = environment_adapter;
        }
        if (!navigation_dependencies.obstacle_source)
        {
            navigation_dependencies.obstacle_source = environment_adapter;
        }

        navigation::NavigationOptions navigation_options = options.navigation;
        if (options.profile != RuntimeProfile::Production && !navigation_dependencies.backend)
        {
            navigation_options.enable_mock_queries = true;
        }
        const auto created = navigation::CreateNavigationServices(navigation_options, navigation_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.navigation = std::make_shared<navigation::NavigationServices>(created.Value());
        integrations->navigation_data_source = navigation_dependencies.data_source;
        integrations->navigation_costs = navigation_dependencies.cost_provider;
        integrations->navigation_obstacles = navigation_dependencies.obstacle_source;
        if (environment_adapter)
        {
            AddAdapter(*integrations, RuntimeAdapterKind::EnvironmentToNavigation);
        }
        add_major("Navigation");
    }

    if (options.enable_animation)
    {
        animation::AnimationDependencies animation_dependencies = dependencies.animation;
        if (!animation_dependencies.resources)
        {
            std::shared_ptr<IAnimationResourceMapper> mapper = dependencies.animation_resource_mapper;
            if (!mapper)
            {
                mapper = std::make_shared<DefaultAnimationResourceMapper>();
            }
            auto source = std::make_shared<RuntimeAnimationResourceSource>(
                services.resources->manager,
                std::move(mapper));
            animation_dependencies.resources = source;
            integrations->animation_resources = source;
            AddAdapter(*integrations, RuntimeAdapterKind::ResourcesToAnimation);
        }
        else
        {
            integrations->animation_resources = animation_dependencies.resources;
        }

        if (!animation_dependencies.pose_sink)
        {
            animation_pose_bridge = std::make_shared<RuntimeAnimationPoseBridge>();
            animation_dependencies.pose_sink = animation_pose_bridge;
            integrations->animation_pose_sink = animation_pose_bridge;
            integrations->animation_pose_cache = animation_pose_bridge;
            integrations->render_pose_source = animation_pose_bridge;
            AddAdapter(*integrations, RuntimeAdapterKind::AnimationToRenderer);
        }
        else
        {
            integrations->animation_pose_sink = animation_dependencies.pose_sink;
        }

        if (!animation_dependencies.evaluator)
        {
            auto evaluator = std::make_shared<RuntimeAnimationEvaluator>();
            animation_dependencies.evaluator = evaluator;
            integrations->animation_evaluator = evaluator;
        }
        else
        {
            integrations->animation_evaluator = animation_dependencies.evaluator;
        }

        animation::AnimationOptions animation_options = options.animation;
        animation_options.enable_mock_pose_evaluation = false;
        const auto created = animation::CreateAnimationServices(animation_options, animation_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.animation = std::make_shared<animation::AnimationServices>(created.Value());
        add_major("Animation");
    }

    if (options.enable_physics)
    {
        physics::PhysicsDependencies physics_dependencies = dependencies.physics;
        physics_dependencies.options = options.physics;
        if (!physics_dependencies.transform_source)
        {
            physics_dependencies.transform_source = scene_transform_adapter;
        }
        if (!physics_dependencies.transform_sink)
        {
            physics_dependencies.transform_sink = scene_transform_adapter;
        }
        const auto created = physics::CreatePhysicsServices(physics_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.physics = std::make_shared<physics::PhysicsServices>(created.Value());
        integrations->physics_transform_source = physics_dependencies.transform_source;
        integrations->physics_transform_sink = physics_dependencies.transform_sink;
        if (scene_transform_adapter)
        {
            AddAdapter(*integrations, RuntimeAdapterKind::SceneToPhysics);
        }
        add_major("Physics");
    }

    if (options.enable_audio)
    {
        audio::AudioDependencies audio_dependencies = dependencies.audio;
        if (!audio_dependencies.backend)
        {
            auto backend = std::make_shared<ReferenceAudioBackend>();
            audio_dependencies.backend = backend;
            integrations->audio_backend = backend;
        }
        else
        {
            integrations->audio_backend = audio_dependencies.backend;
        }

        if (!audio_dependencies.resources)
        {
            std::shared_ptr<IAudioResourceMapper> mapper = dependencies.audio_resource_mapper;
            if (!mapper)
            {
                mapper = std::make_shared<DefaultAudioResourceMapper>();
            }
            auto source = std::make_shared<RuntimeAudioResourceSource>(
                services.resources->manager,
                std::move(mapper));
            audio_dependencies.resources = source;
            integrations->audio_resources = source;
            AddAdapter(*integrations, RuntimeAdapterKind::ResourcesToAudio);
        }
        else
        {
            integrations->audio_resources = audio_dependencies.resources;
        }

        if (!audio_dependencies.transforms)
        {
            audio_dependencies.transforms = scene_transform_adapter;
        }
        integrations->audio_transforms = audio_dependencies.transforms;
        if (scene_transform_adapter)
        {
            AddAdapter(*integrations, RuntimeAdapterKind::SceneToAudio);
        }

        const auto created = audio::CreateAudioServices(options.audio, audio_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        services.audio = std::make_shared<audio::AudioServices>(created.Value());
        add_major("Audio");
    }

    if (options.enable_renderer)
    {
        renderer::RendererDependencies renderer_dependencies = dependencies.renderer;
        if (!renderer_dependencies.resource_bridge)
        {
            auto bridge = std::make_shared<RuntimeRenderResourceBridge>(services.resources->manager);
            renderer_dependencies.resource_bridge = bridge;
            integrations->render_resources = bridge;
            AddAdapter(*integrations, RuntimeAdapterKind::ResourcesToRenderer);
        }
        else
        {
            integrations->render_resources = renderer_dependencies.resource_bridge;
        }

        if (!renderer_dependencies.scene_source)
        {
            auto source = std::make_shared<RuntimeRenderSceneSource>(
                services.scene->nodes,
                services.scene->transforms);
            renderer_dependencies.scene_source = source;
            integrations->render_scene = source;
            AddAdapter(*integrations, RuntimeAdapterKind::SceneToRenderer);
        }
        else
        {
            integrations->render_scene = renderer_dependencies.scene_source;
        }

        if (!renderer_dependencies.pose_source && animation_pose_bridge)
        {
            renderer_dependencies.pose_source = animation_pose_bridge;
            integrations->render_pose_source = animation_pose_bridge;
            AddAdapter(*integrations, RuntimeAdapterKind::AnimationToRenderer);
        }
        else if (renderer_dependencies.pose_source)
        {
            integrations->render_pose_source = renderer_dependencies.pose_source;
        }

        if (!renderer_dependencies.command_sink)
        {
            renderer_dependencies.command_sink = std::make_shared<ReferenceRenderCommandSink>();
        }
        integrations->render_commands = renderer_dependencies.command_sink;

        const auto created = renderer::CreateRendererServices(options.renderer, renderer_dependencies);
        if (!created)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(created.GetError());
        }
        renderer::RendererServices renderer_services = created.Value();
        if (const auto main_view = EnsureMainView(renderer_services, services.scene); !main_view)
        {
            return foundation::Result<PreparedEngineRuntime>::Failure(main_view.GetError());
        }
        services.renderer = std::make_shared<renderer::RendererServices>(std::move(renderer_services));
        add_major("Renderer");
    }

    integrations->event_sink = dependencies.event_sink;
    if (!integrations->event_sink && options.profile != RuntimeProfile::Production)
    {
        integrations->event_sink = std::make_shared<InMemoryRuntimeEventSink>();
    }

    services.integrations = integrations;
    services.coordinator = std::make_shared<EngineRuntimeCoordinator>(
        MakeSnapshot(services),
        integrations,
        options);

    return foundation::Result<PreparedEngineRuntime>::Success(
        PreparedEngineRuntime{std::move(services)});
}

foundation::Result<EngineRuntimeServices> CommitPreparedRuntime(core::Application& app,
                                                                PreparedEngineRuntime prepared)
{
    if (const auto clean = ValidateApplicationIsClean(app); !clean)
    {
        return foundation::Result<EngineRuntimeServices>::Failure(clean.GetError());
    }
    auto aggregate = std::make_shared<EngineRuntimeServices>(prepared.services);
    const auto registered = RegisterShared(app, aggregate, "EngineRuntime");
    if (!registered)
    {
        return foundation::Result<EngineRuntimeServices>::Failure(registered.GetError());
    }
    return foundation::Result<EngineRuntimeServices>::Success(std::move(prepared.services));
}

foundation::Result<EngineRuntimeServices> RegisterDefaultEngineRuntime(
    core::Application& app,
    const EngineRuntimeOptions& options,
    EngineRuntimeDependencies dependencies)
{
    if (const auto clean = ValidateApplicationIsClean(app); !clean)
    {
        return foundation::Result<EngineRuntimeServices>::Failure(clean.GetError());
    }
    const auto prepared = PrepareEngineRuntime(options, std::move(dependencies));
    if (!prepared)
    {
        return foundation::Result<EngineRuntimeServices>::Failure(prepared.GetError());
    }
    return CommitPreparedRuntime(app, prepared.Value());
}

foundation::Result<void> RegisterRuntimeFoundation(core::Application& app)
{
    return RegisterShared(app, std::make_shared<RuntimeFoundationRegistration>(), "RuntimeFoundation");
}

foundation::Result<void> RegisterAssets(core::Application& app, const AssetsOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateAssetServices(options);
    return created ? RegisterShared(app, std::make_shared<AssetServices>(created.Value()), "Assets")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterSerialization(core::Application& app,
                                               const SerializationOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateSerializationServices(options);
    return created ? RegisterShared(app, std::make_shared<SerializationServices>(created.Value()), "Serialization")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterResources(core::Application& app, const ResourceOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateResourceServices(options);
    return created ? RegisterShared(app, std::make_shared<ResourceServices>(created.Value()), "Resources")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterPersistence(core::Application& app, const PersistenceOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreatePersistenceServices(options);
    return created ? RegisterShared(app, std::make_shared<PersistenceServices>(created.Value()), "Persistence")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterTime(core::Application& app, const TimeOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateTimeServices(options);
    return created ? RegisterShared(app, std::make_shared<TimeServices>(created.Value()), "Time")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterEnvironment(core::Application& app,
                                             const EnvironmentOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateEnvironmentServices(options);
    return created ? RegisterShared(app, std::make_shared<EnvironmentServices>(created.Value()), "Environment")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterScene(core::Application& app, const SceneOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateSceneServices(options);
    return created ? RegisterShared(app, std::make_shared<SceneServices>(created.Value()), "Scene")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterWorld(core::Application& app, const WorldOptions& options)
{
    if (const auto required = Require<RuntimeFoundationRegistration>(app, "RuntimeFoundation"); !required)
    {
        return required;
    }
    const auto created = CreateWorldServices(options);
    return created ? RegisterShared(app, std::make_shared<WorldServices>(created.Value()), "World")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterStreaming(core::Application& app)
{
    if (const auto required = Require<WorldServices>(app, "World"); !required)
    {
        return required;
    }
    if (const auto required = Require<ResourceServices>(app, "Resources"); !required)
    {
        return required;
    }
    if (const auto required = Require<PersistenceServices>(app, "Persistence"); !required)
    {
        return required;
    }
    auto world = app.Services().Get<WorldServices>();
    auto resources = app.Services().Get<ResourceServices>();
    auto persistence = app.Services().Get<PersistenceServices>();
    auto manifests = std::make_shared<ReferenceChunkStreamingManifestSource>(world->chunks);
    auto adapter = std::make_shared<RuntimeStreamingAdapter>(world, resources, persistence, manifests);
    streaming::StreamingDependencies dependencies{};
    dependencies.data_source = adapter;
    dependencies.commit_target = adapter;
    dependencies.priority_provider = adapter;
    dependencies.residency_controller = adapter;
    dependencies.world_source = adapter;
    dependencies.persistence_source = adapter;
    dependencies.resource_source = adapter;
    const auto created = streaming::CreateStreamingServices(dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<streaming::StreamingServices>(created.Value()),
                                    "Streaming")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterSimulation(core::Application& app,
                                            const simulation::SimulationOptions& options)
{
    if (const auto required = Require<TimeServices>(app, "Time"); !required)
    {
        return required;
    }
    auto clock = std::make_shared<RuntimeSimulationClockAdapter>(
        app.Services().Get<TimeServices>()->clock);
    auto commit = std::make_shared<RuntimeSimulationCommitTarget>();
    simulation::SimulationDependencies dependencies{};
    dependencies.clock = clock;
    dependencies.commit_target = commit;
    const auto created = simulation::CreateSimulationServices(options, dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<simulation::SimulationServices>(created.Value()),
                                    "Simulation")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterNavigation(core::Application& app,
                                            navigation::NavigationOptions options)
{
    if (const auto required = Require<EnvironmentServices>(app, "Environment"); !required)
    {
        return required;
    }
    auto adapter = std::make_shared<RuntimeNavigationEnvironmentAdapter>(
        app.Services().Get<EnvironmentServices>());
    navigation::NavigationDependencies dependencies{};
    dependencies.data_source = adapter;
    dependencies.cost_provider = adapter;
    dependencies.obstacle_source = adapter;
    options.enable_mock_queries = true;
    const auto created = navigation::CreateNavigationServices(options, dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<navigation::NavigationServices>(created.Value()),
                                    "Navigation")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterAnimation(core::Application& app,
                                           animation::AnimationOptions options)
{
    if (const auto required = Require<ResourceServices>(app, "Resources"); !required)
    {
        return required;
    }
    auto mapper = std::make_shared<DefaultAnimationResourceMapper>();
    auto resources = std::make_shared<RuntimeAnimationResourceSource>(
        app.Services().Get<ResourceServices>()->manager,
        mapper);
    auto poses = std::make_shared<RuntimeAnimationPoseBridge>();
    auto evaluator = std::make_shared<RuntimeAnimationEvaluator>();
    animation::AnimationDependencies dependencies{};
    dependencies.resources = resources;
    dependencies.pose_sink = poses;
    dependencies.evaluator = evaluator;
    options.enable_mock_pose_evaluation = false;
    const auto created = animation::CreateAnimationServices(options, dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<animation::AnimationServices>(created.Value()),
                                    "Animation")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterPhysics(core::Application& app)
{
    if (const auto required = Require<SceneServices>(app, "Scene"); !required)
    {
        return required;
    }
    auto transforms = std::make_shared<RuntimeSceneTransformAdapter>(
        app.Services().Get<SceneServices>()->transforms);
    physics::PhysicsDependencies dependencies{};
    dependencies.transform_source = transforms;
    dependencies.transform_sink = transforms;
    const auto created = physics::CreatePhysicsServices(dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<physics::PhysicsServices>(created.Value()),
                                    "Physics")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterAudio(core::Application& app, audio::AudioOptions options)
{
    if (const auto required = Require<ResourceServices>(app, "Resources"); !required)
    {
        return required;
    }
    if (const auto required = Require<SceneServices>(app, "Scene"); !required)
    {
        return required;
    }
    auto backend = std::make_shared<ReferenceAudioBackend>();
    auto mapper = std::make_shared<DefaultAudioResourceMapper>();
    auto resources = std::make_shared<RuntimeAudioResourceSource>(
        app.Services().Get<ResourceServices>()->manager,
        mapper);
    auto transforms = std::make_shared<RuntimeSceneTransformAdapter>(
        app.Services().Get<SceneServices>()->transforms);
    audio::AudioDependencies dependencies{};
    dependencies.backend = backend;
    dependencies.resources = resources;
    dependencies.transforms = transforms;
    const auto created = audio::CreateAudioServices(options, dependencies);
    return created ? RegisterShared(app,
                                    std::make_shared<audio::AudioServices>(created.Value()),
                                    "Audio")
                   : foundation::Result<void>::Failure(created.GetError());
}

foundation::Result<void> RegisterRenderer(core::Application& app,
                                          const renderer::RendererOptions& options)
{
    if (const auto required = Require<ResourceServices>(app, "Resources"); !required)
    {
        return required;
    }
    if (const auto required = Require<SceneServices>(app, "Scene"); !required)
    {
        return required;
    }
    auto scene = app.Services().Get<SceneServices>();
    auto resource_bridge = std::make_shared<RuntimeRenderResourceBridge>(
        app.Services().Get<ResourceServices>()->manager);
    auto scene_source = std::make_shared<RuntimeRenderSceneSource>(scene->nodes, scene->transforms);
    auto command_sink = std::make_shared<ReferenceRenderCommandSink>();
    renderer::RendererDependencies dependencies{};
    dependencies.resource_bridge = resource_bridge;
    dependencies.scene_source = scene_source;
    dependencies.command_sink = command_sink;
    const auto created = renderer::CreateRendererServices(options, dependencies);
    if (!created)
    {
        return foundation::Result<void>::Failure(created.GetError());
    }
    renderer::RendererServices services = created.Value();
    if (const auto main = EnsureMainView(services, scene); !main)
    {
        return main;
    }
    return RegisterShared(app,
                          std::make_shared<renderer::RendererServices>(std::move(services)),
                          "Renderer");
}

std::vector<RuntimeAdapterKind> GetAllowedRuntimeAdapters()
{
    return {
        RuntimeAdapterKind::SceneToRenderer,
        RuntimeAdapterKind::ResourcesToRenderer,
        RuntimeAdapterKind::SceneToPhysics,
        RuntimeAdapterKind::SceneToAudio,
        RuntimeAdapterKind::ResourcesToAnimation,
        RuntimeAdapterKind::AnimationToRenderer,
        RuntimeAdapterKind::ResourcesToAudio,
        RuntimeAdapterKind::WorldResourcesPersistenceToStreaming,
        RuntimeAdapterKind::TimeToSimulation,
        RuntimeAdapterKind::SimulationToDomain,
        RuntimeAdapterKind::EnvironmentToNavigation,
    };
}

std::vector<RuntimeUpdateStep> GetRuntimeUpdateOrder()
{
    return {
        RuntimeUpdateStep::Time,
        RuntimeUpdateStep::MainThreadCommits,
        RuntimeUpdateStep::Resources,
        RuntimeUpdateStep::Streaming,
        RuntimeUpdateStep::Simulation,
        RuntimeUpdateStep::Navigation,
        RuntimeUpdateStep::Animation,
        RuntimeUpdateStep::Physics,
        RuntimeUpdateStep::SceneProjectionCommit,
        RuntimeUpdateStep::Audio,
        RuntimeUpdateStep::Renderer,
        RuntimeUpdateStep::DiagnosticsEvents,
    };
}

std::vector<RuntimeShutdownStep> GetRuntimeShutdownOrder()
{
    return {
        RuntimeShutdownStep::StopNewWork,
        RuntimeShutdownStep::ResolveSimulationWork,
        RuntimeShutdownStep::ShutdownStreaming,
        RuntimeShutdownStep::ShutdownAudio,
        RuntimeShutdownStep::ShutdownPhysics,
        RuntimeShutdownStep::FlushSceneProjections,
        RuntimeShutdownStep::ReleaseAnimationResources,
        RuntimeShutdownStep::ShutdownRenderer,
        RuntimeShutdownStep::ReleaseResourceAdapters,
        RuntimeShutdownStep::ClearNavigationAndEvents,
        RuntimeShutdownStep::DestroyAdapters,
        RuntimeShutdownStep::Complete,
    };
}
} // namespace epidemic::runtime
