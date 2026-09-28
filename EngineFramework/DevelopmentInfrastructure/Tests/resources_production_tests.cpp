#include "../../GameplayWorldStateOwners/ResourcesProduction/src/resources_production_test_seam.h"
#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"

#include <limits>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::resources;

namespace
{
[[nodiscard]] GameplayObjectRef Ref(const char *name)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(name)};
}

[[nodiscard]] bool RegisterWood(ResourcesProductionService &service, ResourceTypeId &wood)
{
    ResourceType type;
    type.canonical_name = "test.resource.wood";
    auto id = service.RegisterResourceType(type);
    if (!id)
        return false;
    wood = id.Value();
    return true;
}

[[nodiscard]] bool SameCoreSnapshot(const ResourcesSnapshot &a, const ResourcesSnapshot &b)
{
    return a.stockpiles.size() == b.stockpiles.size() && a.amounts.size() == b.amounts.size() &&
           a.nodes.size() == b.nodes.size() && a.sites.size() == b.sites.size() &&
           a.reservations.size() == b.reservations.size() && a.capabilities.size() == b.capabilities.size() &&
           a.plans.size() == b.plans.size() && a.transactions.size() == b.transactions.size() &&
           a.stockpile_ids.next == b.stockpile_ids.next && a.node_ids.next == b.node_ids.next &&
           a.site_ids.next == b.site_ids.next && a.reservation_ids.next == b.reservation_ids.next &&
           a.capability_ids.next == b.capability_ids.next && a.plan_ids.next == b.plan_ids.next &&
           a.transaction_ids.next == b.transaction_ids.next && a.revision == b.revision &&
           a.change_epoch == b.change_epoch;
}

[[nodiscard]] bool HasRevisionExhausted(const auto &result)
{
    return !result && result.GetError().HasCode("gameplay.resources.revision_exhausted");
}

[[nodiscard]] bool TestRevisionExhaustionKeepsAllGenerators()
{
    ResourcesProductionService service;
    ResourceTypeId wood;
    if (!RegisterWood(service, wood))
        return false;
    service.Freeze();
    const auto owner = Ref("revision.owner");
    auto source = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
    auto destination = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
    if (!source || !destination || !service.Add(source.Value(), {wood, 100}))
        return false;

    auto exhausted = service.CaptureSnapshot();
    exhausted.revision.value = std::numeric_limits<std::uint64_t>::max();
    if (!service.RestoreSnapshot(exhausted))
        return false;
    const auto before = service.CaptureSnapshot();

    ResourceNode node;
    node.type = wood;
    node.remaining_amount = 1;
    node.maximum_amount = 1;
    node.state = ResourceNodeState::Active;
    ProductionSite site;
    site.site_object = owner;
    ProductionCapability capability;
    capability.site = owner;
    capability.capacity = 1;
    ProductionPlan plan;
    plan.owner = owner;
    plan.desired_output = wood;
    plan.target_quantity = 1;

    if (!HasRevisionExhausted(service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}})) ||
        !SameCoreSnapshot(before, service.CaptureSnapshot()))
        return false;
    if (!HasRevisionExhausted(service.CreateNode(node)) || !SameCoreSnapshot(before, service.CaptureSnapshot()))
        return false;
    if (!HasRevisionExhausted(service.CreateProductionSite(site)) || !SameCoreSnapshot(before, service.CaptureSnapshot()))
        return false;
    if (!HasRevisionExhausted(service.Reserve(source.Value(), {{wood, 1}}, owner)) ||
        !SameCoreSnapshot(before, service.CaptureSnapshot()) || service.GetReservedAmount(source.Value(), wood) != 0)
        return false;
    if (!HasRevisionExhausted(service.Transfer(source.Value(), destination.Value(), {{wood, 1}})) ||
        !SameCoreSnapshot(before, service.CaptureSnapshot()) || service.GetAmount(source.Value(), wood) != 100 ||
        service.GetAmount(destination.Value(), wood) != 0)
        return false;
    if (!HasRevisionExhausted(service.CreateProductionCapability(capability)) ||
        !SameCoreSnapshot(before, service.CaptureSnapshot()))
        return false;
    if (!HasRevisionExhausted(service.CreateProductionPlan(plan)) || !SameCoreSnapshot(before, service.CaptureSnapshot()))
        return false;
    return true;
}

[[nodiscard]] bool TestPublicationFaultAtomicity()
{
    const auto owner = Ref("fault.owner");

    for (const auto point : {internal_test::AllocationFaultPoint::PrimaryInsert,
                             internal_test::AllocationFaultPoint::JournalAppend})
    {
        ResourcesProductionService service;
        ResourceTypeId wood;
        if (!RegisterWood(service, wood))
            return false;
        service.Freeze();
        const auto before = service.CaptureSnapshot();
        internal_test::ArmAllocationFault(point);
        const auto result = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
        internal_test::ResetAllocationFault();
        if (result || !result.GetError().HasCode("gameplay.resources.publication_failed") ||
            !SameCoreSnapshot(before, service.CaptureSnapshot()))
            return false;
    }

    {
        ResourcesProductionService service;
        ResourceTypeId wood;
        if (!RegisterWood(service, wood))
            return false;
        service.Freeze();
        auto stockpile = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
        if (!stockpile || !service.Add(stockpile.Value(), {wood, 10}))
            return false;
        const auto before = service.CaptureSnapshot();
        internal_test::ArmAllocationFault(internal_test::AllocationFaultPoint::ReservedIndexInsert);
        const auto result = service.Reserve(stockpile.Value(), {{wood, 1}}, owner);
        internal_test::ResetAllocationFault();
        if (result || !result.GetError().HasCode("gameplay.resources.publication_failed") ||
            !SameCoreSnapshot(before, service.CaptureSnapshot()) || service.GetReservedAmount(stockpile.Value(), wood) != 0)
            return false;
    }

    {
        ResourcesProductionService service;
        ResourceTypeId wood;
        if (!RegisterWood(service, wood))
            return false;
        service.Freeze();
        auto from = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
        auto to = service.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
        if (!from || !to || !service.Add(from.Value(), {wood, 10}))
            return false;
        const auto before = service.CaptureSnapshot();
        internal_test::ArmAllocationFault(internal_test::AllocationFaultPoint::TransactionPublication);
        const auto result = service.Transfer(from.Value(), to.Value(), {{wood, 1}});
        internal_test::ResetAllocationFault();
        if (result || !result.GetError().HasCode("gameplay.resources.publication_failed") ||
            !SameCoreSnapshot(before, service.CaptureSnapshot()) || service.GetAmount(from.Value(), wood) != 10 ||
            service.GetAmount(to.Value(), wood) != 0)
            return false;
    }
    return true;
}
} // namespace

int main()
{
    ResourcesProductionService s;
    ResourceTypeId wood;
    if (!RegisterWood(s, wood)) return 1;
    s.Freeze();

    auto owner = Ref("owner");
    auto a = s.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!a) return 2;
    auto b = s.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!b) return 3;
    if (!s.Add(a.Value(), {wood, 100})) return 4;
    if (!s.Transfer(a.Value(), b.Value(), {{wood, 30}}, TypeId::FromString("test.transfer"))) return 5;
    if (s.GetAmount(a.Value(), wood) != 70 || s.GetAmount(b.Value(), wood) != 30) return 6;

    auto duplicate_overreserve = s.Reserve(a.Value(), {{wood, 40}, {wood, 40}}, owner,
                                           TypeId::FromString("test.reserve"));
    if (duplicate_overreserve) return 7;
    if (s.GetReservedAmount(a.Value(), wood) != 0 || s.GetAvailableAmount(a.Value(), wood) != 70) return 8;

    auto exact_reserve = s.Reserve(a.Value(), {{wood, 35}, {wood, 35}}, owner,
                                   TypeId::FromString("test.reserve"));
    if (!exact_reserve) return 9;
    if (s.GetReservedAmount(a.Value(), wood) != 70 || s.GetAvailableAmount(a.Value(), wood) != 0) return 10;
    if (!s.ReleaseReservation(exact_reserve.Value())) return 11;
    if (s.GetReservedAmount(a.Value(), wood) != 0 || s.FindReservation(exact_reserve.Value()) != nullptr) return 12;

    if (!s.SetStockpileState(b.Value(), StockpileState::Disabled)) return 13;
    if (s.Add(b.Value(), {wood, 1})) return 14;
    if (s.Remove(b.Value(), {wood, 1})) return 15;
    if (s.Reserve(b.Value(), {{wood, 1}}, owner)) return 16;
    if (s.Transfer(a.Value(), b.Value(), {{wood, 10}}, TypeId::FromString("test.transfer"))) return 17;
    if (s.GetAmount(a.Value(), wood) != 70 || s.GetAmount(b.Value(), wood) != 30) return 18;
    if (!s.SetStockpileState(b.Value(), StockpileState::Active)) return 19;

    ResourceNode node;
    node.type = wood;
    node.remaining_amount = 0;
    node.regeneration_rate_per_tick = 3;
    node.state = ResourceNodeState::Depleted;
    node.maximum_amount = 10;
    node.last_regenerated_at = GameplayTimePoint{0};
    auto node_id = s.CreateNode(node); if (!node_id) return 20;
    if (!s.RegenerateNode(node_id.Value(), GameplayTimePoint{4})) return 21;
    auto snap_after_node = s.CaptureSnapshot();
    bool found_node = false;
    for (const auto &n : snap_after_node.nodes)
    {
        if (n.id == node_id.Value())
        {
            found_node = true;
            if (n.remaining_amount != 10 || n.last_regenerated_at.ticks != 4) return 22;
        }
    }
    if (!found_node) return 23;
    if (s.RegenerateNode(node_id.Value(), GameplayTimePoint{3})) return 24;

    ResourceNode overflow_node = node;
    overflow_node.last_regenerated_at = GameplayTimePoint{std::numeric_limits<std::int64_t>::min()};
    overflow_node.remaining_amount = 1;
    overflow_node.maximum_amount = 10;
    overflow_node.regeneration_rate_per_tick = 1;
    auto overflow_node_id = s.CreateNode(overflow_node); if (!overflow_node_id) return 46;
    const auto overflow_before = s.CaptureSnapshot();
    auto overflow_regen = s.RegenerateNode(overflow_node_id.Value(), GameplayTimePoint{std::numeric_limits<std::int64_t>::max()});
    if (overflow_regen || !overflow_regen.GetError().HasCode("gameplay.time_overflow")) return 47;
    const auto overflow_after = s.CaptureSnapshot();
    const ResourceNode *before_node = nullptr;
    const ResourceNode *after_node = nullptr;
    for (const auto &entry_node : overflow_before.nodes) if (entry_node.id == overflow_node_id.Value()) before_node = &entry_node;
    for (const auto &entry_node : overflow_after.nodes) if (entry_node.id == overflow_node_id.Value()) after_node = &entry_node;
    if (before_node == nullptr || after_node == nullptr || before_node->remaining_amount != after_node->remaining_amount ||
        before_node->last_regenerated_at != after_node->last_regenerated_at || before_node->revision != after_node->revision) return 48;

    auto recipe = ProductionRecipe{ProductionRecipeId::FromString("test.production.plank"), "test.production.plank",
                                   {{wood, 10}}, {{wood, 12}}, GameplayDuration{5}, {}};
    ResourcesProductionService p;
    ResourceTypeId pwood;
    if (!RegisterWood(p, pwood)) return 25;
    auto rid = p.RegisterProductionRecipe(recipe); if (!rid) return 26;
    p.Freeze();
    auto in = p.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
    auto out = p.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
    auto site = p.CreateProductionSite({{}, owner, {}, {}, ProductionSiteState::Active, 1'000'000, {}});
    if (!in || !out || !site) return 27;
    if (!p.Add(in.Value(), {pwood, 100})) return 28;

    const auto pre_restore_cursor = p.LatestChangeCursor();
    if (pre_restore_cursor.sequence < 2) return 57;
    auto snap = p.CaptureSnapshot();
    ResourcesProductionService restored;
    ResourceTypeId restored_wood;
    if (!RegisterWood(restored, restored_wood)) return 35;
    auto rr = restored.RegisterProductionRecipe(recipe); if (!rr) return 36;
    if (!restored.RestoreSnapshot(snap)) return 37;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required) return 49;
    if (!restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required) return 50;
    if (restored.GetAmount(in.Value(), restored_wood) != 100) return 38;

    auto before_bad_restore = restored.GetDiagnostics();
    auto bad = snap;
    bad.stockpile_ids.next = 1;
    if (restored.RestoreSnapshot(bad)) return 39;
    auto after_bad_restore = restored.GetDiagnostics();
    if (before_bad_restore.stockpiles != after_bad_restore.stockpiles ||
        restored.GetAmount(in.Value(), restored_wood) != 100) return 40;
    if (!restored.Add(in.Value(), {restored_wood, 1})) return 51;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required) return 52;
    const auto resources_epoch = restored.ReadChangesSince(ChangeCursor{});
    if (resources_epoch.snapshot_required || resources_epoch.changes.empty()) return 53;
    const auto resources_current = restored.ReadChangesSince(resources_epoch.latest_cursor);
    if (resources_current.snapshot_required || !resources_current.changes.empty()) return 54;

    ResourcesProductionService journal;
    ResourceTypeId jwood;
    if (!RegisterWood(journal, jwood)) return 41;
    journal.Freeze();
    auto js = journal.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!js) return 42;
    for (int i = 0; i < 4100; ++i)
    {
        if (!journal.Add(js.Value(), {jwood, 1})) return 43;
    }
    auto batch = journal.ReadChangesSince(ChangeCursor{});
    if (!batch.snapshot_required) return 44;
    if (!journal.ReadChangesSince(journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required) return 55;
    ResourcesProductionService empty_journal;
    if (!empty_journal.ReadChangesSince(journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required) return 56;
    if (journal.ReadChangesSince(journal.LatestChangeCursor()).changes.size() != 0) return 45;

    // Goal 4: deterministic module-local restore seams replace process-global allocation hooks.
    const auto allocation_before = restored.CaptureSnapshot();
    for (const auto point : {internal_test::AllocationFaultPoint::RestorePrimary,
                             internal_test::AllocationFaultPoint::RestoreDerived})
    {
        auto allocation_target = allocation_before;
        internal_test::ArmAllocationFault(point);
        const auto restored_under_fault = restored.RestoreSnapshot(std::move(allocation_target));
        internal_test::ResetAllocationFault();
        if (restored_under_fault || !SameCoreSnapshot(allocation_before, restored.CaptureSnapshot()))
            return 941;
    }
    if (!TestRevisionExhaustionKeepsAllGenerators())
        return 942;
    if (!TestPublicationFaultAtomicity())
        return 943;
    return 0;
}
