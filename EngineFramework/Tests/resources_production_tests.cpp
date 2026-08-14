#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::resources;
int main()
{
    ResourcesProductionService s;
    ResourceType wood; wood.canonical_name = "test.resource.wood";
    auto wood_id = s.RegisterResourceType(wood); if (!wood_id) return 1;
    s.Freeze();
    GameplayObjectRef owner{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("owner")};
    auto a = s.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!a) return 2;
    auto b = s.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); if (!b) return 3;
    if (!s.Add(a.Value(), {wood_id.Value(), 100})) return 4;
    if (!s.Transfer(a.Value(), b.Value(), {{wood_id.Value(), 30}}, TypeId::FromString("test.transfer"))) return 5;
    if (s.GetAmount(a.Value(), wood_id.Value()) != 70 || s.GetAmount(b.Value(), wood_id.Value()) != 30) return 6;
    auto recipe = ProductionRecipe{ProductionRecipeId::FromString("test.production.plank"), "test.production.plank", {{wood_id.Value(), 10}}, {{wood_id.Value(), 12}}, GameplayDuration{5}, {}};
    ResourcesProductionService p;
    auto pt = p.RegisterResourceType(wood); if (!pt) return 15;
    auto rid = p.RegisterProductionRecipe(recipe); if (!rid) return 7;
    p.Freeze();
    auto in = p.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}}); auto out = p.CreateStockpile({{}, owner, {}, {}, StockpileState::Active, {}});
    auto site = p.CreateProductionSite({{}, owner, {}, {}, ProductionSiteState::Active, 1'000'000, {}}); if (!in || !out || !site) return 8;
    if (!p.Add(in.Value(), {wood_id.Value(), 100})) return 16;
    auto order = p.StartProductionOrder(site.Value(), rid.Value(), in.Value(), out.Value(), GameplayTimePoint{0}); if (!order) return 9;
    if (p.CompleteProductionOrder(order.Value(), GameplayTimePoint{4})) return 10;
    if (!p.CompleteProductionOrder(order.Value(), GameplayTimePoint{5})) return 11;
    if (p.GetAmount(in.Value(), wood_id.Value()) != 90 || p.GetAmount(out.Value(), wood_id.Value()) != 12) return 12;
    auto snap = p.CaptureSnapshot();
    ResourcesProductionService restored; auto rt = restored.RegisterResourceType(wood); auto rr = restored.RegisterProductionRecipe(recipe); if (!rt || !rr) return 17;
    if (!restored.RestoreSnapshot(std::move(snap))) return 13;
    if (restored.GetAmount(in.Value(), wood_id.Value()) != 90) return 14;
    return 0;
}
