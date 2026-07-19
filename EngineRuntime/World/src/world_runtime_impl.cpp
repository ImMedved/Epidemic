#include "world_runtime_impl.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/World/world_invariants.h"

#include <algorithm>
#include <limits>
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
    if (expected_revision == 0)
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.expected_revision_required", "public world commands must declare an expected object revision"));
    }
    if (expected_revision != 0 && object.revision != expected_revision)
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.revision_conflict", "world object revision does not match command expectation"));
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] bool IsDestroyed(const WorldObjectRecord& object) noexcept
{
    return std::holds_alternative<DestroyedPlacement>(object.placement);
}

[[nodiscard]] bool IsDependentPlacement(const ObjectPlacement& placement, RuntimeObjectId owner) noexcept
{
    if (const auto* container = std::get_if<ContainerPlacement>(&placement))
    {
        return container->container == owner;
    }
    if (const auto* inventory = std::get_if<InventoryPlacement>(&placement))
    {
        return inventory->owner == owner;
    }
    if (const auto* equipped = std::get_if<EquippedPlacement>(&placement))
    {
        return equipped->owner == owner;
    }
    return false;
}

[[nodiscard]] bool RequiresPersistentId(PersistenceTier tier) noexcept
{
    return PersistenceTierRank(tier) >= PersistenceTierRank(PersistenceTier::PlayerTouched);
}

[[nodiscard]] bool SupportsPhysicalMaterialization(const ObjectPlacement& placement) noexcept
{
    return std::holds_alternative<WorldSurfacePlacement>(placement);
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

foundation::Result<ChunkSnapshot> WorldRuntime::GetChunkSnapshot(ChunkId id) const
{
    const auto iterator = chunks_.find(id);
    if (iterator == chunks_.end())
    {
        return WorldFailureValue<ChunkSnapshot>("world.chunk_not_found", "chunk was not found");
    }

    return foundation::Result<ChunkSnapshot>::Success(ChunkSnapshot{iterator->second.descriptor, iterator->second.state, iterator->second.revision});
}

foundation::Result<void> WorldRuntime::SetChunkState(ChangeChunkStateCommand command)
{
    auto iterator = chunks_.find(command.chunk);
    if (iterator == chunks_.end())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.chunk_not_found", "chunk was not found for state update"));
    }
    if (command.expected_revision == 0)
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.expected_revision_required", "chunk state commands must declare an expected chunk revision"));
    }
    if (command.expected_revision != iterator->second.revision)
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.revision_conflict", "chunk revision does not match command expectation"));
    }
    if (!CanTransition(iterator->second.state, command.state))
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.invalid_chunk_transition", "chunk state transition is not allowed"));
    }
    if (iterator->second.state == command.state)
    {
        return foundation::Result<void>::Success();
    }
    if (iterator->second.revision == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<void>::Failure(
            MakeWorldError("world.revision_overflow", "chunk revision cannot advance beyond UINT64_MAX"));
    }

    iterator->second.state = command.state;
    ++iterator->second.revision;
    return foundation::Result<void>::Success();
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
    if (IsDestroyed(iterator->second))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot be moved");
    }
    if (std::holds_alternative<DestroyedPlacement>(command.placement))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_requires_destroy_command",
                                                     "destroyed placement may only be created by destroy command");
    }
    WorldObjectRecord candidate = iterator->second;
    candidate.placement = command.placement;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    InvalidateDemotionTokensForObject(command.runtime_id);
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
    if (IsDestroyed(iterator->second))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot change residency");
    }
    WorldObjectRecord candidate = iterator->second;
    candidate.residency = command.residency;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    InvalidateDemotionTokensForObject(command.runtime_id);
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
    if (IsDestroyed(iterator->second))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot change persistence tier");
    }
    WorldObjectRecord candidate = iterator->second;
    if (command.persistent_id)
    {
        if (!command.persistent_id->IsValid())
        {
            return WorldFailureValue<WorldCommandResult>("world.invalid_persistent_object", "persistent id must be valid for persistent tier promotion");
        }
        const auto existing = persistent_to_runtime_.find(*command.persistent_id);
        if (existing != persistent_to_runtime_.end() && existing->second != command.runtime_id)
        {
            return WorldFailureValue<WorldCommandResult>("world.duplicate_persistent_object", "persistent object id is already registered");
        }
        candidate.persistent_id = *command.persistent_id;
    }
    if (RequiresPersistentId(command.tier) && !candidate.persistent_id.IsValid())
    {
        return WorldFailureValue<WorldCommandResult>("world.persistent_id_required", "persistent tier promotion requires a persistent object id");
    }
    candidate.persistence_tier = command.tier;
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    InvalidateDemotionTokensForObject(command.runtime_id);
    if (candidate.persistent_id.IsValid())
    {
        persistent_to_runtime_[candidate.persistent_id] = command.runtime_id;
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
    if (IsDestroyed(object))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot be materialized");
    }
    if (command.request.target_reality == ObjectRealityLevel::Physical && !SupportsPhysicalMaterialization(object.placement))
    {
        return WorldFailureValue<WorldCommandResult>("world.materialization_placement_incompatible",
                                                     "physical materialization requires a physical-compatible placement");
    }
    WorldObjectRecord candidate = object;
    candidate.reality = command.request.target_reality;
    candidate.residency = ResidencyForReality(command.request.target_reality);
    ++candidate.revision;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }
    InvalidateDemotionTokensForObject(index->second);
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
    const auto issued_token = issued_demotion_tokens_.find(command.request.commit_token.token_id);
    if (IsDestroyed(iterator->second))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot be demoted");
    }
    if (!command.request.commit_token.IsValid() || issued_token == issued_demotion_tokens_.end() ||
        issued_token->second != command.request.commit_token || command.request.commit_token.object != command.request.runtime_id ||
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
    issued_demotion_tokens_.erase(issued_token);
    InvalidateDemotionTokensForObject(command.request.runtime_id);
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
    if (IsDestroyed(object))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects are terminal");
    }
    std::vector<RuntimeObjectId> dependents;
    for (const auto& [runtime_id, candidate] : world_objects_)
    {
        if (runtime_id != command.runtime_id && IsDependentPlacement(candidate.placement, command.runtime_id))
        {
            dependents.push_back(runtime_id);
        }
    }
    if (!dependents.empty())
    {
        return WorldFailureValue<WorldCommandResult>("world.dependent_objects_exist", "object cannot be destroyed while dependent placements exist");
    }
    if (!object.persistent_id.IsValid() && object.persistence_tier == PersistenceTier::Disposable)
    {
        InvalidateDemotionTokensForObject(command.runtime_id);
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
    InvalidateDemotionTokensForObject(command.runtime_id);
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
    const auto object = FindObject(id);
    if (!object)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.object_not_found", "world object was not found for destruction"));
    }
    const auto result = Apply(DestroyObjectCommand{id, object->revision});
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
    const auto object = FindObject(id);
    if (!object)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.object_not_found", "world object was not found for placement update"));
    }
    const auto result = Apply(ChangePlacementCommand{id, object->revision, std::move(placement)});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> WorldRuntime::SetResidency(RuntimeObjectId id, ResidencyState state)
{
    const auto object = FindObject(id);
    if (!object)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.object_not_found", "world object was not found for residency update"));
    }
    const auto result = Apply(ChangeResidencyCommand{id, object->revision, state});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<RuntimeObjectId> WorldRuntime::Materialize(const MaterializationRequest& request)
{
    const auto index = persistent_to_runtime_.find(request.persistent_id);
    if (index == persistent_to_runtime_.end())
    {
        return foundation::Result<RuntimeObjectId>::Failure(MakeWorldError("world.object_not_found", "world object with the requested persistent id was not found"));
    }
    const auto object = FindObject(index->second);
    if (!object)
    {
        return foundation::Result<RuntimeObjectId>::Failure(MakeWorldError("world.object_not_found", "world object was not found for materialization"));
    }
    const auto result = Apply(MaterializeObjectCommand{request, object->revision});
    if (!result)
    {
        return foundation::Result<RuntimeObjectId>::Failure(result.GetError());
    }
    return foundation::Result<RuntimeObjectId>::Success(result.Value().runtime_id);
}

foundation::Result<void> WorldRuntime::Demote(const DemotionRequest& request)
{
    const auto object = FindObject(request.runtime_id);
    if (!object)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.object_not_found", "world object was not found for demotion"));
    }
    const auto result = Apply(DemoteObjectCommand{request, object->revision});
    if (!result)
    {
        return foundation::Result<void>::Failure(result.GetError());
    }
    return foundation::Result<void>::Success();
}

foundation::Result<DemotionCommitToken> WorldRuntime::IssueDemotionCommitToken(DemotionSnapshot snapshot)
{
    const auto object = FindObject(snapshot.object);
    if (!object)
    {
        return foundation::Result<DemotionCommitToken>::Failure(MakeWorldError("world.object_not_found", "world object was not found for demotion token"));
    }
    if (snapshot.source_revision != object->revision || !snapshot.collapse_record_id.IsValid() ||
        !CanDemoteReality(object->reality, snapshot.target_reality))
    {
        return foundation::Result<DemotionCommitToken>::Failure(MakeWorldError("world.invalid_demotion_snapshot", "demotion snapshot must match current object revision"));
    }
    WorldObjectRecord candidate = *object;
    candidate.reality = snapshot.target_reality;
    candidate.residency = ResidencyForReality(snapshot.target_reality);
    candidate.placement = snapshot.collapsed_placement;
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<DemotionCommitToken>::Failure(invariant.GetError());
    }
    if (next_demotion_token_value_ == 0 || next_demotion_token_value_ == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<DemotionCommitToken>::Failure(MakeWorldError("world.demotion_token_overflow", "demotion token id cannot advance beyond UINT64_MAX"));
    }

    DemotionCommitToken token{};
    token.token_id = next_demotion_token_value_++;
    token.object = snapshot.object;
    token.target_reality = snapshot.target_reality;
    token.collapsed_placement = snapshot.collapsed_placement;
    token.collapse_record_id = snapshot.collapse_record_id;
    token.source_revision = snapshot.source_revision;
    issued_demotion_tokens_[token.token_id] = token;
    return foundation::Result<DemotionCommitToken>::Success(token);
}

foundation::Result<void> WorldRuntime::RevokeDemotionCommitToken(std::uint64_t token_id)
{
    if (token_id == 0)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.invalid_demotion_token", "demotion token id must be valid before revoke"));
    }
    if (issued_demotion_tokens_.erase(token_id) == 0)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.demotion_token_not_found", "demotion token was not found for revoke"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> WorldRuntime::SetChunkState(ChunkId id, ChunkState state)
{
    const auto snapshot = GetChunkSnapshot(id);
    if (!snapshot)
    {
        return foundation::Result<void>::Failure(snapshot.GetError());
    }
    return SetChunkState(ChangeChunkStateCommand{id, snapshot.Value().revision, state});
}

void WorldRuntime::InvalidateDemotionTokensForObject(RuntimeObjectId object)
{
    for (auto iterator = issued_demotion_tokens_.begin(); iterator != issued_demotion_tokens_.end();)
    {
        if (iterator->second.object == object)
        {
            iterator = issued_demotion_tokens_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}
} 
