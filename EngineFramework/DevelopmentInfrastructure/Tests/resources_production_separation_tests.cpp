#include "Epidemic/GameFramework/ResourcesProduction/resources_production.h"
#include <cstdlib>
#include <iostream>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::resources;
namespace
{
void Check(bool v, const char *m)
{
    if (!v)
    {
        std::cerr << m << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *d, const char *i)
{
    return {GameplayDomainId::FromString(d), GameplayObjectId::FromString(i)};
}
} // namespace
int main()
{
    ResourcesProductionService s;
    ResourceType iron;
    iron.canonical_name = "resource.iron";
    auto ironid = s.RegisterResourceType(iron);
    Check(static_cast<bool>(ironid), "resource type");
    auto owner = Ref("actor", "forge");
    ResourceStockpile pile;
    pile.owner = owner;
    auto pid = s.CreateStockpile(pile);
    Check(static_cast<bool>(pid), "stockpile");
    Check(static_cast<bool>(s.Add(pid.Value(), {ironid.Value(), 100})), "add");
    auto reservation = s.Reserve(pid.Value(), {{ironid.Value(), 60}}, owner, TypeId::FromString("process"));
    Check(static_cast<bool>(reservation), "reserve");
    Check(s.GetReservedAmount(pid.Value(), ironid.Value()) == 60 &&
              s.GetAvailableAmount(pid.Value(), ironid.Value()) == 40,
          "reservation accounting");
    Check(!s.Remove(pid.Value(), {ironid.Value(), 50}), "reserved amount protected");
    Check(static_cast<bool>(s.ConsumeReservation(reservation.Value())), "consume reservation");
    Check(s.GetAmount(pid.Value(), ironid.Value()) == 40, "reservation consumed");
    ProductionCapability cap;
    cap.site = owner;
    cap.capacity = 2;
    cap.process_tags.Add(TagId::FromString("process.smithing"));
    auto capid = s.CreateProductionCapability(cap);
    Check(static_cast<bool>(capid), "capability");
    ProductionPlan plan;
    plan.owner = owner;
    plan.desired_output = ironid.Value();
    plan.target_quantity = 25;
    plan.priority = 10;
    auto planid = s.CreateProductionPlan(plan);
    Check(static_cast<bool>(planid), "plan");
    Check(s.FindProductionPlans(owner).size() == 1, "find plan");
    Check(static_cast<bool>(s.SetProductionPlanState(planid.Value(), ProductionPlanState::Active)), "activate plan");
    auto snap = s.CaptureSnapshot();
    ResourcesProductionService restored;
    Check(static_cast<bool>(restored.RegisterResourceType(iron)), "restore resource type");
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore resource state");
    Check(restored.FindProductionCapability(capid.Value()) != nullptr &&
              restored.FindProductionPlan(planid.Value()) != nullptr,
          "capability/plan restored");
    return 0;
}
