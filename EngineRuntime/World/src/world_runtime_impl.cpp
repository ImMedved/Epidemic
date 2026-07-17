#include "world_runtime_impl.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/World/world_invariants.h"

#include <algorithm>
#include <string_view>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Error MakeWorldError(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

void SortByRuntimeId(std::vector<WorldObjectRecord>& records)
{
    std::sort(records.begin(), records.end(), [](const WorldObjectRecord& left, const WorldObjectRecord& right) {
        return left.runtime_id.Raw() < right.runtime_id.Raw();
    });
}

template <typename TValue>
[[nodiscard]] foundation::Result<TValue> WorldFailureValue(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(MakeWorldError(code, message));
}

[[nodiscard]] foundation::Result<void> CheckExpectedRevision(const WorldObjectRecord& object, std::uint64_t expected_revision)
{
    if (expected_revision != 0 && object.revision != expected_revision)
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.revision_conflict", "world object revision does not match command expectation"));
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] ResidencyState ResidencyForReality(ObjectRealityLevel reality) noexcept
{
    return reality == ObjectRealityLevel::Physical ? ResidencyState::Active : ResidencyState::Resident;
}
} // namespace

foundation::Result<void> WorldRuntime::RegisterRegion(RegionDescriptor region)
{
    if (!region.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.invalid_region", "region id must be valid before registration"));
    }

    if (regions_.contains(region.id))
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.duplicate_region", "region id is already registered"));
    }

    regions_[region.id] = region;
    return foundation::Result<void>::Success();
}

std::optional<RegionDescriptor> WorldRuntime::FindRegion(RegionId id) const
{
    const auto iterator = regions_.find(id);
    if (iterator == regions_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

foundation::Result<void> WorldRuntime::RegisterChunk(ChunkDescriptor chunk)
{
    if (!chunk.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.invalid_chunk", "chunk id must be valid before registration"));
    }

    if (!chunk.region_id.IsValid())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.invalid_region", "chunk region id must be valid before registration"));
    }

    if (!regions_.contains(chunk.region_id))
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.region_not_found", "chunk must reference an existing region"));
    }

    if (chunks_.contains(chunk.id))
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.duplicate_chunk", "chunk id is already registered"));
    }

    chunks_[chunk.id] = ChunkRecord{chunk, ChunkState::Unloaded};
    return foundation::Result<void>::Success();
}

std::optional<ChunkDescriptor> WorldRuntime::FindChunk(ChunkId id) const
{
    const auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return std::nullopt;
    }

    return iterator->second.descriptor;
}

ChunkState WorldRuntime::GetChunkState(ChunkId id) const
{
    const auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return ChunkState::Unloaded;
    }

    return iterator->second.state;
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const CreateObjectCommand& command)
{
    WorldObjectRecord record = command.record;
    if (record.persistent_id.IsValid() && persistent_to_runtime_.contains(record.persistent_id))
    {
        return foundation::Result<WorldCommandResult>::Failure(
            MakeWorldError("world.duplicate_persistent_object", "persistent object id is already registered"));
    }

    const RuntimeObjectId runtime_id{next_runtime_object_value_++};
    record.runtime_id = runtime_id;
    record.revision = 1;
    const auto invariant = ValidateWorldObjectInvariant(record, *this, *this, *this);
    if (!invariant)
    {
        --next_runtime_object_value_;
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    world_objects_[runtime_id] = record;
    if (record.persistent_id.IsValid())
    {
        persistent_to_runtime_[record.persistent_id] = runtime_id;
    }
    return foundation::Result<WorldCommandResult>::Success(WorldCommandResult{runtime_id, std::nullopt, record});
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const ChangePlacementCommand& command)
{
    auto iterator = world_objects_.find(command.runtime_id);
    if (iterator == world_objects_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found", "world object was not found for placement update");
    }

    const auto revision = CheckExpectedRevision(iterator->second, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }

    WorldCommandResult result{command.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord candidate = iterator->second;
    candidate.placement = command.placement;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    iterator->second = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const ChangeResidencyCommand& command)
{
    auto iterator = world_objects_.find(command.runtime_id);
    if (iterator == world_objects_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found", "world object was not found for residency update");
    }

    const auto revision = CheckExpectedRevision(iterator->second, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }
    if (!CanTransition(iterator->second.residency, command.residency))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_residency_transition",
                                                     "requested residency transition is not allowed");
    }

    WorldCommandResult result{command.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord candidate = iterator->second;
    candidate.residency = command.residency;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    iterator->second = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const PromotePersistenceTierCommand& command)
{
    auto iterator = world_objects_.find(command.runtime_id);
    if (iterator == world_objects_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found", "world object was not found for persistence update");
    }

    const auto revision = CheckExpectedRevision(iterator->second, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }
    if (!CanPromotePersistenceTier(iterator->second.persistence_tier, command.tier))
    {
        return WorldFailureValue<WorldCommandResult>("world.forbidden_persistence_downgrade",
                                                     "persistence tier cannot be downgraded by promotion command");
    }

    WorldCommandResult result{command.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord candidate = iterator->second;
    candidate.persistence_tier = command.tier;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    iterator->second = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const MaterializeObjectCommand& command)
{
    if (!command.request.persistent_id.IsValid())
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_persistent_object",
                                                     "persistent object id must be valid before materialization");
    }

    const auto index = persistent_to_runtime_.find(command.request.persistent_id);
    if (index == persistent_to_runtime_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found",
                                                     "world object with the requested persistent id was not found");
    }

    WorldObjectRecord& object = world_objects_.at(index->second);
    const auto revision = CheckExpectedRevision(object, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }
    if (!CanPromoteReality(object.reality, command.request.target_reality))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_reality_promotion",
                                                     "materialization cannot target a less concrete reality level");
    }

    WorldCommandResult result{index->second, object, std::nullopt};
    WorldObjectRecord candidate = object;
    candidate.reality = command.request.target_reality;
    candidate.residency = ResidencyForReality(command.request.target_reality);
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    object = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const DemoteObjectCommand& command)
{
    auto iterator = world_objects_.find(command.request.runtime_id);
    if (iterator == world_objects_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found", "world object was not found for demotion");
    }

    const auto revision = CheckExpectedRevision(iterator->second, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }
    if (!CanDemoteReality(iterator->second.reality, command.request.target_reality))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_reality_demotion",
                                                     "demotion cannot target a more concrete reality level");
    }
    if (!command.request.commit_token.IsValid() || command.request.commit_token.object != command.request.runtime_id ||
        command.request.commit_token.target_reality != command.request.target_reality ||
        command.request.commit_token.source_revision != iterator->second.revision)
    {
        return WorldFailureValue<WorldCommandResult>("world.demotion_not_confirmed",
                                                     "demotion requires a matching collapse commit token");
    }

    WorldCommandResult result{command.request.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord candidate = iterator->second;
    candidate.reality = command.request.target_reality;
    candidate.residency = ResidencyForReality(command.request.target_reality);
    candidate.placement = command.request.commit_token.collapsed_placement;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    iterator->second = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<WorldCommandResult> WorldRuntime::Apply(const DestroyObjectCommand& command)
{
    if (!command.runtime_id.IsValid())
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_object", "runtime object id must be valid before destruction");
    }

    auto iterator = world_objects_.find(command.runtime_id);
    if (iterator == world_objects_.end())
    {
        return WorldFailureValue<WorldCommandResult>("world.object_not_found", "world object was not found for destruction");
    }

    const auto revision = CheckExpectedRevision(iterator->second, command.expected_revision);
    if (!revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(revision.GetError());
    }

    WorldCommandResult result{command.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord& object = iterator->second;
    if (!object.persistent_id.IsValid() && object.persistence_tier == PersistenceTier::Disposable)
    {
        world_objects_.erase(iterator);
        return foundation::Result<WorldCommandResult>::Success(std::move(result));
    }

    WorldObjectRecord candidate = object;
    candidate.placement = DestroyedPlacement{command.destroyed_at, command.reason};
    candidate.reality = ObjectRealityLevel::Logical;
    candidate.residency = ResidencyState::Unloaded;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    object = candidate;
    result.after = candidate;
    return foundation::Result<WorldCommandResult>::Success(std::move(result));
}

foundation::Result<RuntimeObjectId> WorldRuntime::CreateObject(WorldObjectRecord record)
{
    const auto result = Apply(CreateObjectCommand{std::move(record)});
    if (!result)
    {
        return foundation::Result<RuntimeObjectId>::Failure(result.GetError());
    }
    return foundation::Result<RuntimeObjectId>::Success(result.Value().runtime_id);
}

foundation::Result<void> WorldRuntime::DestroyObject(RuntimeObjectId id)
{
    const auto result = Apply(DestroyObjectCommand{id});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

std::optional<WorldObjectRecord> WorldRuntime::FindObject(RuntimeObjectId id) const
{
    const auto iterator = world_objects_.find(id);
    if (iterator == world_objects_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<WorldObjectRecord> WorldRuntime::FindObjectsInRegion(RegionId region) const
{
    std::vector<WorldObjectRecord> matches;
    for (const auto& [runtime_id, object] : world_objects_)
    {
        (void)runtime_id;
        const auto placement_region = GetPlacementRegion(object.placement);
        if (placement_region && *placement_region == region)
        {
            matches.push_back(object);
        }
    }

    SortByRuntimeId(matches);
    return matches;
}

std::vector<WorldObjectRecord> WorldRuntime::FindObjectsInChunk(ChunkId chunk) const
{
    std::vector<WorldObjectRecord> matches;
    for (const auto& [runtime_id, object] : world_objects_)
    {
        (void)runtime_id;
        const auto placement_chunk = GetPlacementChunk(object.placement);
        if (placement_chunk && *placement_chunk == chunk)
        {
            matches.push_back(object);
        }
    }

    SortByRuntimeId(matches);
    return matches;
}

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

    SortByRuntimeId(matches);
    return matches;
}

foundation::Result<void> WorldRuntime::SetPlacement(RuntimeObjectId id, ObjectPlacement placement)
{
    const auto result = Apply(ChangePlacementCommand{id, 0, std::move(placement)});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> WorldRuntime::SetResidency(RuntimeObjectId id, ResidencyState state)
{
    const auto result = Apply(ChangeResidencyCommand{id, 0, state});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<RuntimeObjectId> WorldRuntime::Materialize(const MaterializationRequest& request)
{
    const auto result = Apply(MaterializeObjectCommand{request});
    if (!result)
    {
        return foundation::Result<RuntimeObjectId>::Failure(result.GetError());
    }
    return foundation::Result<RuntimeObjectId>::Success(result.Value().runtime_id);
}

foundation::Result<void> WorldRuntime::Demote(const DemotionRequest& request)
{
    const auto result = Apply(DemoteObjectCommand{request});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> WorldRuntime::SetChunkState(ChunkId id, ChunkState state)
{
    auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.chunk_not_found", "chunk was not found for state update"));
    }

    iterator->second.state = state;
    return foundation::Result<void>::Success();
}
} 
