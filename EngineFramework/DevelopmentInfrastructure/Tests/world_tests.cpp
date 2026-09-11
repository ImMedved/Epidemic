#include "Epidemic/GameFramework/World/world.h"

#include "allocation_fault_injection.h"

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
void RegisterTypes(WorldService &world, WorldAlterationTypeId alteration, WorldFeatureTypeId feature)
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

    // One committed transaction has one authoritative revision for every
    // mutation.
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

    // A transaction cannot mutate the same alteration twice in an order-dependent
    // way.
    auto duplicate_mutation = world.BeginTransaction();
    WorldAlterationUpdate updated;
    updated.payload = std::vector<std::byte>{std::byte{1}};
    CHECK(duplicate_mutation.Update(first_id.Value(), updated));
    CHECK(!duplicate_mutation.Remove(first_id.Value()));
    duplicate_mutation.Cancel();

    const GameplayObjectRef placed{GameplayDomainId::FromString("test.world"),
                                   GameplayObjectId::FromString("object.placed")};
    ObjectPlacementRecord placement;
    placement.object = placed;
    placement.location = child.id;
    placement.area = area.id;
    placement.position = child.position;
    CHECK(world.PlaceObject(placement));
    CHECK(world.FindObjectPlacement(placed).has_value());

    // Update patches cannot rewrite immutable origin fields and update the
    // spatial index incrementally.
    auto patch_tx = world.BeginTransaction();
    WorldAlterationUpdate patch;
    patch.affected_area = WorldAabb{{5000, 0, 0}, {6000, 1000, 1000}};
    patch.payload = std::vector<std::byte>{std::byte{2}};
    CHECK(patch_tx.Update(first_id.Value(), patch));
    CHECK(patch_tx.Commit());
    const auto patched = world.FindAlteration(first_id.Value());
    CHECK(patched && patched->type == alteration_type && patched->created_at == first_after_commit->created_at);
    CHECK(world.FindAlterations({{5000, 0, 0}, {6000, 1000, 1000}}).size() == 1);
    CHECK(world.FindAlterations({{0, 0, 0}, {1000, 1000, 1000}}).empty());

    // Dynamic features have a complete runtime lifecycle.
    WorldFeatureRecord dynamic_feature;
    dynamic_feature.id = WorldFeatureId::FromString("feature.dynamic");
    dynamic_feature.type = feature_type;
    dynamic_feature.bounds = {{0, 0, 0}, {10, 10, 10}};
    CHECK(world.AddDynamicFeature(dynamic_feature));
    CHECK(world.UpdateDynamicFeature(dynamic_feature.id, {{20, 0, 0}, {30, 10, 10}}, {}));
    CHECK(world.FindFeature(dynamic_feature.id)->bounds.min.x_mm == 20);
    CHECK(world.RemoveDynamicFeature(dynamic_feature.id));
    CHECK(!world.FindFeature(dynamic_feature.id));

    // Terminal alterations can be compacted out of authoritative state.
    auto remove_tx = world.BeginTransaction();
    CHECK(remove_tx.Remove(second_id.Value()));
    CHECK(remove_tx.Commit());
    CHECK(world.FindAlteration(second_id.Value())->state == WorldAlterationState::Removed);
    CHECK(world.CompactAlteration(second_id.Value()));
    CHECK(!world.FindAlteration(second_id.Value()));

    const auto snapshot = world.CaptureSnapshot();
    CHECK(snapshot.alterations.size() == 1 && snapshot.object_placements.size() == 1);

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
    CHECK(restored.CurrentRevision() == revision_before_corrupt &&
          restored.FindAlteration(first_id.Value()).has_value());

    // Caller-supplied IDs in the service scope advance the generator and cannot
    // be reproduced later.
    auto id_snapshot = restored.CaptureSnapshot();
    const auto requested_low = id_snapshot.alteration_ids.next + 100;
    auto requested_tx = restored.BeginTransaction();
    WorldAlterationRecord requested = first;
    requested.id = WorldAlterationId::FromRaw(id_snapshot.alteration_ids.scope, requested_low);
    CHECK(requested_tx.Create(requested));
    CHECK(requested_tx.Commit());
    auto following_tx = restored.BeginTransaction();
    WorldAlterationRecord following = first;
    const auto following_id = following_tx.Create(following);
    CHECK(following_id && following_id.Value().value.Low() > requested_low);
    CHECK(following_tx.Commit());

    // Cancelled transactions and failed commits do not consume alteration IDs.
    const auto generator_before_cancel = restored.CaptureSnapshot().alteration_ids;
    auto cancelled_id_tx = restored.BeginTransaction();
    WorldAlterationRecord cancelled_record = first;
    CHECK(cancelled_id_tx.Create(cancelled_record));
    cancelled_id_tx.Cancel();
    const auto generator_after_cancel = restored.CaptureSnapshot().alteration_ids;
    CHECK(generator_after_cancel.scope == generator_before_cancel.scope &&
          generator_after_cancel.next == generator_before_cancel.next);

    // All public alteration enum inputs are validated before publication.
    auto invalid_state_tx = restored.BeginTransaction();
    WorldAlterationRecord invalid_state = first;
    invalid_state.state = static_cast<WorldAlterationState>(255);
    const auto invalid_state_id = invalid_state_tx.Create(invalid_state);
    CHECK(invalid_state_id && !invalid_state_tx.Commit());
    CHECK(!restored.FindAlteration(invalid_state_id.Value()));

    auto invalid_persistence_tx = restored.BeginTransaction();
    WorldAlterationRecord invalid_persistence = first;
    invalid_persistence.persistence = static_cast<WorldAlterationPersistence>(255);
    const auto invalid_persistence_id = invalid_persistence_tx.Create(invalid_persistence);
    CHECK(invalid_persistence_id && !invalid_persistence_tx.Commit());
    CHECK(!restored.FindAlteration(invalid_persistence_id.Value()));

    auto invalid_enum_snapshot = restored.CaptureSnapshot();
    invalid_enum_snapshot.alterations.front().state = static_cast<WorldAlterationState>(255);
    CHECK(!restored.RestoreSnapshot(std::move(invalid_enum_snapshot)));

    // Allocation failure leaves direct mutations, transactions, indexes, journals
    // and generators unchanged.
    WorldFeatureRecord fault_feature;
    fault_feature.id = WorldFeatureId::FromString("feature.fault");
    fault_feature.type = feature_type;
    fault_feature.bounds = {{0, 0, 0}, {10, 10, 10}};
    const auto direct_revision_before = restored.CurrentRevision();
    const auto direct_cursor_before = restored.LatestChangeCursor();
    {
        epidemic::tests::allocation_fault::FailAfter fault(0);
        CHECK(!restored.AddDynamicFeature(fault_feature));
    }
    CHECK(!restored.FindFeature(fault_feature.id));
    CHECK(restored.CurrentRevision() == direct_revision_before &&
          restored.LatestChangeCursor() == direct_cursor_before);

    const auto transaction_before = restored.CaptureSnapshot();
    const auto transaction_cursor_before = restored.LatestChangeCursor();
    auto fault_tx = restored.BeginTransaction();
    WorldAlterationRecord fault_alteration = first;
    fault_alteration.affected_area = {{7000, 0, 0}, {8000, 1000, 1000}};
    const auto fault_id = fault_tx.Create(fault_alteration);
    CHECK(fault_id);
    {
        epidemic::tests::allocation_fault::FailAfter fault(0);
        CHECK(!fault_tx.Commit());
    }
    const auto transaction_after = restored.CaptureSnapshot();
    CHECK(!restored.FindAlteration(fault_id.Value()));
    CHECK(transaction_after.revision == transaction_before.revision &&
          transaction_after.alteration_ids.next == transaction_before.alteration_ids.next &&
          restored.LatestChangeCursor() == transaction_cursor_before);
    // The change journal is bounded and reports when a consumer must resync from
    // a snapshot.
    for (int i = 0; i < 4200; ++i)
    {
        const GameplayObjectRef object{GameplayDomainId::FromString("test.world.journal"),
                                       GameplayObjectId::FromRaw(1, static_cast<std::uint64_t>(i + 1))};
        ObjectPlacementRecord p;
        p.object = object;
        p.area = area.id;
        CHECK(restored.PlaceObject(p));
    }
    const auto stale_batch = restored.ReadChangesSince(ChangeCursor{});
    CHECK(stale_batch.snapshot_required);
    const auto latest_batch = restored.ReadChangesSince(restored.LatestChangeCursor());
    CHECK(!latest_batch.snapshot_required && latest_batch.changes.empty());

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

    // Asymmetric adjacency is also invalid; adjacency is a symmetric topology
    // relation.
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
