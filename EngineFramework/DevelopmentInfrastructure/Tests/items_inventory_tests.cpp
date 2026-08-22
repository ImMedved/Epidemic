#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include <cstdlib>
#include <iostream>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::items;
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
    ItemsInventoryService s;
    ItemDefinition apple;
    apple.canonical_name = "item.apple";
    apple.base_weight = 2;
    apple.base_volume = 1;
    apple.stack_policy = ItemStackPolicy::StackByDefinition;
    auto apple_id = s.RegisterDefinition(apple);
    Check(static_cast<bool>(apple_id), "register apple");
    ItemDefinition sword;
    sword.canonical_name = "item.sword";
    sword.base_weight = 10;
    sword.stack_policy = ItemStackPolicy::NonStackable;
    sword.durability_policy = ItemDurabilityPolicy::InstanceValue;
    sword.default_durability = 100;
    sword.max_durability = 100;
    auto sword_id = s.RegisterDefinition(sword);
    Check(static_cast<bool>(sword_id), "register sword");
    s.Freeze();
    ContainerRecord bag;
    bag.owner_object = Ref("actor", "player");
    bag.max_weight = 100;
    bag.max_slots = 10;
    auto bag_id = s.CreateContainer(bag);
    Check(static_cast<bool>(bag_id), "create bag");
    ContainerRecord chest;
    chest.owner_object = Ref("world", "chest");
    chest.max_weight = 100;
    chest.max_slots = 10;
    auto chest_id = s.CreateContainer(chest);
    Check(static_cast<bool>(chest_id), "create chest");
    ItemInstance stack;
    stack.definition = apple_id.Value();
    stack.quantity = 10;
    stack.location = {ItemLocationKind::Container, bag_id.Value(), {}, {}, {}};
    auto stack_id = s.CreateItem(stack);
    Check(static_cast<bool>(stack_id), "create stack");
    auto r = s.ReserveItem(stack_id.Value(), 3, Ref("actor", "process"));
    Check(static_cast<bool>(r), "reserve stack");
    Check(!s.ReserveItem(stack_id.Value(), 8, Ref("actor", "other")), "reservation conflict");
    Check(static_cast<bool>(s.ReleaseReservation(r.Value())), "release reservation");
    ItemLocation target;
    target.kind = ItemLocationKind::Container;
    target.container = chest_id.Value();
    auto plan = s.PrepareTransfer(stack_id.Value(), target, 10);
    Check(static_cast<bool>(plan), "prepare transfer");
    auto temporary = s.ReserveItem(stack_id.Value(), 1, Ref("actor", "temp"));
    Check(static_cast<bool>(temporary), "reserve after prepare");
    Check(!s.CommitTransfer(plan.Value()), "stale transfer blocked by reservation");
    Check(static_cast<bool>(s.ReleaseReservation(temporary.Value())), "release temp");
    auto plan2 = s.PrepareTransfer(stack_id.Value(), target, 10);
    Check(static_cast<bool>(plan2) && static_cast<bool>(s.CommitTransfer(plan2.Value())), "commit transfer");
    Check(s.FindItemsInContainer(chest_id.Value()).size() == 1, "item moved");
    ItemInstance sw;
    sw.definition = sword_id.Value();
    sw.location = {ItemLocationKind::Container, bag_id.Value(), {}, {}, {}};
    auto swid = s.CreateItem(sw);
    Check(static_cast<bool>(swid), "create sword");
    Check(s.FindItem(swid.Value())->durability == 100, "default durability");
    Check(static_cast<bool>(s.AdjustDurability(swid.Value(), -25)), "durability change");
    Check(s.FindItem(swid.Value())->durability == 75, "durability stored");

    // B25 support regression: exchange old equipment reservations for a new item
    // under one Items revision and without exposing a partially released state.
    ItemInstance replacement;
    replacement.definition = sword_id.Value();
    replacement.location = {ItemLocationKind::Container, bag_id.Value(), {}, {}, {}};
    auto replacement_id = s.CreateItem(replacement);
    Check(static_cast<bool>(replacement_id), "create replacement sword");
    const auto equipment_owner = Ref("actor", "equipment_owner");
    const auto equipment_reason = TypeId::FromString("equipment.binding");
    auto old_equipment_reservation = s.ReserveItem(swid.Value(), 1, equipment_owner, equipment_reason);
    Check(static_cast<bool>(old_equipment_reservation), "reserve old equipment item");
    const auto before_exchange_revision = s.CurrentRevision();
    auto exchanged = s.ExchangeReservations({&old_equipment_reservation.Value(), 1}, replacement_id.Value(), 1,
                                             equipment_owner, equipment_reason);
    Check(static_cast<bool>(exchanged), "atomic reservation exchange");
    Check(s.CurrentRevision().value == before_exchange_revision.value + 1, "exchange uses one revision");
    Check(s.FindReservations(swid.Value()).empty(), "old equipment reservation released");
    Check(s.FindReservations(replacement_id.Value()).size() == 1, "replacement item reserved");

    auto snap = s.CaptureSnapshot();
    ItemsInventoryService restored;
    Check(static_cast<bool>(restored.RegisterDefinition(apple)), "restore apple def");
    Check(static_cast<bool>(restored.RegisterDefinition(sword)), "restore sword def");
    restored.Freeze();
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore items");
    Check(restored.FindItemsInContainer(chest_id.Value()).size() == 1, "restored container items");
    ContainerRecord tiny;
    tiny.owner_object = Ref("actor", "tiny");
    tiny.max_weight = 1;
    auto tinyid = restored.CreateContainer(tiny);
    Check(static_cast<bool>(tinyid), "tiny container");
    ItemInstance too_heavy;
    too_heavy.definition = apple_id.Value();
    too_heavy.quantity = 1;
    too_heavy.location = {ItemLocationKind::Container, tinyid.Value(), {}, {}, {}};
    Check(!restored.CreateItem(too_heavy), "capacity checked on create");
    return 0;
}
