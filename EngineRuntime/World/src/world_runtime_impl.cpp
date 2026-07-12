#include "world_runtime_impl.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
#include "Epidemic/Foundation/error.h"

namespace epidemic::runtime
{
// Function note: Registers region.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::RegisterRegion(RegionDescriptor region)
{
    if (!region.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.invalid_region", "region id must be valid before registration"));
    }

    regions_[region.id] = region;
    return foundation::Result<void>::Success();
}

// Function note: Finds region.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<RegionDescriptor> WorldRuntime::FindRegion(RegionId id) const
{
    const auto iterator = regions_.find(id);
    if (iterator == regions_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Registers chunk.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::RegisterChunk(ChunkDescriptor chunk)
{
    if (!chunk.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.invalid_chunk", "chunk id must be valid before registration"));
    }

    if (!chunk.region_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.invalid_region", "chunk region id must be valid before registration"));
    }

    chunks_[chunk.id] = ChunkRecord{chunk, ChunkState::Unloaded};
    return foundation::Result<void>::Success();
}

// Function note: Finds chunk.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<ChunkDescriptor> WorldRuntime::FindChunk(ChunkId id) const
{
    const auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return std::nullopt;
    }

    return iterator->second.descriptor;
}

// Function note: Gets chunk state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ChunkState WorldRuntime::GetChunkState(ChunkId id) const
{
    const auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return ChunkState::Unloaded;
    }

    return iterator->second.state;
}

// Function note: Creates object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<RuntimeObjectId> WorldRuntime::CreateObject(WorldObjectRecord record)
{
    const RuntimeObjectId runtime_id{next_runtime_object_value_++};
    record.runtime_id = runtime_id;
    world_objects_[runtime_id] = record;
    return foundation::Result<RuntimeObjectId>::Success(runtime_id);
}

// Function note: Destroys object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::DestroyObject(RuntimeObjectId id)
{
    if (!id.IsValid())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.invalid_object", "runtime object id must be valid before destruction"));
    }

    const auto erased = world_objects_.erase(id);
    if (erased == 0)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.object_not_found", "world object was not found for destruction"));
    }

    return foundation::Result<void>::Success();
}

// Function note: Finds object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::optional<WorldObjectRecord> WorldRuntime::FindObject(RuntimeObjectId id) const
{
    const auto iterator = world_objects_.find(id);
    if (iterator == world_objects_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Function note: Finds objects in region.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::vector<WorldObjectRecord> WorldRuntime::FindObjectsInRegion(RegionId region) const
{
    std::vector<WorldObjectRecord> matches;
    for (const auto& [runtime_id, object] : world_objects_)
    {
        (void)runtime_id;
        if (object.placement.region_id == region)
        {
            matches.push_back(object);
        }
    }

    return matches;
}

// Function note: Finds objects in chunk.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::vector<WorldObjectRecord> WorldRuntime::FindObjectsInChunk(ChunkId chunk) const
{
    std::vector<WorldObjectRecord> matches;
    for (const auto& [runtime_id, object] : world_objects_)
    {
        (void)runtime_id;
        if (object.placement.chunk_id == chunk)
        {
            matches.push_back(object);
        }
    }

    return matches;
}

// Function note: Finds objects by reality.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
std::vector<WorldObjectRecord> WorldRuntime::FindObjectsByReality(ObjectRealityLevel reality) const
{
    std::vector<WorldObjectRecord> matches;
    for (const auto& [runtime_id, object] : world_objects_)
    {
        (void)runtime_id;
        if (object.reality == reality)
        {
            matches.push_back(object);
        }
    }

    return matches;
}

// Function note: Sets placement.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::SetPlacement(RuntimeObjectId id, ObjectPlacement placement)
{
    auto iterator = world_objects_.find(id);
    if (iterator == world_objects_.end())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.object_not_found", "world object was not found for placement update"));
    }

    iterator->second.placement = placement;
    return foundation::Result<void>::Success();
}

// Function note: Sets residency.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::SetResidency(RuntimeObjectId id, ResidencyState state)
{
    auto iterator = world_objects_.find(id);
    if (iterator == world_objects_.end())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.object_not_found", "world object was not found for residency update"));
    }

    iterator->second.residency = state;
    return foundation::Result<void>::Success();
}

// Function note: Handles materialize.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<RuntimeObjectId> WorldRuntime::Materialize(const MaterializationRequest& request)
{
    if (!request.persistent_id.IsValid())
    {
        return foundation::Result<RuntimeObjectId>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.invalid_persistent_object", "persistent object id must be valid before materialization"));
    }

    for (auto& [runtime_id, object] : world_objects_)
    {
        if (object.persistent_id != request.persistent_id)
        {
            continue;
        }

        object.reality = request.target_reality;
        object.residency = request.target_reality == ObjectRealityLevel::Physical ? ResidencyState::Active : ResidencyState::Resident;
        return foundation::Result<RuntimeObjectId>::Success(runtime_id);
    }

    return foundation::Result<RuntimeObjectId>::Failure(
        // Function note: Creates the associated runtime state.
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        foundation::Error::Create("world.object_not_found", "world object with the requested persistent id was not found"));
}

// Function note: Handles demote.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::Demote(const DemotionRequest& request)
{
    auto iterator = world_objects_.find(request.runtime_id);
    if (iterator == world_objects_.end())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.object_not_found", "world object was not found for demotion"));
    }

    iterator->second.reality = request.target_reality;
    iterator->second.residency = request.target_reality == ObjectRealityLevel::Physical ? ResidencyState::Active : ResidencyState::Resident;
    return foundation::Result<void>::Success();
}

// Function note: Sets chunk state.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> WorldRuntime::SetChunkState(ChunkId id, ChunkState state)
{
    auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates the associated runtime state.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            foundation::Error::Create("world.chunk_not_found", "chunk was not found for state update"));
    }

    iterator->second.state = state;
    return foundation::Result<void>::Success();
}
} 
