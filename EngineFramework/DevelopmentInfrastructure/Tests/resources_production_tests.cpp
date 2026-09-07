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
    auto order = p.StartProductionOrder(site.Value(), rid.Value(), in.Value(), out.Value(), GameplayTimePoint{0});
    if (!order) return 29;
    if (p.GetAmount(in.Value(), pwood) != 100 || p.GetAvailableAmount(in.Value(), pwood) != 90) return 30;
    if (p.CompleteProductionOrder(order.Value(), GameplayTimePoint{4})) return 31;
    if (!p.CompleteProductionOrder(order.Value(), GameplayTimePoint{5})) return 32;
    if (p.GetAmount(in.Value(), pwood) != 90 || p.GetAmount(out.Value(), pwood) != 12) return 33;
    if (p.FindProductionOrder(order.Value()) != nullptr) return 34;

    const auto pre_restore_cursor = p.LatestChangeSequence();
    if (pre_restore_cursor < 2) return 57;
    auto snap = p.CaptureSnapshot();
    ResourcesProductionService restored;
    ResourceTypeId restored_wood;
    if (!RegisterWood(restored, restored_wood)) return 35;
    auto rr = restored.RegisterProductionRecipe(recipe); if (!rr) return 36;
    if (!restored.RestoreSnapshot(snap)) return 37;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required) return 49;
    if (!restored.ReadChangesSince(std::numeric_limits<std::uint64_t>::max()).snapshot_required) return 50;
    if (restored.GetAmount(in.Value(), restored_wood) != 90) return 38;

    auto before_bad_restore = restored.GetDiagnostics();
    auto bad = snap;
    bad.stockpile_ids.next = 1;
    if (restored.RestoreSnapshot(bad)) return 39;
    auto after_bad_restore = restored.GetDiagnostics();
    if (before_bad_restore.stockpiles != after_bad_restore.stockpiles ||
        restored.GetAmount(in.Value(), restored_wood) != 90) return 40;
    if (!restored.Add(in.Value(), {restored_wood, 1})) return 51;
    if (!restored.ReadChangesSince(pre_restore_cursor).snapshot_required) return 52;
    const auto resources_epoch = restored.ReadChangesSince(0);
    if (resources_epoch.snapshot_required || resources_epoch.changes.empty()) return 53;
    const auto resources_current = restored.ReadChangesSince(resources_epoch.latest_sequence);
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
    auto batch = journal.ReadChangesSince(0);
    if (!batch.snapshot_required) return 44;
    if (!journal.ReadChangesSince(std::numeric_limits<std::uint64_t>::max()).snapshot_required) return 55;
    ResourcesProductionService empty_journal;
    if (!empty_journal.ReadChangesSince(std::numeric_limits<std::uint64_t>::max()).snapshot_required) return 56;
    if (journal.ChangesSince(journal.LatestChangeSequence()).size() != 0) return 45;

    return 0;
}
