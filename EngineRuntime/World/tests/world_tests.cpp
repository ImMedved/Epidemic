#include "world_runtime_impl.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_budget.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"
#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/object_materialization.h"
#include "Epidemic/Runtime/World/object_placement.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_location.h"
#include "Epidemic/Runtime/World/world_object.h"
#include "Epidemic/Runtime/World/world_invariants.h"
#include "Epidemic/Runtime/World/world_query.h"
#include "Epidemic/Runtime/World/world_services.h"
#include "Epidemic/Runtime/World/world_state.h"

#include <type_traits>

namespace
{
using epidemic::foundation::StringId;
using epidemic::runtime::AssetId;
using epidemic::runtime::ChunkDescriptor;
using epidemic::runtime::ChunkId;
using epidemic::runtime::ChunkState;
using epidemic::runtime::DemotionRequest;
using epidemic::runtime::DemotionCommitToken;
using epidemic::runtime::DemotionSnapshot;
using epidemic::runtime::DestroyObjectCommand;
using epidemic::runtime::EquippedPlacement;
using epidemic::runtime::GameTimePoint;
using epidemic::runtime::InventoryPlacement;
using epidemic::runtime::MaterializationRequest;
using epidemic::runtime::ObjectPlacement;
using epidemic::runtime::ObjectRealityLevel;
using epidemic::runtime::PersistenceTier;
using epidemic::runtime::PersistentObjectId;
using epidemic::runtime::RegionDescriptor;
using epidemic::runtime::RegionId;
using epidemic::runtime::ResidencyState;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::WorldSurfacePlacement;
using epidemic::runtime::ContainerPlacement;
using epidemic::runtime::DestroyedPlacement;
using epidemic::runtime::HiddenPlacement;
using epidemic::runtime::Transform;
using epidemic::runtime::CreateWorldServices;
using epidemic::runtime::WorldLocation;
using epidemic::runtime::WorldObjectRecord;
using epidemic::runtime::WorldRuntime;
using epidemic::runtime::ChangePlacementCommand;
using epidemic::runtime::ChangeChunkStateCommand;
using epidemic::runtime::ChangeResidencyCommand;
using epidemic::runtime::CreateObjectCommand;
using epidemic::runtime::PromotePersistenceTierCommand;
using epidemic::runtime::ValidateRealityResidencyCombination;

bool RegisterRegionAndChunk(WorldRuntime& runtime, RegionId region, ChunkId chunk)
{
    return runtime.RegisterRegion(RegionDescriptor{region, StringId::FromString("test-region")}).HasValue() &&
           runtime.RegisterChunk(ChunkDescriptor{chunk, region, 0, 0, 0}).HasValue();
}

// Verifies register and find region.
bool TestRegisterAndFindRegion()
{
    WorldRuntime runtime;
    const RegionDescriptor region{RegionId{1}, StringId::FromString("test-region")};
    const auto result = runtime.RegisterRegion(region);
    const auto stored = runtime.FindRegion(region.id);

    return result.HasValue() && stored.has_value() && stored.value() == region;
}

// Verifies duplicate region ids are rejected.
bool TestDuplicateRegionIsRejected()
{
    WorldRuntime runtime;
    const RegionDescriptor region{RegionId{1}, StringId::FromString("test-region")};
    const auto first = runtime.RegisterRegion(region);
    const auto duplicate = runtime.RegisterRegion(region);

    return first.HasValue() && !duplicate.HasValue() && duplicate.GetError().HasCode("world.duplicate_region");
}

// Verifies register and find chunk.
bool TestRegisterAndFindChunk()
{
    WorldRuntime runtime;
    const auto region_result = runtime.RegisterRegion(RegionDescriptor{RegionId{2}, StringId::FromString("coast")});
    const ChunkDescriptor chunk{ChunkId{11}, RegionId{2}, 3, 4, 5};
    const auto chunk_result = runtime.RegisterChunk(chunk);
    const auto stored = runtime.FindChunk(chunk.id);

    return region_result.HasValue() && chunk_result.HasValue() && stored.has_value() && stored.value() == chunk;
}

// Verifies chunks must reference existing regions and ids stay unique.
bool TestChunkRegistrationValidation()
{
    WorldRuntime runtime;
    const auto missing_region = runtime.RegisterChunk(ChunkDescriptor{ChunkId{12}, RegionId{99}, 0, 0, 0});
    const auto region = runtime.RegisterRegion(RegionDescriptor{RegionId{4}, StringId::FromString("forest")});
    const auto first = runtime.RegisterChunk(ChunkDescriptor{ChunkId{12}, RegionId{4}, 0, 0, 0});
    const auto duplicate = runtime.RegisterChunk(ChunkDescriptor{ChunkId{12}, RegionId{4}, 1, 0, 0});

    return !missing_region.HasValue() && missing_region.GetError().HasCode("world.region_not_found") &&
           region.HasValue() && first.HasValue() && !duplicate.HasValue() &&
           duplicate.GetError().HasCode("world.duplicate_chunk");
}

// Verifies chunk state snapshots are revisioned and transitions are validated.
bool TestChunkStateDefaultsToUnloadedAndCanChange()
{
    WorldRuntime runtime;
    const auto region_result = runtime.RegisterRegion(RegionDescriptor{RegionId{3}, StringId::FromString("mountain")});
    const auto chunk_result = runtime.RegisterChunk(ChunkDescriptor{ChunkId{15}, RegionId{3}, 0, 0, 0});
    if (!region_result.HasValue() || !chunk_result.HasValue())
    {
        return false;
    }

    const auto initial = runtime.GetChunkSnapshot(ChunkId{15});
    const auto unknown = runtime.GetChunkSnapshot(ChunkId{999});
    if (!initial || initial.Value().state != ChunkState::Unloaded || initial.Value().revision != 1u ||
        unknown || !unknown.GetError().HasCode("world.chunk_not_found"))
    {
        return false;
    }

    const auto invalid_transition = runtime.SetChunkState(ChangeChunkStateCommand{ChunkId{15}, initial.Value().revision, ChunkState::Active});
    const auto loading = runtime.SetChunkState(ChangeChunkStateCommand{ChunkId{15}, initial.Value().revision, ChunkState::Loading});
    const auto stale = runtime.SetChunkState(ChangeChunkStateCommand{ChunkId{15}, initial.Value().revision, ChunkState::Resident});
    const auto loaded = runtime.GetChunkSnapshot(ChunkId{15});
    if (invalid_transition || !invalid_transition.GetError().HasCode("world.invalid_chunk_transition") ||
        !loading || stale || !stale.GetError().HasCode("world.revision_conflict") ||
        !loaded || loaded.Value().state != ChunkState::Loading)
    {
        return false;
    }

    const auto resident = runtime.SetChunkState(ChangeChunkStateCommand{ChunkId{15}, loaded.Value().revision, ChunkState::Resident});
    const auto resident_snapshot = runtime.GetChunkSnapshot(ChunkId{15});
    const auto active = resident_snapshot ? runtime.SetChunkState(ChangeChunkStateCommand{ChunkId{15}, resident_snapshot.Value().revision, ChunkState::Active})
                                          : epidemic::foundation::Result<void>::Failure(epidemic::foundation::Error::Create("test.missing_snapshot", "missing"));
    const auto active_snapshot = runtime.GetChunkSnapshot(ChunkId{15});
    return resident && resident_snapshot && active && active_snapshot && active_snapshot.Value().state == ChunkState::Active;
}

// Verifies create object returns valid runtime id.
bool TestCreateObjectReturnsValidRuntimeId()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.asset_id = AssetId::FromString("items/rope");
    const auto created = runtime.CreateObject(record);

    return created.HasValue() && created.Value().IsValid();
}

// Verifies find object returns snapshot.
bool TestFindObjectReturnsSnapshot()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.asset_id = AssetId::FromString("items/apple");
    record.persistent_id = PersistentObjectId{140};
    record.persistence_tier = PersistenceTier::PlayerTouched;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto stored = runtime.FindObject(created.Value());
    return stored.has_value() && stored->runtime_id == created.Value() &&
           stored->persistence_tier == PersistenceTier::PlayerTouched && stored->revision == 1;
}

// Verifies set placement updates placement.
bool TestSetPlacementUpdatesPlacement()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{4}, ChunkId{9}))
    {
        return false;
    }
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }
    const ObjectPlacement placement{WorldSurfacePlacement{RegionId{4}, ChunkId{9}, Transform{}}};
    const auto updated = runtime.SetPlacement(created.Value(), placement);
    const auto stored = runtime.FindObject(created.Value());

    return updated.HasValue() && stored.has_value() && stored->placement == placement && stored->revision == 2;
}

// Verifies invalid placement is rejected before mutation.
bool TestInvalidPlacementIsRejected()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const ObjectPlacement invalid_world{WorldSurfacePlacement{{}, ChunkId{9}, Transform{}}};
    const ObjectPlacement invalid_container{ContainerPlacement{RuntimeObjectId{}, {}}};
    const auto world_result = runtime.SetPlacement(created.Value(), invalid_world);
    const auto container_result = runtime.SetPlacement(created.Value(), invalid_container);
    const auto stored = runtime.FindObject(created.Value());

    return !world_result.HasValue() && world_result.GetError().HasCode("world.invalid_placement") &&
           !container_result.HasValue() && stored.has_value() && stored->revision == 1;
}

bool TestDuplicatePersistentIdIsRejected()
{
    WorldRuntime runtime;
    WorldObjectRecord first{};
    first.persistent_id = PersistentObjectId{44};
    first.persistence_tier = PersistenceTier::TemporaryObserved;
    WorldObjectRecord second{};
    second.persistent_id = PersistentObjectId{44};
    second.persistence_tier = PersistenceTier::TemporaryObserved;

    const auto created = runtime.CreateObject(first);
    const auto duplicate = runtime.CreateObject(second);
    return created.HasValue() && !duplicate.HasValue() &&
           duplicate.GetError().HasCode("world.duplicate_persistent_object");
}

// Verifies set residency updates state.
bool TestSetResidencyUpdatesState()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto updated = runtime.SetResidency(created.Value(), ResidencyState::Resident);
    const auto stored = runtime.FindObject(created.Value());

    return updated.HasValue() && stored.has_value() && stored->residency == ResidencyState::Resident &&
           stored->revision == 2;
}

bool TestInvalidResidencyTransitionIsRejected()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto invalid = runtime.SetResidency(created.Value(), ResidencyState::Active);
    const auto stored = runtime.FindObject(created.Value());
    return !invalid.HasValue() && invalid.GetError().HasCode("world.invalid_residency_transition") &&
           stored.has_value() && stored->residency == ResidencyState::Unloaded && stored->revision == 1;
}

bool TestCommandRevisionConflictIsRejected()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto conflict = runtime.Apply(ChangeResidencyCommand{created.Value(), 99, ResidencyState::Resident});
    const auto stored = runtime.FindObject(created.Value());
    return !conflict.HasValue() && conflict.GetError().HasCode("world.revision_conflict") &&
           stored.has_value() && stored->revision == 1 && stored->residency == ResidencyState::Unloaded;
}

bool TestCommandReturnsBeforeAndAfterSnapshots()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{8}, ChunkId{16}))
    {
        return false;
    }
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const ObjectPlacement placement{WorldSurfacePlacement{RegionId{8}, ChunkId{16}, Transform{}}};
    const auto changed = runtime.Apply(ChangePlacementCommand{created.Value(), 1, placement});
    return changed.HasValue() && changed.Value().before.has_value() && changed.Value().after.has_value() &&
           changed.Value().before->revision == 1 && changed.Value().after->revision == 2 &&
           changed.Value().after->placement == placement;
}

bool TestPersistencePromotionForbidsDowngrade()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{900};
    record.persistence_tier = PersistenceTier::PlayerTouched;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto downgrade = runtime.Apply(PromotePersistenceTierCommand{created.Value(), 1, PersistenceTier::Disposable, std::nullopt});
    const auto upgrade = runtime.Apply(PromotePersistenceTierCommand{created.Value(), 1, PersistenceTier::Protected, std::nullopt});
    const auto stored = runtime.FindObject(created.Value());
    return !downgrade.HasValue() && downgrade.GetError().HasCode("world.forbidden_persistence_downgrade") &&
           upgrade.HasValue() && stored.has_value() && stored->persistence_tier == PersistenceTier::Protected &&
           stored->revision == 2;
}

bool TestPlayerTouchedPromotionRequiresPersistentId()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto missing_id = runtime.Apply(PromotePersistenceTierCommand{created.Value(), 1, PersistenceTier::PlayerTouched, std::nullopt});
    const auto promoted = runtime.Apply(PromotePersistenceTierCommand{created.Value(), 1, PersistenceTier::PlayerTouched, PersistentObjectId{901}});
    const auto stored = runtime.FindObject(created.Value());
    return !missing_id.HasValue() && missing_id.GetError().HasCode("world.persistent_id_required") &&
           promoted.HasValue() && stored && stored->persistent_id == PersistentObjectId{901} &&
           stored->persistence_tier == PersistenceTier::PlayerTouched;
}

bool TestPublicCommandsRequireExpectedRevision()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto placement = runtime.Apply(ChangePlacementCommand{created.Value(), 0, HiddenPlacement{}});
    const auto residency = runtime.Apply(ChangeResidencyCommand{created.Value(), 0, ResidencyState::Resident});
    const auto promotion = runtime.Apply(PromotePersistenceTierCommand{created.Value(), 0, PersistenceTier::TemporaryObserved, std::nullopt});
    const auto destruction = runtime.Apply(DestroyObjectCommand{created.Value(), 0});
    return !placement.HasValue() && placement.GetError().HasCode("world.expected_revision_required") &&
           !residency.HasValue() && residency.GetError().HasCode("world.expected_revision_required") &&
           !promotion.HasValue() && promotion.GetError().HasCode("world.expected_revision_required") &&
           !destruction.HasValue() && destruction.GetError().HasCode("world.expected_revision_required");
}

bool TestSurfacePlacementRequiresRegisteredRegionAndChunk()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto unknown_region = runtime.SetPlacement(
        created.Value(), ObjectPlacement{WorldSurfacePlacement{RegionId{100}, ChunkId{200}, Transform{}}});
    if (unknown_region.HasValue() || !unknown_region.GetError().HasCode("world.region_not_found"))
    {
        return false;
    }

    if (!runtime.RegisterRegion(RegionDescriptor{RegionId{100}, StringId::FromString("region")}).HasValue())
    {
        return false;
    }
    const auto unknown_chunk = runtime.SetPlacement(
        created.Value(), ObjectPlacement{WorldSurfacePlacement{RegionId{100}, ChunkId{200}, Transform{}}});
    return !unknown_chunk.HasValue() && unknown_chunk.GetError().HasCode("world.chunk_not_found");
}

bool TestChunkFromAnotherRegionRejected()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{1}, ChunkId{10}) ||
        !runtime.RegisterRegion(RegionDescriptor{RegionId{2}, StringId::FromString("region-2")}).HasValue())
    {
        return false;
    }
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto changed = runtime.SetPlacement(
        created.Value(), ObjectPlacement{WorldSurfacePlacement{RegionId{2}, ChunkId{10}, Transform{}}});
    return !changed.HasValue() && changed.GetError().HasCode("world.chunk_region_mismatch");
}

bool TestSelfContainerRejected()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto changed = runtime.SetPlacement(
        created.Value(), ObjectPlacement{ContainerPlacement{created.Value(), StringId::FromString("bag/main")}});
    return !changed.HasValue() && changed.GetError().HasCode("world.invalid_placement");
}

bool TestContainmentCycleRejected()
{
    WorldRuntime runtime;
    const auto parent = runtime.CreateObject(WorldObjectRecord{});
    const auto child = runtime.CreateObject(WorldObjectRecord{});
    if (!parent.HasValue() || !child.HasValue())
    {
        return false;
    }

    const auto child_inside_parent = runtime.SetPlacement(
        child.Value(), ObjectPlacement{ContainerPlacement{parent.Value(), StringId::FromString("slot/child")}});
    const auto parent_inside_child = runtime.SetPlacement(
        parent.Value(), ObjectPlacement{ContainerPlacement{child.Value(), StringId::FromString("slot/parent")}});
    return child_inside_parent.HasValue() && !parent_inside_child.HasValue() &&
           parent_inside_child.GetError().HasCode("world.containment_cycle");
}

bool TestOwnerMissingRejected()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const auto inventory = runtime.SetPlacement(created.Value(), ObjectPlacement{InventoryPlacement{RuntimeObjectId{999}}});
    const auto equipped = runtime.SetPlacement(
        created.Value(), ObjectPlacement{EquippedPlacement{RuntimeObjectId{999}, StringId::FromString("slot/hand")}});
    return !inventory.HasValue() && inventory.GetError().HasCode("world.owner_not_found") &&
           !equipped.HasValue() && equipped.GetError().HasCode("world.owner_not_found");
}

bool TestInvalidRealityResidencyCombinationsRejected()
{
    const auto logical_active = ValidateRealityResidencyCombination(ObjectRealityLevel::Logical, ResidencyState::Active);
    const auto physical_unloaded = ValidateRealityResidencyCombination(ObjectRealityLevel::Physical, ResidencyState::Unloaded);
    if (logical_active.HasValue() || physical_unloaded.HasValue())
    {
        return false;
    }

    WorldRuntime runtime;
    WorldObjectRecord destroyed{};
    destroyed.placement = DestroyedPlacement{GameTimePoint{10}, StringId::FromString("test.destroyed")};
    destroyed.residency = ResidencyState::Active;
    const auto destroyed_create = runtime.CreateObject(destroyed);

    WorldObjectRecord physical{};
    physical.reality = ObjectRealityLevel::Physical;
    physical.residency = ResidencyState::Unloaded;
    const auto physical_create = runtime.CreateObject(physical);
    return !destroyed_create.HasValue() && !physical_create.HasValue();
}

bool TestDemotionWithoutCollapseConfirmationRejected()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{700};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto demoted = runtime.Demote(DemotionRequest{created.Value(), ObjectRealityLevel::Logical});
    const auto stored = runtime.FindObject(created.Value());
    return !demoted.HasValue() && demoted.GetError().HasCode("world.demotion_not_confirmed") &&
           stored.has_value() && stored->reality == ObjectRealityLevel::Physical && stored->revision == 1;
}

bool TestDestroyedPlacementRequiresDestroyCommandAndIsTerminal()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{910};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto forged_destroyed = runtime.Apply(ChangePlacementCommand{
        created.Value(),
        1,
        ObjectPlacement{DestroyedPlacement{GameTimePoint{1}, StringId::FromString("forged")}}});
    const auto destroyed = runtime.Apply(DestroyObjectCommand{created.Value(), 1, GameTimePoint{2}, StringId::FromString("destroy")});
    const auto move_after_destroy = runtime.Apply(ChangePlacementCommand{created.Value(), 2, ObjectPlacement{HiddenPlacement{}}});
    const auto residency_after_destroy = runtime.Apply(ChangeResidencyCommand{created.Value(), 2, ResidencyState::Resident});
    const auto materialize_after_destroy = runtime.Materialize(MaterializationRequest{record.persistent_id, ObjectRealityLevel::Physical});

    return !forged_destroyed.HasValue() && forged_destroyed.GetError().HasCode("world.destroyed_requires_destroy_command") &&
           destroyed.HasValue() && !move_after_destroy.HasValue() &&
           move_after_destroy.GetError().HasCode("world.destroyed_terminal") &&
           !residency_after_destroy.HasValue() && residency_after_destroy.GetError().HasCode("world.destroyed_terminal") &&
           !materialize_after_destroy.HasValue() && materialize_after_destroy.GetError().HasCode("world.destroyed_terminal");
}

bool TestDestroyRejectsDependentObjects()
{
    WorldRuntime runtime;
    const auto owner = runtime.CreateObject(WorldObjectRecord{});
    const auto dependent = runtime.CreateObject(WorldObjectRecord{});
    if (!owner.HasValue() || !dependent.HasValue() ||
        !runtime.SetPlacement(dependent.Value(), ObjectPlacement{InventoryPlacement{owner.Value()}}).HasValue())
    {
        return false;
    }

    const auto destroyed = runtime.DestroyObject(owner.Value());
    const auto owner_stored = runtime.FindObject(owner.Value());
    return !destroyed.HasValue() && destroyed.GetError().HasCode("world.dependent_objects_exist") &&
           owner_stored.has_value();
}

// Verifies find objects in chunk works.
bool TestFindObjectsInChunkWorks()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{5}, ChunkId{20}) ||
        !runtime.RegisterChunk(ChunkDescriptor{ChunkId{21}, RegionId{5}, 1, 0, 0}).HasValue())
    {
        return false;
    }
    auto first = WorldObjectRecord{};
    first.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{5}, ChunkId{20}, Transform{}}};
    auto second = WorldObjectRecord{};
    second.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{5}, ChunkId{21}, Transform{}}};
    const auto first_created = runtime.CreateObject(first);
    const auto second_created = runtime.CreateObject(second);
    if (!first_created.HasValue() || !second_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsInChunk(ChunkId{20});
    return matches.size() == 1 && matches.front().runtime_id == first_created.Value();
}

// Verifies query order does not depend on unordered storage iteration.
bool TestQueryOrderIsDeterministic()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{5}, ChunkId{20}))
    {
        return false;
    }
    auto first = WorldObjectRecord{};
    first.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{5}, ChunkId{20}, Transform{}}};
    auto second = first;
    auto third = first;

    const auto first_created = runtime.CreateObject(first);
    const auto second_created = runtime.CreateObject(second);
    const auto third_created = runtime.CreateObject(third);
    if (!first_created.HasValue() || !second_created.HasValue() || !third_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsInChunk(ChunkId{20});
    return matches.size() == 3 && matches[0].runtime_id == first_created.Value() &&
           matches[1].runtime_id == second_created.Value() && matches[2].runtime_id == third_created.Value();
}

// Verifies find objects in region works.
bool TestFindObjectsInRegionWorks()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{6}, ChunkId{30}) ||
        !runtime.RegisterRegion(RegionDescriptor{RegionId{7}, StringId::FromString("other-region")}).HasValue() ||
        !runtime.RegisterChunk(ChunkDescriptor{ChunkId{31}, RegionId{7}, 0, 0, 0}).HasValue())
    {
        return false;
    }
    auto first = WorldObjectRecord{};
    first.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{6}, ChunkId{30}, Transform{}}};
    auto second = WorldObjectRecord{};
    second.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{7}, ChunkId{31}, Transform{}}};
    const auto first_created = runtime.CreateObject(first);
    const auto second_created = runtime.CreateObject(second);
    if (!first_created.HasValue() || !second_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsInRegion(RegionId{6});
    return matches.size() == 1 && matches.front().runtime_id == first_created.Value();
}

// Verifies find objects by reality works.
bool TestFindObjectsByRealityWorks()
{
    WorldRuntime runtime;
    auto logical = WorldObjectRecord{};
    logical.reality = ObjectRealityLevel::Logical;
    auto physical = WorldObjectRecord{};
    physical.reality = ObjectRealityLevel::Physical;
    physical.residency = ResidencyState::Active;
    const auto logical_created = runtime.CreateObject(logical);
    const auto physical_created = runtime.CreateObject(physical);
    if (!logical_created.HasValue() || !physical_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsByReality(ObjectRealityLevel::Physical);
    return matches.size() == 1 && matches.front().runtime_id == physical_created.Value();
}

// Verifies materialize changes logical to physical.
bool TestMaterializeChangesLogicalToPhysical()
{
    WorldRuntime runtime;
    if (!RegisterRegionAndChunk(runtime, RegionId{12}, ChunkId{24}))
    {
        return false;
    }
    WorldObjectRecord record{};
    record.persistent_id = epidemic::runtime::PersistentObjectId{77};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Logical;
    record.residency = ResidencyState::Unloaded;
    record.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{12}, ChunkId{24}, Transform{}}};
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto materialized = runtime.Materialize(MaterializationRequest{record.persistent_id, ObjectRealityLevel::Physical});
    const auto stored = runtime.FindObject(created.Value());
    return materialized.HasValue() && stored.has_value() && stored->reality == ObjectRealityLevel::Physical &&
           stored->residency == ResidencyState::Active;
}

bool TestMaterializePhysicalRequiresCompatiblePlacement()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{78};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Logical;
    record.residency = ResidencyState::Unloaded;
    record.placement = HiddenPlacement{};
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto materialized = runtime.Materialize(MaterializationRequest{record.persistent_id, ObjectRealityLevel::Physical});
    const auto stored = runtime.FindObject(created.Value());
    return !materialized.HasValue() && materialized.GetError().HasCode("world.materialization_placement_incompatible") &&
           stored && stored->reality == ObjectRealityLevel::Logical;
}

// Verifies demote changes physical to logical.
bool TestDemoteChangesPhysicalToLogical()
{
    const auto services = CreateWorldServices();
    if (!services || !services.Value().writer || !services.Value().query || !services.Value().materialization || !services.Value().demotion_authority)
    {
        return false;
    }
    WorldObjectRecord record{};
    record.persistent_id = epidemic::runtime::PersistentObjectId{88};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = services.Value().writer->Apply(epidemic::runtime::CreateObjectCommand{record});
    if (!created.HasValue())
    {
        return false;
    }

    const auto token = services.Value().demotion_authority->IssueDemotionCommitToken(
        DemotionSnapshot{created.Value().runtime_id, ObjectRealityLevel::Logical, HiddenPlacement{}, StringId::FromString("collapse/88"), 1});
    if (!token.HasValue())
    {
        return false;
    }
    const auto demoted = services.Value().materialization->Demote(DemotionRequest{created.Value().runtime_id, ObjectRealityLevel::Logical, token.Value()});
    const auto stored = services.Value().query->FindObject(created.Value().runtime_id);
    return demoted.HasValue() && stored.has_value() && stored->reality == ObjectRealityLevel::Logical &&
           stored->residency == ResidencyState::Resident;
}

bool TestForgedDemotionTokenRejected()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{89};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    DemotionCommitToken forged{};
    forged.token_id = 1;
    forged.object = created.Value();
    forged.target_reality = ObjectRealityLevel::Logical;
    forged.collapsed_placement = HiddenPlacement{};
    forged.collapse_record_id = StringId::FromString("collapse/forged");
    forged.source_revision = 1;
    const auto demoted = runtime.Demote(DemotionRequest{created.Value(), ObjectRealityLevel::Logical, forged});
    const auto stored = runtime.FindObject(created.Value());
    return !demoted.HasValue() && demoted.GetError().HasCode("world.demotion_not_confirmed") &&
           stored && stored->reality == ObjectRealityLevel::Physical;
}

bool TestRevokedDemotionTokenRejected()
{
    const auto services = CreateWorldServices();
    if (!services || !services.Value().writer || !services.Value().materialization || !services.Value().demotion_authority)
    {
        return false;
    }

    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{90};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = services.Value().writer->Apply(CreateObjectCommand{record});
    if (!created.HasValue())
    {
        return false;
    }

    const auto token = services.Value().demotion_authority->IssueDemotionCommitToken(
        DemotionSnapshot{created.Value().runtime_id, ObjectRealityLevel::Logical, ObjectPlacement{HiddenPlacement{}}, StringId::FromString("collapse/90"), 1});
    if (!token.HasValue())
    {
        return false;
    }

    const auto revoked = services.Value().demotion_authority->RevokeDemotionCommitToken(token.Value().token_id);
    const auto demoted = services.Value().materialization->Demote(
        DemotionRequest{created.Value().runtime_id, ObjectRealityLevel::Logical, token.Value()});
    return revoked.HasValue() && !demoted.HasValue() && demoted.GetError().HasCode("world.demotion_not_confirmed");
}

bool TestDemotionTokenInvalidatedByObjectRevisionChange()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{91};
    record.persistence_tier = PersistenceTier::TemporaryObserved;
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto token = runtime.IssueDemotionCommitToken(
        DemotionSnapshot{created.Value(), ObjectRealityLevel::Logical, ObjectPlacement{HiddenPlacement{}}, StringId::FromString("collapse/91"), 1});
    if (!token.HasValue())
    {
        return false;
    }

    const auto changed = runtime.SetResidency(created.Value(), ResidencyState::Sleeping);
    const auto demoted = runtime.Demote(DemotionRequest{created.Value(), ObjectRealityLevel::Logical, token.Value()});
    const auto revoked_after_invalidation = runtime.RevokeDemotionCommitToken(token.Value().token_id);
    const auto stored = runtime.FindObject(created.Value());
    return changed.HasValue() && !demoted.HasValue() && demoted.GetError().HasCode("world.demotion_not_confirmed") &&
           !revoked_after_invalidation.HasValue() && revoked_after_invalidation.GetError().HasCode("world.demotion_token_not_found") &&
           stored && stored->revision == 2;
}

// Verifies world location stores region and chunk.
bool TestWorldLocationStoresRegionAndChunk()
{
    const WorldLocation location{RegionId{8}, ChunkId{22}};
    return location.region_id.IsValid() && location.chunk_id.IsValid();
}

bool TestFactoryCreatesSharedWorldServices()
{
    const auto services = CreateWorldServices();
    if (!services.HasValue() || !services.Value().regions || !services.Value().chunks || !services.Value().query ||
        !services.Value().writer || !services.Value().materialization || !services.Value().demotion_authority)
    {
        return false;
    }

    const auto region = services.Value().regions->RegisterRegion(RegionDescriptor{RegionId{1}, StringId::FromString("factory")});
    const auto chunk = services.Value().chunks->RegisterChunk(ChunkDescriptor{ChunkId{1}, RegionId{1}, 0, 0, 0});
    WorldObjectRecord object{};
    object.placement = ObjectPlacement{WorldSurfacePlacement{RegionId{1}, ChunkId{1}, Transform{}}};
    const auto created = services.Value().writer->Apply(epidemic::runtime::CreateObjectCommand{object});
    const auto matches = services.Value().query->FindObjectsInChunk(ChunkId{1});
    return region.HasValue() && chunk.HasValue() && created.HasValue() && matches.size() == 1u &&
           matches.front().runtime_id == created.Value().runtime_id;
}

bool TestDestroyPersistentObjectLeavesTombstone()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = PersistentObjectId{101};
    record.persistence_tier = PersistenceTier::PlayerTouched;
    record.residency = ResidencyState::Resident;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto destroyed = runtime.DestroyObject(created.Value());
    const auto stored = runtime.FindObject(created.Value());
    return destroyed.HasValue() && stored.has_value() &&
           std::holds_alternative<DestroyedPlacement>(stored->placement) &&
           stored->residency == ResidencyState::Unloaded && stored->revision == 2;
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    static_assert(std::is_trivially_copyable_v<ChunkDescriptor>);
    static_assert(std::is_trivially_copyable_v<RegionDescriptor>);
    static_assert(std::is_trivially_copyable_v<WorldLocation>);
    static_assert(std::is_trivially_copyable_v<MaterializationRequest>);
    static_assert(std::is_copy_constructible_v<DemotionRequest>);

    if (!TestRegisterAndFindRegion())
    {
        return 1;
    }

    if (!TestRegisterAndFindChunk())
    {
        return 2;
    }

    if (!TestDuplicateRegionIsRejected())
    {
        return 3;
    }

    if (!TestChunkRegistrationValidation())
    {
        return 4;
    }

    if (!TestChunkStateDefaultsToUnloadedAndCanChange())
    {
        return 5;
    }

    if (!TestCreateObjectReturnsValidRuntimeId())
    {
        return 6;
    }

    if (!TestFindObjectReturnsSnapshot())
    {
        return 7;
    }

    if (!TestSetPlacementUpdatesPlacement())
    {
        return 8;
    }

    if (!TestInvalidPlacementIsRejected())
    {
        return 9;
    }

    if (!TestDuplicatePersistentIdIsRejected())
    {
        return 10;
    }

    if (!TestSetResidencyUpdatesState())
    {
        return 11;
    }

    if (!TestInvalidResidencyTransitionIsRejected())
    {
        return 12;
    }

    if (!TestCommandRevisionConflictIsRejected())
    {
        return 13;
    }

    if (!TestCommandReturnsBeforeAndAfterSnapshots())
    {
        return 14;
    }

    if (!TestPersistencePromotionForbidsDowngrade())
    {
        return 15;
    }

    if (!TestPlayerTouchedPromotionRequiresPersistentId())
    {
        return 32;
    }

    if (!TestPublicCommandsRequireExpectedRevision())
    {
        return 33;
    }

    if (!TestSurfacePlacementRequiresRegisteredRegionAndChunk())
    {
        return 16;
    }

    if (!TestChunkFromAnotherRegionRejected())
    {
        return 17;
    }

    if (!TestSelfContainerRejected())
    {
        return 18;
    }

    if (!TestContainmentCycleRejected())
    {
        return 19;
    }

    if (!TestOwnerMissingRejected())
    {
        return 20;
    }

    if (!TestInvalidRealityResidencyCombinationsRejected())
    {
        return 21;
    }

    if (!TestDemotionWithoutCollapseConfirmationRejected())
    {
        return 22;
    }

    if (!TestDestroyedPlacementRequiresDestroyCommandAndIsTerminal())
    {
        return 34;
    }

    if (!TestDestroyRejectsDependentObjects())
    {
        return 35;
    }

    if (!TestFindObjectsInChunkWorks())
    {
        return 23;
    }

    if (!TestQueryOrderIsDeterministic())
    {
        return 24;
    }

    if (!TestFindObjectsInRegionWorks())
    {
        return 25;
    }

    if (!TestFindObjectsByRealityWorks())
    {
        return 26;
    }

    if (!TestMaterializeChangesLogicalToPhysical())
    {
        return 27;
    }

    if (!TestMaterializePhysicalRequiresCompatiblePlacement())
    {
        return 36;
    }

    if (!TestDemoteChangesPhysicalToLogical())
    {
        return 28;
    }

    if (!TestForgedDemotionTokenRejected())
    {
        return 37;
    }

    if (!TestRevokedDemotionTokenRejected())
    {
        return 38;
    }

    if (!TestDemotionTokenInvalidatedByObjectRevisionChange())
    {
        return 39;
    }

    if (!TestDestroyPersistentObjectLeavesTombstone())
    {
        return 29;
    }

    if (!TestWorldLocationStoresRegionAndChunk())
    {
        return 30;
    }

    if (!TestFactoryCreatesSharedWorldServices())
    {
        return 31;
    }

    return 0;
}
