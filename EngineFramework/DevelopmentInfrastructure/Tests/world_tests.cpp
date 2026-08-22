#include "Epidemic/GameFramework/World/world.h"

#include <cstdlib>

#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            std::abort();                                                                                              \
    } while (false)

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::world;

namespace
{
void RegisterTypes(WorldService& world, WorldAlterationTypeId alteration, WorldFeatureTypeId feature)
{
    CHECK(world.RegisterAlterationType(alteration, "game.test.terrain"));
    CHECK(world.RegisterFeatureType(feature, "game.test.road"));
}
} // namespace

int main()
{
    WorldService world;
    const auto alteration_type = WorldAlterationTypeId::FromString("game.test.terrain");
    const auto feature_type = WorldFeatureTypeId::FromString("game.test.road");
    RegisterTypes(world, alteration_type, feature_type);

    WorldRegionDefinition region;
    region.id = WorldRegionId::FromString("region.a");
    region.canonical_name = "region.a";
    CHECK(world.RegisterRegion(region));

    WorldAreaDefinition area;
    area.id = WorldAreaId::FromString("area.a");
    area.canonical_name = "area.a";
    area.bounds = {{0, 0, 0}, {10000, 10000, 10000}};
    area.regions = {region.id};
    CHECK(world.RegisterArea(area));

    LocationDefinition parent;
    parent.id = LocationId::FromString("loc.parent");
    parent.canonical_name = "loc.parent";
    parent.position = {100, 100, 100};
    parent.regions = {region.id};
    parent.areas = {area.id};
    CHECK(world.RegisterLocation(parent));

    LocationDefinition child;
    child.id = LocationId::FromString("loc.child");
    child.canonical_name = "loc.child";
    child.position = {200, 100, 100};
    child.regions = {region.id};
    child.areas = {area.id};
    child.parent_location = parent.id;
    CHECK(world.RegisterLocation(child));

    CHECK(world.Freeze());
    CHECK(world.FindAreasAt({500, 500, 500}).size() == 1);
    CHECK(world.FindLocationsInArea(area.id).size() == 2);
    CHECK(world.FindChildLocations(parent.id).size() == 1);

    WorldRegionDefinition frozen;
    frozen.id = WorldRegionId::FromString("x");
    frozen.canonical_name = "x";
    CHECK(!world.RegisterRegion(frozen));

    // One committed transaction has one authoritative revision for every mutation.
    auto transaction = world.BeginTransaction();
    WorldAlterationRecord first;
    first.type = alteration_type;
    first.affected_area = {{0, 0, 0}, {1000, 1000, 1000}};
    first.persistence = WorldAlterationPersistence::Persistent;
    const auto first_id = transaction.Create(first);
    WorldAlterationRecord second = first;
    second.affected_area = {{2000, 0, 0}, {3000, 1000, 1000}};
    const auto second_id = transaction.Create(second);
    CHECK(first_id && second_id && transaction.Commit());
    const auto first_after_commit = world.FindAlteration(first_id.Value());
    const auto second_after_commit = world.FindAlteration(second_id.Value());
    CHECK(first_after_commit && second_after_commit);
    CHECK(first_after_commit->revision == second_after_commit->revision);

    // A transaction cannot mutate the same alteration twice in an order-dependent way.
    auto duplicate_mutation = world.BeginTransaction();
    auto updated = *first_after_commit;
    updated.payload.push_back(std::byte{1});
    CHECK(duplicate_mutation.Update(updated));
    CHECK(!duplicate_mutation.Remove(first_id.Value()));
    duplicate_mutation.Cancel();

    const GameplayObjectRef placed{GameplayDomainId::FromString("test.world"), GameplayObjectId::FromString("object.placed")};
    ObjectPlacementRecord placement;
    placement.object = placed;
    placement.location = child.id;
    placement.area = area.id;
    placement.position = child.position;
    CHECK(world.PlaceObject(placement));
    CHECK(world.FindObjectPlacement(placed).has_value());

    const auto snapshot = world.CaptureSnapshot();
    CHECK(snapshot.alterations.size() == 2 && snapshot.object_placements.size() == 1);

    WorldService restored;
    RegisterTypes(restored, alteration_type, feature_type);
    CHECK(restored.RegisterRegion(region));
    CHECK(restored.RegisterArea(area));
    CHECK(restored.RegisterLocation(parent));
    CHECK(restored.RegisterLocation(child));
    CHECK(restored.Freeze());
    CHECK(restored.RestoreSnapshot(snapshot));
    CHECK(restored.FindAlteration(first_id.Value()).has_value());
    CHECK(restored.FindObjectPlacement(placed).has_value());

    // Corrupt restore is rejected atomically.
    auto corrupt = snapshot;
    corrupt.alterations.push_back(corrupt.alterations.front());
    const auto revision_before_corrupt = restored.CurrentRevision();
    CHECK(!restored.RestoreSnapshot(corrupt));
    CHECK(restored.CurrentRevision() == revision_before_corrupt && restored.FindAlteration(first_id.Value()).has_value());

    // Freeze rejects missing topology references.
    WorldService missing_reference;
    RegisterTypes(missing_reference, alteration_type, feature_type);
    WorldAreaDefinition invalid_area;
    invalid_area.id = WorldAreaId::FromString("area.invalid");
    invalid_area.canonical_name = "area.invalid";
    invalid_area.bounds = {{0, 0, 0}, {1, 1, 1}};
    invalid_area.regions = {WorldRegionId::FromString("region.missing")};
    CHECK(missing_reference.RegisterArea(invalid_area));
    CHECK(!missing_reference.Freeze());

    // Parent-location cycles are rejected at the authored topology boundary.
    WorldService cycle;
    RegisterTypes(cycle, alteration_type, feature_type);
    LocationDefinition cycle_a;
    cycle_a.id = LocationId::FromString("loc.cycle.a");
    cycle_a.canonical_name = "loc.cycle.a";
    cycle_a.parent_location = LocationId::FromString("loc.cycle.b");
    LocationDefinition cycle_b;
    cycle_b.id = LocationId::FromString("loc.cycle.b");
    cycle_b.canonical_name = "loc.cycle.b";
    cycle_b.parent_location = cycle_a.id;
    CHECK(cycle.RegisterLocation(cycle_a));
    CHECK(cycle.RegisterLocation(cycle_b));
    CHECK(!cycle.Freeze());

    // Asymmetric adjacency is also invalid; adjacency is a symmetric topology relation.
    WorldService adjacency;
    RegisterTypes(adjacency, alteration_type, feature_type);
    WorldAreaDefinition left;
    left.id = WorldAreaId::FromString("area.left");
    left.canonical_name = "area.left";
    left.bounds = {{0, 0, 0}, {10, 10, 10}};
    left.adjacent = {WorldAreaId::FromString("area.right")};
    WorldAreaDefinition right;
    right.id = WorldAreaId::FromString("area.right");
    right.canonical_name = "area.right";
    right.bounds = {{11, 0, 0}, {20, 10, 10}};
    CHECK(adjacency.RegisterArea(left));
    CHECK(adjacency.RegisterArea(right));
    CHECK(!adjacency.Freeze());

    return 0;
}
