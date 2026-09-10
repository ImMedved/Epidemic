#include "world_runtime_impl.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/World/world_invariants.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
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

[[nodiscard]] foundation::Result<std::uint64_t> PeekMonotonicId(std::uint64_t next_value,
                                                                 std::string_view code,
                                                                 std::string_view message)
{
    if (next_value == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(MakeWorldError(code, message));
    }
    return foundation::Result<std::uint64_t>::Success(next_value);
}

void CommitMonotonicId(std::uint64_t& next_value) noexcept
{
    next_value = next_value == std::numeric_limits<std::uint64_t>::max() ? 0 : next_value + 1;
}

[[nodiscard]] foundation::Result<std::uint64_t> NextObjectRevision(const WorldObjectRecord& object)
{
    if (object.revision == std::numeric_limits<std::uint64_t>::max())
    {
        return foundation::Result<std::uint64_t>::Failure(
            MakeWorldError("world.revision_overflow", "world object revision cannot advance beyond UINT64_MAX"));
    }
    return foundation::Result<std::uint64_t>::Success(object.revision + 1);
}
} // namespace

foundation::Result<void> WorldRuntime::RegisterRegion(RegionDescriptor region)
{
    if (topology_frozen_)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.topology_frozen", "region/chunk topology registry is frozen"));
    }
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

void WorldRuntime::Freeze() noexcept
{
    topology_frozen_ = true;
}

bool WorldRuntime::IsFrozen() const noexcept
{
    return topology_frozen_;
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
    if (topology_frozen_)
    {
        return foundation::Result<void>::Failure(MakeWorldError("world.topology_frozen", "region/chunk topology registry is frozen"));
    }
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

    const auto runtime_value = PeekMonotonicId(next_runtime_object_value_, "world.runtime_object_id_exhausted",
                                                "runtime object id allocator is exhausted");
    if (!runtime_value)
    {
        return foundation::Result<WorldCommandResult>::Failure(runtime_value.GetError());
    }
    const RuntimeObjectId runtime_id{runtime_value.Value()};
    record.runtime_id = runtime_id;
    record.revision = 1;
    const auto invariant = ValidateWorldObjectInvariant(record, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }

    try
    {
        const auto [object_iterator, object_inserted] = world_objects_.emplace(runtime_id, record);
        if (!object_inserted)
        {
            return foundation::Result<WorldCommandResult>::Failure(
                MakeWorldError("world.duplicate_runtime_object", "allocated runtime object id already exists"));
        }

        if (record.persistent_id.IsValid())
        {
            try
            {
                if (fail_next_persistent_index_publication_for_testing_)
                {
                    fail_next_persistent_index_publication_for_testing_ = false;
                    throw std::bad_alloc{};
                }
                const auto [_, persistent_inserted] = persistent_to_runtime_.emplace(record.persistent_id, runtime_id);
                if (!persistent_inserted)
                {
                    world_objects_.erase(object_iterator);
                    return foundation::Result<WorldCommandResult>::Failure(
                        MakeWorldError("world.duplicate_persistent_object", "persistent object id is already registered"));
                }
            }
            catch (...)
            {
                world_objects_.erase(object_iterator);
                return foundation::Result<WorldCommandResult>::Failure(
                    MakeWorldError("world.allocation_failed", "failed to publish persistent object index"));
            }
        }
    }
    catch (...)
    {
        return foundation::Result<WorldCommandResult>::Failure(
            MakeWorldError("world.allocation_failed", "failed to publish world object"));
    }

    CommitMonotonicId(next_runtime_object_value_);
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
    const auto next_revision = NextObjectRevision(iterator->second);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    WorldObjectRecord candidate = iterator->second;
    candidate.placement = command.placement;
    candidate.revision = next_revision.Value();
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
    if (!IsValidResidencyState(command.residency))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_state_enum", "residency enum value is outside the supported domain");
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
    const auto next_revision = NextObjectRevision(iterator->second);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    WorldObjectRecord candidate = iterator->second;
    candidate.residency = command.residency;
    candidate.revision = next_revision.Value();
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
    if (!IsValidPersistenceTier(command.tier))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_state_enum", "persistence tier enum value is outside the supported domain");
    }
    if (!CanPromotePersistenceTier(iterator->second.persistence_tier, command.tier))
    {
        return WorldFailureValue<WorldCommandResult>("world.forbidden_persistence_downgrade",
                                                     "persistence tier cannot be downgraded by promotion command");
    }
    if (IsDestroyed(iterator->second))
    {
        return WorldFailureValue<WorldCommandResult>("world.destroyed_terminal", "destroyed objects cannot change persistence tier");
    }

    WorldCommandResult result{command.runtime_id, iterator->second, std::nullopt};
    WorldObjectRecord candidate = iterator->second;
    if (command.persistent_id)
    {
        if (!command.persistent_id->IsValid())
        {
            return WorldFailureValue<WorldCommandResult>("world.invalid_persistent_object", "persistent id must be valid for persistent tier promotion");
        }
        if (candidate.persistent_id.IsValid() && candidate.persistent_id != *command.persistent_id)
        {
            return WorldFailureValue<WorldCommandResult>("world.persistent_id_immutable", "persistent object identity cannot be renamed during promotion");
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
    const auto next_revision = NextObjectRevision(iterator->second);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    candidate.persistence_tier = command.tier;
    candidate.revision = next_revision.Value();
    const auto invariant = ValidateWorldObjectInvariant(candidate, *this, *this, *this);
    if (!invariant)
    {
        return foundation::Result<WorldCommandResult>::Failure(invariant.GetError());
    }

    bool published_new_index = false;
    if (candidate.persistent_id.IsValid() && !persistent_to_runtime_.contains(candidate.persistent_id))
    {
        try
        {
            if (fail_next_persistent_index_publication_for_testing_)
            {
                fail_next_persistent_index_publication_for_testing_ = false;
                throw std::bad_alloc{};
            }
            const auto [_, inserted] = persistent_to_runtime_.emplace(candidate.persistent_id, command.runtime_id);
            if (!inserted)
            {
                return WorldFailureValue<WorldCommandResult>("world.duplicate_persistent_object", "persistent object id is already registered");
            }
            published_new_index = true;
        }
        catch (...)
        {
            return WorldFailureValue<WorldCommandResult>("world.allocation_failed", "failed to publish persistent object index");
        }
    }

    try
    {
        iterator->second = candidate;
    }
    catch (...)
    {
        if (published_new_index)
        {
            persistent_to_runtime_.erase(candidate.persistent_id);
        }
        return WorldFailureValue<WorldCommandResult>("world.allocation_failed", "failed to publish world object persistence state");
    }
    InvalidateDemotionTokensForObject(command.runtime_id);
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

    if (!IsValidObjectRealityLevel(command.request.target_reality))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_state_enum", "materialization target reality enum is outside the supported domain");
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
    const auto next_revision = NextObjectRevision(object);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    WorldObjectRecord candidate = object;
    candidate.reality = command.request.target_reality;
    candidate.residency = ResidencyForReality(command.request.target_reality);
    candidate.revision = next_revision.Value();
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
    if (!IsValidObjectRealityLevel(command.request.target_reality))
    {
        return WorldFailureValue<WorldCommandResult>("world.invalid_state_enum", "demotion target reality enum is outside the supported domain");
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
    const auto next_revision = NextObjectRevision(iterator->second);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    WorldObjectRecord candidate = iterator->second;
    candidate.reality = command.request.target_reality;
    candidate.residency = ResidencyForReality(command.request.target_reality);
    candidate.placement = command.request.commit_token.collapsed_placement;
    candidate.revision = next_revision.Value();
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

    const auto next_revision = NextObjectRevision(object);
    if (!next_revision)
    {
        return foundation::Result<WorldCommandResult>::Failure(next_revision.GetError());
    }
    WorldObjectRecord candidate = object;
    candidate.placement = DestroyedPlacement{command.destroyed_at, command.reason};
    candidate.reality = ObjectRealityLevel::Logical;
    candidate.residency = ResidencyState::Unloaded;
    candidate.revision = next_revision.Value();
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
    if (!IsValidObjectRealityLevel(snapshot.target_reality))
    {
        return foundation::Result<DemotionCommitToken>::Failure(MakeWorldError("world.invalid_state_enum", "demotion target reality enum is outside the supported domain"));
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
    const auto token_value = PeekMonotonicId(next_demotion_token_value_, "world.demotion_token_exhausted", "demotion token id allocator is exhausted");
    if (!token_value)
    {
        return foundation::Result<DemotionCommitToken>::Failure(token_value.GetError());
    }

    DemotionCommitToken token{};
    token.token_id = token_value.Value();
    token.object = snapshot.object;
    token.target_reality = snapshot.target_reality;
    token.collapsed_placement = snapshot.collapsed_placement;
    token.collapse_record_id = snapshot.collapse_record_id;
    token.source_revision = snapshot.source_revision;
    try
    {
        if (fail_next_demotion_token_publication_for_testing_)
        {
            fail_next_demotion_token_publication_for_testing_ = false;
            throw std::bad_alloc{};
        }
        const auto [_, inserted] = issued_demotion_tokens_.emplace(token.token_id, token);
        if (!inserted)
        {
            return foundation::Result<DemotionCommitToken>::Failure(
                MakeWorldError("world.duplicate_demotion_token", "allocated demotion token id already exists"));
        }
    }
    catch (...)
    {
        return foundation::Result<DemotionCommitToken>::Failure(
            MakeWorldError("world.allocation_failed", "failed to publish demotion commit token"));
    }
    CommitMonotonicId(next_demotion_token_value_);
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
