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
#include "Epidemic/Runtime/World/world_query.h"
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
using epidemic::runtime::MaterializationRequest;
using epidemic::runtime::ObjectPlacement;
using epidemic::runtime::ObjectPlacementKind;
using epidemic::runtime::ObjectRealityLevel;
using epidemic::runtime::PersistenceTier;
using epidemic::runtime::RegionDescriptor;
using epidemic::runtime::RegionId;
using epidemic::runtime::ResidencyState;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::WorldLocation;
using epidemic::runtime::WorldObjectRecord;
using epidemic::runtime::WorldRuntime;

bool TestRegisterAndFindRegion()
{
    WorldRuntime runtime;
    const RegionDescriptor region{RegionId{1}, StringId::FromString("test-region")};
    const auto result = runtime.RegisterRegion(region);
    const auto stored = runtime.FindRegion(region.id);

    return result.HasValue() && stored.has_value() && stored.value() == region;
}

bool TestRegisterAndFindChunk()
{
    WorldRuntime runtime;
    const auto region_result = runtime.RegisterRegion(RegionDescriptor{RegionId{2}, StringId::FromString("coast")});
    const ChunkDescriptor chunk{ChunkId{11}, RegionId{2}, 3, 4, 5};
    const auto chunk_result = runtime.RegisterChunk(chunk);
    const auto stored = runtime.FindChunk(chunk.id);

    return region_result.HasValue() && chunk_result.HasValue() && stored.has_value() && stored.value() == chunk;
}

bool TestChunkStateDefaultsToUnloadedAndCanChange()
{
    WorldRuntime runtime;
    const auto region_result = runtime.RegisterRegion(RegionDescriptor{RegionId{3}, StringId::FromString("mountain")});
    const auto chunk_result = runtime.RegisterChunk(ChunkDescriptor{ChunkId{15}, RegionId{3}, 0, 0, 0});
    if (!region_result.HasValue() || !chunk_result.HasValue())
    {
        return false;
    }

    if (runtime.GetChunkState(ChunkId{15}) != ChunkState::Unloaded)
    {
        return false;
    }

    const auto set_state = runtime.SetChunkState(ChunkId{15}, ChunkState::Active);
    return set_state.HasValue() && runtime.GetChunkState(ChunkId{15}) == ChunkState::Active;
}

bool TestCreateObjectReturnsValidRuntimeId()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.asset_id = AssetId::FromString("items/rope");
    const auto created = runtime.CreateObject(record);

    return created.HasValue() && created.Value().IsValid();
}

bool TestFindObjectReturnsSnapshot()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.asset_id = AssetId::FromString("items/apple");
    record.persistence_tier = PersistenceTier::PlayerTouched;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto stored = runtime.FindObject(created.Value());
    return stored.has_value() && stored->runtime_id == created.Value() &&
           stored->persistence_tier == PersistenceTier::PlayerTouched;
}

bool TestSetPlacementUpdatesPlacement()
{
    WorldRuntime runtime;
    const auto created = runtime.CreateObject(WorldObjectRecord{});
    if (!created.HasValue())
    {
        return false;
    }

    const ObjectPlacement placement{ObjectPlacementKind::WorldSurface, RegionId{4}, ChunkId{9}, {}, StringId::FromString("ground")};
    const auto updated = runtime.SetPlacement(created.Value(), placement);
    const auto stored = runtime.FindObject(created.Value());

    return updated.HasValue() && stored.has_value() && stored->placement == placement;
}

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

    return updated.HasValue() && stored.has_value() && stored->residency == ResidencyState::Resident;
}

bool TestFindObjectsInChunkWorks()
{
    WorldRuntime runtime;
    auto first = WorldObjectRecord{};
    first.placement = ObjectPlacement{ObjectPlacementKind::WorldSurface, RegionId{5}, ChunkId{20}, {}, {}};
    auto second = WorldObjectRecord{};
    second.placement = ObjectPlacement{ObjectPlacementKind::WorldSurface, RegionId{5}, ChunkId{21}, {}, {}};
    const auto first_created = runtime.CreateObject(first);
    const auto second_created = runtime.CreateObject(second);
    if (!first_created.HasValue() || !second_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsInChunk(ChunkId{20});
    return matches.size() == 1 && matches.front().runtime_id == first_created.Value();
}

bool TestFindObjectsInRegionWorks()
{
    WorldRuntime runtime;
    auto first = WorldObjectRecord{};
    first.placement = ObjectPlacement{ObjectPlacementKind::WorldSurface, RegionId{6}, ChunkId{30}, {}, {}};
    auto second = WorldObjectRecord{};
    second.placement = ObjectPlacement{ObjectPlacementKind::WorldSurface, RegionId{7}, ChunkId{31}, {}, {}};
    const auto first_created = runtime.CreateObject(first);
    const auto second_created = runtime.CreateObject(second);
    if (!first_created.HasValue() || !second_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsInRegion(RegionId{6});
    return matches.size() == 1 && matches.front().runtime_id == first_created.Value();
}

bool TestFindObjectsByRealityWorks()
{
    WorldRuntime runtime;
    auto logical = WorldObjectRecord{};
    logical.reality = ObjectRealityLevel::Logical;
    auto physical = WorldObjectRecord{};
    physical.reality = ObjectRealityLevel::Physical;
    const auto logical_created = runtime.CreateObject(logical);
    const auto physical_created = runtime.CreateObject(physical);
    if (!logical_created.HasValue() || !physical_created.HasValue())
    {
        return false;
    }

    const auto matches = runtime.FindObjectsByReality(ObjectRealityLevel::Physical);
    return matches.size() == 1 && matches.front().runtime_id == physical_created.Value();
}

bool TestMaterializeChangesLogicalToPhysical()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = epidemic::runtime::PersistentObjectId{77};
    record.reality = ObjectRealityLevel::Logical;
    record.residency = ResidencyState::Unloaded;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto materialized = runtime.Materialize(MaterializationRequest{record.persistent_id, ObjectRealityLevel::Physical, RuntimeBudget{}});
    const auto stored = runtime.FindObject(created.Value());
    return materialized.HasValue() && stored.has_value() && stored->reality == ObjectRealityLevel::Physical &&
           stored->residency == ResidencyState::Active;
}

bool TestDemoteChangesPhysicalToLogical()
{
    WorldRuntime runtime;
    WorldObjectRecord record{};
    record.persistent_id = epidemic::runtime::PersistentObjectId{88};
    record.reality = ObjectRealityLevel::Physical;
    record.residency = ResidencyState::Active;
    const auto created = runtime.CreateObject(record);
    if (!created.HasValue())
    {
        return false;
    }

    const auto demoted = runtime.Demote(DemotionRequest{created.Value(), ObjectRealityLevel::Logical});
    const auto stored = runtime.FindObject(created.Value());
    return demoted.HasValue() && stored.has_value() && stored->reality == ObjectRealityLevel::Logical &&
           stored->residency == ResidencyState::Resident;
}

bool TestWorldLocationStoresRegionAndChunk()
{
    const WorldLocation location{RegionId{8}, ChunkId{22}};
    return location.region_id.IsValid() && location.chunk_id.IsValid();
}
} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<ChunkDescriptor>);
    static_assert(std::is_trivially_copyable_v<RegionDescriptor>);
    static_assert(std::is_trivially_copyable_v<WorldLocation>);
    static_assert(std::is_trivially_copyable_v<ObjectPlacement>);
    static_assert(std::is_trivially_copyable_v<WorldObjectRecord>);
    static_assert(std::is_trivially_copyable_v<MaterializationRequest>);
    static_assert(std::is_trivially_copyable_v<DemotionRequest>);

    if (!TestRegisterAndFindRegion())
    {
        return 1;
    }

    if (!TestRegisterAndFindChunk())
    {
        return 2;
    }

    if (!TestChunkStateDefaultsToUnloadedAndCanChange())
    {
        return 3;
    }

    if (!TestCreateObjectReturnsValidRuntimeId())
    {
        return 4;
    }

    if (!TestFindObjectReturnsSnapshot())
    {
        return 5;
    }

    if (!TestSetPlacementUpdatesPlacement())
    {
        return 6;
    }

    if (!TestSetResidencyUpdatesState())
    {
        return 7;
    }

    if (!TestFindObjectsInChunkWorks())
    {
        return 8;
    }

    if (!TestFindObjectsInRegionWorks())
    {
        return 9;
    }

    if (!TestFindObjectsByRealityWorks())
    {
        return 10;
    }

    if (!TestMaterializeChangesLogicalToPhysical())
    {
        return 11;
    }

    if (!TestDemoteChangesPhysicalToLogical())
    {
        return 12;
    }

    if (!TestWorldLocationStoresRegionAndChunk())
    {
        return 13;
    }

    return 0;
}
