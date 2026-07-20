#include "Epidemic/Runtime/Resources/resource_dependency_graph.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Resources/resource_loader.h"
#include "Epidemic/Runtime/Resources/resource_loader_registry.h"
#include "Epidemic/Runtime/Resources/resource_manager.h"
#include "Epidemic/Runtime/Resources/resource_payload.h"
#include "Epidemic/Runtime/Resources/resource_request.h"
#include "Epidemic/Runtime/Resources/resource_result.h"
#include "Epidemic/Runtime/Resources/resource_services.h"
#include "Epidemic/Runtime/Resources/resource_state.h"
#include "Epidemic/Runtime/Resources/resource_type.h"
#include "resource_dependency_graph.h"
#include "resource_loader_registry.h"
#include "resource_manager.h"

#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using epidemic::foundation::StringId;
using epidemic::runtime::ByteResourcePayload;
using epidemic::runtime::CanTransition;
using epidemic::runtime::CreateResourceServices;
using epidemic::runtime::IResourceLoader;
using epidemic::runtime::IResourceLoaderRegistry;
using epidemic::runtime::IResourceManager;
using epidemic::runtime::IResourcePayload;
using epidemic::runtime::ResourceLease;
using epidemic::runtime::ResourceDependency;
using epidemic::runtime::ResourceDependencyGraph;
using epidemic::runtime::ResourceHandle;
using epidemic::runtime::ResourceId;
using epidemic::runtime::ResourceLoadArtifact;
using epidemic::runtime::ResourceLoaderRegistry;
using epidemic::runtime::ResourceManager;
using epidemic::runtime::ResourcePayloadPtr;
using epidemic::runtime::ResourceRequest;
using epidemic::runtime::ResourceResult;
using epidemic::runtime::ResourceSlot;
using epidemic::runtime::ResourceState;
using epidemic::runtime::ResourceType;
using epidemic::runtime::RuntimeBudget;

[[nodiscard]] ResourceType Type(std::string_view value)
{
    return ResourceType{StringId::FromString(value)};
}

[[nodiscard]] ResourceRequest MakeRequest(std::string_view resource_id, std::string_view type)
{
    return ResourceRequest{ResourceId::FromString(resource_id), Type(type)};
}

[[nodiscard]] ResourceDependency MakeDependency(std::string_view resource_id, std::string_view type, bool required = true)
{
    return ResourceDependency{ResourceId::FromString(resource_id), Type(type), required};
}

class CountingLoader final : public IResourceLoader
{
  public:
    explicit CountingLoader(ResourceType type, std::size_t payload_size = 8, bool should_fail = false,
                            std::vector<ResourceDependency> dependencies = {})
        : type_(type), payload_size_(payload_size), should_fail_(should_fail), dependencies_(std::move(dependencies))
    {
    }

    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return type_;
    }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        ++load_count_;
        last_request_ = request;
        if (should_fail_)
        {
            return epidemic::foundation::Result<ResourceLoadArtifact>::Failure(
                epidemic::foundation::Error::Create("resource.load_failed", "test loader failed on purpose"));
        }

        std::vector<std::byte> bytes(payload_size_, std::byte{0x2a});
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, std::make_shared<ByteResourcePayload>(std::move(bytes)), dependencies_});
    }

    [[nodiscard]] int load_count() const noexcept
    {
        return load_count_;
    }

    [[nodiscard]] ResourceRequest last_request() const noexcept
    {
        return last_request_;
    }

  private:
    ResourceType type_{};
    std::size_t payload_size_ = 0;
    bool should_fail_ = false;
    int load_count_ = 0;
    ResourceRequest last_request_{};
    std::vector<ResourceDependency> dependencies_;
};

class SizedPayload final : public IResourcePayload
{
  public:
    explicit SizedPayload(std::size_t size) : size_(size)
    {
    }

    [[nodiscard]] std::size_t GetSizeBytes() const noexcept override
    {
        return size_;
    }

  private:
    std::size_t size_ = 0;
};

class SizedPayloadLoader final : public IResourceLoader
{
  public:
    SizedPayloadLoader(ResourceType type, std::size_t payload_size) : type_(type), payload_size_(payload_size)
    {
    }

    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return type_;
    }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(
            ResourceLoadArtifact{request.resource_id, request.type, std::make_shared<SizedPayload>(payload_size_), {}});
    }

  private:
    ResourceType type_{};
    std::size_t payload_size_ = 0;
};

class ArtifactOverrideLoader final : public IResourceLoader
{
  public:
    enum class Mode
    {
        MismatchedId,
        MismatchedType,
        NullPayload,
    };

    explicit ArtifactOverrideLoader(ResourceType type, Mode mode) : type_(type), mode_(mode)
    {
    }

    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return type_;
    }

    [[nodiscard]] epidemic::foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request) override
    {
        ResourceLoadArtifact artifact{request.resource_id, request.type, std::make_shared<ByteResourcePayload>(std::vector<std::byte>{std::byte{0x2a}}), {}};
        if (mode_ == Mode::MismatchedId)
        {
            artifact.resource_id = ResourceId::FromString("resources/wrong.mesh");
        }
        else if (mode_ == Mode::MismatchedType)
        {
            artifact.type = Type("texture");
        }
        else if (mode_ == Mode::NullPayload)
        {
            artifact.payload.reset();
        }
        return epidemic::foundation::Result<ResourceLoadArtifact>::Success(std::move(artifact));
    }

  private:
    ResourceType type_{};
    Mode mode_ = Mode::NullPayload;
};

[[nodiscard]] const ResourceSlot* Slot(const ResourceManager& manager, std::string_view resource_id)
{
    return manager.InspectSlot(ResourceId::FromString(resource_id));
}

[[nodiscard]] bool TestBasicTypesAndStateTransitions()
{
    const ResourceHandle handle{};
    const ResourceRequest request{};
    const auto result = ResourceResult<ResourceHandle>::Success(ResourceHandle{ResourceId::FromString("resources/a.mesh"), 2u});
    return !handle.IsValid() && !request.resource_id.IsValid() && result &&
           result.Value().generation == 2u && CanTransition(ResourceState::Unloaded, ResourceState::Queued) &&
           !CanTransition(ResourceState::Ready, ResourceState::Queued);
}

[[nodiscard]] bool TestDependencyGraphStoresDependencies()
{
    ResourceDependencyGraph graph;
    const auto root = ResourceId::FromString("resources/tree.mesh");
    graph.SetDependencies(root, {MakeDependency("resources/tree_albedo.tex", "texture"),
                                 MakeDependency("resources/tree_normals.tex", "texture", false)});
    const auto found = graph.FindDependencies(root);
    return graph.HasDependencies(root) && found && found->dependencies.size() == 2u && found->dependencies[0].required &&
           !found->dependencies[1].required;
}

[[nodiscard]] bool TestLoaderRegistryNonOwningContract()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    CountingLoader duplicate(Type("mesh"));
    const auto first = registry.RegisterLoader(loader);
    const auto second = registry.RegisterLoader(duplicate);
    return first && registry.FindLoader(loader.GetResourceType()) == &loader && registry.HasLoader(loader.GetResourceType()) && !second &&
           second.GetError().HasCode("resource_loader.duplicate_type");
}

[[nodiscard]] bool TestRequestQueuesAndProcessLoadsPayload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 16);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.RequestLease(MakeRequest("resources/tree.mesh", "mesh"));
    if (!handle || loader.load_count() != 0 || manager.GetState(handle.Value().resource) != ResourceState::Queued)
    {
        return false;
    }

    const auto processed = manager.ProcessPendingLoads();
    const auto payload = manager.GetPayload(handle.Value().resource);
    const auto stats = manager.GetMemoryStatistics();
    return processed && processed.Value().processed_jobs == 1u && processed.Value().loaded_resources == 1u && loader.load_count() == 1 &&
           manager.IsReady(handle.Value().resource) && payload && payload->GetSizeBytes() == 16u && stats.resident_bytes == 16u && stats.ready_count == 1u;
}

[[nodiscard]] bool TestProcessBudgetLimitsJobs()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/a.mesh", "mesh"));
    const auto second = manager.RequestLease(MakeRequest("resources/b.mesh", "mesh"));
    RuntimeBudget budget{};
    budget.max_items = 1;
    const auto processed = manager.ProcessPendingLoads(budget);
    return first && second && processed && processed.Value().processed_jobs == 1u && loader.load_count() == 1 &&
           manager.GetState(first.Value().resource) == ResourceState::Ready && manager.GetState(second.Value().resource) == ResourceState::Queued;
}

[[nodiscard]] bool TestRepeatedReadyRequestDoesNotReload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/rock.mesh", "mesh"));
    if (!first || !manager.ProcessPendingLoads())
    {
        return false;
    }
    const auto second = manager.RequestLease(MakeRequest("resources/rock.mesh", "mesh"));
    const ResourceSlot* slot = Slot(manager, "resources/rock.mesh");
    return second && first.Value().resource == second.Value().resource && loader.load_count() == 1 && slot != nullptr && slot->reference_count == 2u;
}

[[nodiscard]] bool TestTypeMismatchRejectsConflictingRequest()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"));
    CountingLoader texture_loader(Type("texture"));
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto mesh = manager.RequestLease(MakeRequest("resources/shared.asset", "mesh"));
    const auto texture = manager.RequestLease(MakeRequest("resources/shared.asset", "texture"));
    return mesh && !texture && texture.GetError().HasCode("resource.type_mismatch") && manager.GetState(mesh.Value().resource) == ResourceState::Queued;
}

[[nodiscard]] bool TestDependenciesLoadAndReleaseOnlyOnEvict()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 10, false, {MakeDependency("resources/tree_albedo.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 6);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/tree.mesh", "mesh"));
    if (!root)
    {
        return false;
    }
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* root_slot = Slot(manager, "resources/tree.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/tree_albedo.tex");
    if (!processed || !root_slot || !dependency_slot || root_slot->state != ResourceState::Ready || dependency_slot->state != ResourceState::Ready ||
        dependency_slot->reference_count != 1u)
    {
        return false;
    }

    const auto release_root = manager.Release(root.Value());
    if (!release_root || manager.GetState(root.Value().resource) != ResourceState::Ready || root_slot->reference_count != 0u ||
        dependency_slot->reference_count != 1u)
    {
        return false;
    }

    const auto evicted = manager.Evict(ResourceId::FromString("resources/tree.mesh"));
    return evicted && manager.GetState(root.Value().resource) == ResourceState::Unknown && dependency_slot->reference_count == 0u &&
           dependency_slot->state == ResourceState::Ready;
}

[[nodiscard]] bool TestSharedDependencySurvivesUntilBothRootsEvict()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 4, false, {MakeDependency("resources/shared.tex", "texture")});
    CountingLoader material_loader(Type("material"), 4, false, {MakeDependency("resources/shared.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 4);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(material_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/tree.mesh", "mesh"));
    const auto second = manager.RequestLease(MakeRequest("resources/tree.mat", "material"));
    if (!first || !second || !manager.ProcessPendingLoads())
    {
        return false;
    }

    const ResourceSlot* shared = Slot(manager, "resources/shared.tex");
    if (!shared || shared->reference_count != 2u)
    {
        return false;
    }

    const auto release_first = manager.Release(first.Value());
    const auto first_evict = manager.Evict(ResourceId::FromString("resources/tree.mesh"));
    if (!release_first || !first_evict || shared->reference_count != 1u)
    {
        return false;
    }

    const auto release_second = manager.Release(second.Value());
    const auto second_evict = manager.Evict(ResourceId::FromString("resources/tree.mat"));
    return release_second && second_evict && shared->reference_count == 0u && shared->state == ResourceState::Ready;
}

[[nodiscard]] bool TestDependencyCycleAcrossChainFails()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 4, false, {MakeDependency("resources/b.material", "material")});
    CountingLoader material_loader(Type("material"), 4, false, {MakeDependency("resources/c.texture", "texture")});
    CountingLoader texture_loader(Type("texture"), 4, false, {MakeDependency("resources/a.mesh", "mesh")});
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(material_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/a.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads(RuntimeBudget{});
    const ResourceSlot* texture = Slot(manager, "resources/c.texture");
    return root && processed && processed.Value().failed_resources >= 1u && texture &&
           texture->state == ResourceState::Failed;
}

[[nodiscard]] bool TestMemoryBudgetFailureRollsBackPayload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 64);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    manager.SetMemoryBudgetBytes(8);
    const auto handle = manager.RequestLease(MakeRequest("resources/heavy.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* slot = Slot(manager, "resources/heavy.mesh");
    const auto stats = manager.GetMemoryStatistics();
    return handle && processed && processed.Value().failed_resources == 1u && slot && slot->state == ResourceState::Failed &&
           !slot->payload && stats.resident_bytes == 0u;
}

[[nodiscard]] bool TestMemoryBudgetOverflowCannotFit()
{
    ResourceLoaderRegistry registry;
    SizedPayloadLoader huge_loader(Type("huge"), std::numeric_limits<std::size_t>::max() - 7u);
    SizedPayloadLoader tiny_loader(Type("tiny"), 16u);
    if (!registry.RegisterLoader(huge_loader) || !registry.RegisterLoader(tiny_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    manager.SetMemoryBudgetBytes(std::numeric_limits<std::size_t>::max());
    const auto huge = manager.RequestLease(MakeRequest("resources/huge.bin", "huge"));
    RuntimeBudget one_job{};
    one_job.max_items = 1;
    const auto first_tick = manager.ProcessPendingLoads(one_job);
    const auto tiny = manager.RequestLease(MakeRequest("resources/tiny.bin", "tiny"));
    const auto second_tick = manager.ProcessPendingLoads(one_job);
    const ResourceSlot* huge_slot = Slot(manager, "resources/huge.bin");
    const ResourceSlot* tiny_slot = Slot(manager, "resources/tiny.bin");
    const auto stats = manager.GetMemoryStatistics();
    return huge && tiny && first_tick && second_tick && second_tick.Value().failed_resources == 1u &&
           huge_slot && huge_slot->state == ResourceState::Ready &&
           tiny_slot && tiny_slot->state == ResourceState::Failed &&
           stats.resident_bytes == huge_slot->memory_bytes;
}

[[nodiscard]] bool TestRequiredDependencyFailureReleasesAcquiredHandles()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 8, false, {MakeDependency("resources/missing.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 4, true);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/root.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* root_slot = Slot(manager, "resources/root.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/missing.tex");
    return root && processed && processed.Value().failed_resources >= 1u && root_slot && dependency_slot &&
           root_slot->state == ResourceState::Failed && root_slot->dependency_handles.empty() && !root_slot->payload &&
           dependency_slot->state == ResourceState::Failed && dependency_slot->reference_count == 0u;
}

[[nodiscard]] bool TestMemoryBudgetFailureAfterDependencyAcquisitionReleasesHandles()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 64, false, {MakeDependency("resources/budget.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 4);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    manager.SetMemoryBudgetBytes(16);
    const auto root = manager.RequestLease(MakeRequest("resources/heavy-root.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* root_slot = Slot(manager, "resources/heavy-root.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/budget.tex");
    const auto stats = manager.GetMemoryStatistics();
    return root && processed && processed.Value().failed_resources == 1u && root_slot && dependency_slot &&
           root_slot->state == ResourceState::Failed && root_slot->dependency_handles.empty() &&
           dependency_slot->state == ResourceState::Ready && dependency_slot->reference_count == 0u &&
           stats.resident_bytes == dependency_slot->memory_bytes;
}

[[nodiscard]] bool TestOptionalDependencyFailureDoesNotFailRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 8, false, {MakeDependency("resources/optional.tex", "texture", false)});
    CountingLoader texture_loader(Type("texture"), 4, true);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/optional-root.mesh", "mesh"));
    const auto processed = manager.ProcessPendingLoads();
    const ResourceSlot* root_slot = Slot(manager, "resources/optional-root.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/optional.tex");
    return root && processed && root_slot && dependency_slot && root_slot->state == ResourceState::Ready &&
           root_slot->dependency_handles.empty() && dependency_slot->reference_count == 0u;
}

[[nodiscard]] bool TestOptionalDependencyDoesNotBlockRoot()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 8, false, {MakeDependency("resources/later.tex", "texture", false)});
    CountingLoader texture_loader(Type("texture"), 4);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/nonblocking-root.mesh", "mesh"));
    RuntimeBudget budget{};
    budget.max_items = 1;
    const auto processed = manager.ProcessPendingLoads(budget);
    const ResourceSlot* root_slot = Slot(manager, "resources/nonblocking-root.mesh");
    const ResourceSlot* dependency_slot = Slot(manager, "resources/later.tex");
    return root && processed && processed.Value().processed_jobs == 1u && root_slot && dependency_slot &&
           root_slot->state == ResourceState::Ready && dependency_slot->state == ResourceState::Queued &&
           dependency_slot->reference_count == 0u;
}

[[nodiscard]] bool TestQueueContinuesAfterIndependentFailure()
{
    ResourceLoaderRegistry registry;
    CountingLoader failing_loader(Type("mesh"), 8, true);
    CountingLoader texture_loader(Type("texture"), 6);
    if (!registry.RegisterLoader(failing_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto failed = manager.RequestLease(MakeRequest("resources/bad.mesh", "mesh"));
    const auto good = manager.RequestLease(MakeRequest("resources/good.tex", "texture"));
    const auto processed = manager.ProcessPendingLoads();
    return failed && good && processed && processed.Value().processed_jobs == 2u &&
           processed.Value().failed_resources == 1u && processed.Value().loaded_resources == 1u &&
           manager.GetState(failed.Value().resource) == ResourceState::Failed && manager.GetState(good.Value().resource) == ResourceState::Ready;
}

[[nodiscard]] bool TestInvalidLoaderArtifactsFail()
{
    ResourceLoaderRegistry wrong_id_registry;
    ResourceLoaderRegistry wrong_type_registry;
    ResourceLoaderRegistry null_payload_registry;
    ArtifactOverrideLoader wrong_id(Type("mesh"), ArtifactOverrideLoader::Mode::MismatchedId);
    ArtifactOverrideLoader wrong_type(Type("mesh"), ArtifactOverrideLoader::Mode::MismatchedType);
    ArtifactOverrideLoader null_payload(Type("mesh"), ArtifactOverrideLoader::Mode::NullPayload);
    if (!wrong_id_registry.RegisterLoader(wrong_id) || !wrong_type_registry.RegisterLoader(wrong_type) ||
        !null_payload_registry.RegisterLoader(null_payload))
    {
        return false;
    }

    ResourceManager wrong_id_manager(&wrong_id_registry);
    ResourceManager wrong_type_manager(&wrong_type_registry);
    ResourceManager null_payload_manager(&null_payload_registry);
    const auto wrong_id_handle = wrong_id_manager.RequestLease(MakeRequest("resources/bad-id.mesh", "mesh"));
    const auto wrong_type_handle = wrong_type_manager.RequestLease(MakeRequest("resources/bad-type.mesh", "mesh"));
    const auto null_payload_handle = null_payload_manager.RequestLease(MakeRequest("resources/null.mesh", "mesh"));
    const auto wrong_id_processed = wrong_id_manager.ProcessPendingLoads();
    const auto wrong_type_processed = wrong_type_manager.ProcessPendingLoads();
    const auto null_payload_processed = null_payload_manager.ProcessPendingLoads();

    return wrong_id_handle && wrong_type_handle && null_payload_handle &&
           wrong_id_processed && wrong_id_processed.Value().failed_resources == 1u &&
           wrong_type_processed && wrong_type_processed.Value().failed_resources == 1u &&
           null_payload_processed && null_payload_processed.Value().failed_resources == 1u &&
           wrong_id_manager.GetState(wrong_id_handle.Value().resource) == ResourceState::Failed &&
           wrong_type_manager.GetState(wrong_type_handle.Value().resource) == ResourceState::Failed &&
           null_payload_manager.GetState(null_payload_handle.Value().resource) == ResourceState::Failed;
}

[[nodiscard]] bool TestRetryAfterFailedLoad()
{
    ResourceLoaderRegistry registry;
    CountingLoader failing_loader(Type("mesh"), 8, true);
    if (!registry.RegisterLoader(failing_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/retry.mesh", "mesh"));
    const auto failed = manager.ProcessPendingLoads();
    const auto second = manager.RequestLease(MakeRequest("resources/retry.mesh", "mesh"));
    const auto retried = manager.ProcessPendingLoads();
    const ResourceSlot* slot = Slot(manager, "resources/retry.mesh");
    return first && failed && failed.Value().failed_resources == 1u && second && first.Value().resource == second.Value().resource &&
           retried && retried.Value().failed_resources == 1u && slot && slot->reference_count == 2u &&
           failing_loader.load_count() == 2;
}

[[nodiscard]] bool TestByteBudgetStopsFurtherJobs()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 8);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/byte-a.mesh", "mesh"));
    const auto second = manager.RequestLease(MakeRequest("resources/byte-b.mesh", "mesh"));
    RuntimeBudget budget{};
    budget.max_bytes = 8;
    const auto processed = manager.ProcessPendingLoads(budget);
    return first && second && processed && processed.Value().loaded_resources == 1u &&
           processed.Value().bytes_loaded == 8u && manager.GetState(first.Value().resource) == ResourceState::Ready &&
           manager.GetState(second.Value().resource) == ResourceState::Queued;
}

[[nodiscard]] bool TestByteBudgetIsSoftForCurrentPayload()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 8);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/soft-byte.mesh", "mesh"));
    const auto second = manager.RequestLease(MakeRequest("resources/soft-byte-b.mesh", "mesh"));
    RuntimeBudget budget{};
    budget.max_bytes = 4;
    const auto processed = manager.ProcessPendingLoads(budget);
    return first && second && processed && processed.Value().loaded_resources == 1u &&
           processed.Value().bytes_loaded == 8u && manager.GetState(first.Value().resource) == ResourceState::Ready &&
           manager.GetState(second.Value().resource) == ResourceState::Queued;
}

[[nodiscard]] bool TestCancellationAfterDependencyAcquisitionReleasesDependency()
{
    ResourceLoaderRegistry registry;
    CountingLoader mesh_loader(Type("mesh"), 8, false, {MakeDependency("resources/cancel.tex", "texture")});
    CountingLoader texture_loader(Type("texture"), 4);
    if (!registry.RegisterLoader(mesh_loader) || !registry.RegisterLoader(texture_loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto root = manager.RequestLease(MakeRequest("resources/cancel-root.mesh", "mesh"));
    RuntimeBudget one_job{};
    one_job.max_items = 1;
    const auto first_tick = manager.ProcessPendingLoads(one_job);
    const ResourceSlot* dependency_before = Slot(manager, "resources/cancel.tex");
    if (!root || !first_tick || !dependency_before || dependency_before->reference_count != 1u)
    {
        return false;
    }

    const auto released_root = manager.Release(root.Value());
    const auto second_tick = manager.ProcessPendingLoads(one_job);
    const auto third_tick = manager.ProcessPendingLoads(one_job);
    const ResourceSlot* dependency_after = Slot(manager, "resources/cancel.tex");
    const ResourceSlot* root_after = Slot(manager, "resources/cancel-root.mesh");
    return released_root && second_tick && third_tick && dependency_after && dependency_after->reference_count == 0u &&
           root_after && root_after->dependency_handles.empty() && !root_after->payload;
}

[[nodiscard]] bool TestResourceLeasePreventsCrossConsumerDoubleRelease()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 8);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto first = manager.RequestLease(MakeRequest("resources/leased.mesh", "mesh"));
    const auto second = manager.RequestLease(MakeRequest("resources/leased.mesh", "mesh"));
    if (!first || !second || first.Value() == second.Value() || !manager.ProcessPendingLoads())
    {
        return false;
    }

    const auto release_first = manager.Release(first.Value());
    const auto release_first_again = manager.Release(first.Value());
    const ResourceSlot* slot_after_double = Slot(manager, "resources/leased.mesh");
    const std::uint32_t count_after_double = slot_after_double != nullptr ? slot_after_double->reference_count : 0u;
    const auto release_second = manager.Release(second.Value());
    const ResourceSlot* slot_after_second = Slot(manager, "resources/leased.mesh");
    return release_first && !release_first_again &&
           release_first_again.GetError().HasCode("resource.acquisition_underflow") &&
           slot_after_double && count_after_double == 1u &&
           release_second && slot_after_second && slot_after_second->reference_count == 0u;
}

[[nodiscard]] bool TestInFlightResourcesAreNotEvicted()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.RequestLease(MakeRequest("resources/inflight.mesh", "mesh"));
    if (!handle)
    {
        return false;
    }
    const auto release = manager.Release(handle.Value());
    const auto evicted_count = manager.EvictUnreferenced();
    const ResourceSlot* slot = Slot(manager, "resources/inflight.mesh");
    return handle && release && evicted_count == 0u && slot && slot->state == ResourceState::Queued;
}

[[nodiscard]] bool TestUnknownAndStaleHandles()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"));
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.RequestLease(MakeRequest("resources/stale.mesh", "mesh"));
    if (!handle || !manager.ProcessPendingLoads())
    {
        return false;
    }
    const auto released = manager.Release(handle.Value());
    const auto double_release = manager.Release(handle.Value());
    const auto evicted = manager.Evict(ResourceId::FromString("resources/stale.mesh"));
    const auto invalid = manager.ValidateHandle(ResourceHandle{});
    const auto stale = manager.ValidateHandle(handle.Value().resource);
    const auto stale_release = manager.Release(handle.Value());
    return released && !double_release && double_release.GetError().HasCode("resource.acquisition_underflow") && evicted &&
           manager.GetState(ResourceHandle{}) == ResourceState::Unknown && manager.GetState(handle.Value().resource) == ResourceState::Unknown &&
           !invalid && invalid.GetError().HasCode("resource.invalid_handle") && !stale &&
           stale.GetError().HasCode("resource.stale_handle") && !stale_release &&
           stale_release.GetError().HasCode("resource.stale_handle");
}

[[nodiscard]] bool TestMemoryStatisticsTrackUnreferencedCache()
{
    ResourceLoaderRegistry registry;
    CountingLoader loader(Type("mesh"), 20);
    if (!registry.RegisterLoader(loader))
    {
        return false;
    }

    ResourceManager manager(&registry);
    const auto handle = manager.RequestLease(MakeRequest("resources/cache.mesh", "mesh"));
    if (!handle || !manager.ProcessPendingLoads())
    {
        return false;
    }

    const auto ready = manager.GetMemoryStatistics();
    const auto released = manager.Release(handle.Value());
    const auto cached = manager.GetMemoryStatistics();
    const auto evicted = manager.Evict(ResourceId::FromString("resources/cache.mesh"));
    const auto empty = manager.GetMemoryStatistics();
    return ready.ready_bytes == 20u && ready.cached_unreferenced_bytes == 0u && ready.resource_count == 1u &&
           released && cached.ready_bytes == 20u && cached.cached_unreferenced_bytes == 20u &&
           evicted && empty.ready_bytes == 0u && empty.cached_unreferenced_bytes == 0u && empty.resource_count == 1u;
}

[[nodiscard]] bool TestFactoryCreatesUsableServices()
{
    const auto services = CreateResourceServices();
    if (!services || !services.Value().loaders || !services.Value().manager)
    {
        return false;
    }

    CountingLoader loader(Type("mesh"));
    if (!services.Value().loaders->RegisterLoader(loader))
    {
        return false;
    }
    const auto handle = services.Value().manager->RequestLease(MakeRequest("resources/factory.mesh", "mesh"));
    const auto processed = services.Value().manager->ProcessPendingLoads();
    return handle && processed && services.Value().manager->IsReady(handle.Value().resource);
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(ResourceHandle{}.generation), std::uint32_t>);
    static_assert(std::is_same_v<decltype(ResourceRequest{}.resource_id), ResourceId>);
    static_assert(std::is_abstract_v<IResourceManager>);
    static_assert(std::is_abstract_v<IResourceLoader>);
    static_assert(std::is_abstract_v<IResourceLoaderRegistry>);

    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"BasicTypesAndStateTransitions", TestBasicTypesAndStateTransitions},
        {"DependencyGraphStoresDependencies", TestDependencyGraphStoresDependencies},
        {"LoaderRegistryNonOwningContract", TestLoaderRegistryNonOwningContract},
        {"RequestQueuesAndProcessLoadsPayload", TestRequestQueuesAndProcessLoadsPayload},
        {"ProcessBudgetLimitsJobs", TestProcessBudgetLimitsJobs},
        {"RepeatedReadyRequestDoesNotReload", TestRepeatedReadyRequestDoesNotReload},
        {"TypeMismatchRejectsConflictingRequest", TestTypeMismatchRejectsConflictingRequest},
        {"DependenciesLoadAndReleaseOnlyOnEvict", TestDependenciesLoadAndReleaseOnlyOnEvict},
        {"SharedDependencySurvivesUntilBothRootsEvict", TestSharedDependencySurvivesUntilBothRootsEvict},
        {"DependencyCycleAcrossChainFails", TestDependencyCycleAcrossChainFails},
        {"MemoryBudgetFailureRollsBackPayload", TestMemoryBudgetFailureRollsBackPayload},
        {"MemoryBudgetOverflowCannotFit", TestMemoryBudgetOverflowCannotFit},
        {"RequiredDependencyFailureReleasesAcquiredHandles", TestRequiredDependencyFailureReleasesAcquiredHandles},
        {"MemoryBudgetFailureAfterDependencyAcquisitionReleasesHandles", TestMemoryBudgetFailureAfterDependencyAcquisitionReleasesHandles},
        {"OptionalDependencyFailureDoesNotFailRoot", TestOptionalDependencyFailureDoesNotFailRoot},
        {"OptionalDependencyDoesNotBlockRoot", TestOptionalDependencyDoesNotBlockRoot},
        {"QueueContinuesAfterIndependentFailure", TestQueueContinuesAfterIndependentFailure},
        {"InvalidLoaderArtifactsFail", TestInvalidLoaderArtifactsFail},
        {"RetryAfterFailedLoad", TestRetryAfterFailedLoad},
        {"ByteBudgetStopsFurtherJobs", TestByteBudgetStopsFurtherJobs},
        {"ByteBudgetIsSoftForCurrentPayload", TestByteBudgetIsSoftForCurrentPayload},
        {"CancellationAfterDependencyAcquisitionReleasesDependency", TestCancellationAfterDependencyAcquisitionReleasesDependency},
        {"ResourceLeasePreventsCrossConsumerDoubleRelease", TestResourceLeasePreventsCrossConsumerDoubleRelease},
        {"InFlightResourcesAreNotEvicted", TestInFlightResourcesAreNotEvicted},
        {"UnknownAndStaleHandles", TestUnknownAndStaleHandles},
        {"MemoryStatisticsTrackUnreferencedCache", TestMemoryStatisticsTrackUnreferencedCache},
        {"FactoryCreatesUsableServices", TestFactoryCreatesUsableServices},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Resources test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}

