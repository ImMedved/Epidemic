#include "Epidemic/Runtime/World/world_invariants.h"

#include "Epidemic/Foundation/error.h"

#include <string_view>
#include <unordered_set>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> WorldInvariantFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool IsDestroyed(const WorldObjectSnapshot& object) noexcept
{
    return std::holds_alternative<DestroyedPlacement>(object.placement);
}

[[nodiscard]] foundation::Result<WorldObjectSnapshot> RequireLiveOwner(RuntimeObjectId object_id,
                                                                        RuntimeObjectId owner_id,
                                                                        const IWorldQuery& objects,
                                                                        std::string_view role)
{
    if (!owner_id.IsValid())
    {
        return foundation::Result<WorldObjectSnapshot>::Failure(
            foundation::Error::Create("world.invalid_placement", "placement owner id must be valid"));
    }
    if (owner_id == object_id)
    {
        return foundation::Result<WorldObjectSnapshot>::Failure(
            foundation::Error::Create("world.invalid_placement", "object cannot be placed inside itself"));
    }

    const auto owner = objects.FindObject(owner_id);
    if (!owner)
    {
        return foundation::Result<WorldObjectSnapshot>::Failure(
            foundation::Error::Create("world.owner_not_found", "placement owner object was not found"));
    }
    if (IsDestroyed(*owner))
    {
        return foundation::Result<WorldObjectSnapshot>::Failure(
            foundation::Error::Create("world.invalid_placement", "placement owner must not be destroyed"));
    }
    (void)role;
    return foundation::Result<WorldObjectSnapshot>::Success(*owner);
}

[[nodiscard]] std::optional<RuntimeObjectId> DirectContainerOwner(const ObjectPlacement& placement)
{
    if (const auto* container = std::get_if<ContainerPlacement>(&placement))
    {
        return container->container;
    }
    if (const auto* inventory = std::get_if<InventoryPlacement>(&placement))
    {
        return inventory->owner;
    }
    if (const auto* equipped = std::get_if<EquippedPlacement>(&placement))
    {
        return equipped->owner;
    }
    return std::nullopt;
}

[[nodiscard]] foundation::Result<void> ValidateNoContainmentCycle(RuntimeObjectId object_id,
                                                                  RuntimeObjectId owner_id,
                                                                  const IWorldQuery& objects)
{
    std::unordered_set<RuntimeObjectId> visited;
    RuntimeObjectId current = owner_id;
    while (current.IsValid())
    {
        if (current == object_id)
        {
            return WorldInvariantFailure("world.containment_cycle", "placement would create a containment cycle");
        }
        if (!visited.insert(current).second)
        {
            return WorldInvariantFailure("world.containment_cycle", "existing containment graph contains a cycle");
        }

        const auto owner = objects.FindObject(current);
        if (!owner)
        {
            return foundation::Result<void>::Success();
        }
        const auto next = DirectContainerOwner(owner->placement);
        if (!next)
        {
            return foundation::Result<void>::Success();
        }
        current = *next;
    }
    return foundation::Result<void>::Success();
}
} // namespace

foundation::Result<void> ValidateRealityResidencyCombination(ObjectRealityLevel reality, ResidencyState residency)
{
    if (reality == ObjectRealityLevel::Physical && (residency == ResidencyState::Unloaded || residency == ResidencyState::Loading))
    {
        return WorldInvariantFailure("world.invalid_reality_residency", "physical objects must be resident or active");
    }
    if (reality != ObjectRealityLevel::Physical && residency == ResidencyState::Active)
    {
        return WorldInvariantFailure("world.invalid_reality_residency", "only physical objects may be active");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ValidateWorldObjectInvariant(const WorldObjectSnapshot& object,
                                                      const IRegionRegistry& regions,
                                                      const IChunkRegistry& chunks,
                                                      const IWorldQuery& objects)
{
    if (!object.runtime_id.IsValid())
    {
        return WorldInvariantFailure("world.invalid_object", "world object runtime id must be valid");
    }
    if (object.persistent_id.IsValid() && object.persistence_tier == PersistenceTier::Disposable)
    {
        return WorldInvariantFailure("world.invalid_persistence_tier", "persistent objects must not use disposable persistence tier");
    }

    const auto reality_residency = ValidateRealityResidencyCombination(object.reality, object.residency);
    if (!reality_residency)
    {
        return reality_residency;
    }

    if (std::holds_alternative<DestroyedPlacement>(object.placement))
    {
        if (object.residency == ResidencyState::Active || object.reality == ObjectRealityLevel::Physical)
        {
            return WorldInvariantFailure("world.invalid_destroyed_state", "destroyed objects must not be active or physical");
        }
        return foundation::Result<void>::Success();
    }

    if (const auto* surface = std::get_if<WorldSurfacePlacement>(&object.placement))
    {
        if (!surface->region.IsValid() || !surface->chunk.IsValid() || !IsValidTransform(surface->transform))
        {
            return WorldInvariantFailure("world.invalid_placement", "world surface placement requires region, chunk and valid transform");
        }
        if (!regions.FindRegion(surface->region))
        {
            return WorldInvariantFailure("world.region_not_found", "world surface placement region was not found");
        }
        const auto chunk = chunks.FindChunk(surface->chunk);
        if (!chunk)
        {
            return WorldInvariantFailure("world.chunk_not_found", "world surface placement chunk was not found");
        }
        if (chunk->region_id != surface->region)
        {
            return WorldInvariantFailure("world.chunk_region_mismatch", "world surface placement chunk belongs to another region");
        }
        return foundation::Result<void>::Success();
    }

    if (const auto* container = std::get_if<ContainerPlacement>(&object.placement))
    {
        if (!container->slot.IsValid())
        {
            return WorldInvariantFailure("world.invalid_placement", "container placement requires a valid slot");
        }
        const auto owner = RequireLiveOwner(object.runtime_id, container->container, objects, "container");
        if (!owner)
        {
            return foundation::Result<void>::Failure(owner.GetError());
        }
        return ValidateNoContainmentCycle(object.runtime_id, container->container, objects);
    }

    if (const auto* inventory = std::get_if<InventoryPlacement>(&object.placement))
    {
        const auto owner = RequireLiveOwner(object.runtime_id, inventory->owner, objects, "inventory");
        if (!owner)
        {
            return foundation::Result<void>::Failure(owner.GetError());
        }
        return ValidateNoContainmentCycle(object.runtime_id, inventory->owner, objects);
    }

    if (const auto* equipped = std::get_if<EquippedPlacement>(&object.placement))
    {
        if (!equipped->slot.IsValid())
        {
            return WorldInvariantFailure("world.invalid_placement", "equipped placement requires a valid slot");
        }
        const auto owner = RequireLiveOwner(object.runtime_id, equipped->owner, objects, "equipped");
        if (!owner)
        {
            return foundation::Result<void>::Failure(owner.GetError());
        }
        return ValidateNoContainmentCycle(object.runtime_id, equipped->owner, objects);
    }

    if (const auto* hidden = std::get_if<HiddenPlacement>(&object.placement))
    {
        if (hidden->region.IsValid() && !regions.FindRegion(hidden->region))
        {
            return WorldInvariantFailure("world.region_not_found", "hidden placement region was not found");
        }
    }

    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime
