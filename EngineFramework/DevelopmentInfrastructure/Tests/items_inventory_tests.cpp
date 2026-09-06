#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include <algorithm>
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

    Check(static_cast<bool>(restored.SetContainerState(chest_id.Value(), ContainerState::Disabled)), "disable container");
    ItemInstance blocked_insert;
    blocked_insert.definition = apple_id.Value();
    blocked_insert.quantity = 1;
    blocked_insert.location = {ItemLocationKind::Container, chest_id.Value(), {}, {}, {}};
    Check(!restored.CreateItem(blocked_insert), "disabled container rejects insert");
    Check(static_cast<bool>(restored.SetContainerState(chest_id.Value(), ContainerState::Active)), "enable container");
    Check(!restored.RemoveContainer(chest_id.Value()), "nonempty container removal rejected");
    ContainerRecord empty_container;
    empty_container.owner_object = Ref("actor", "empty_container_owner");
    auto empty_container_id = restored.CreateContainer(empty_container);
    Check(static_cast<bool>(empty_container_id), "create empty container");
    Check(static_cast<bool>(restored.RemoveContainer(empty_container_id.Value())), "remove empty container");
    Check(restored.FindContainer(empty_container_id.Value()) == nullptr, "removed container is gone");

    ItemInstance world_stack;
    world_stack.definition = apple_id.Value();
    world_stack.quantity = 5;
    world_stack.location.kind = ItemLocationKind::World;
    world_stack.location.world_object = Ref("world", "apple_stack");
    auto world_stack_id = restored.CreateItem(world_stack);
    Check(static_cast<bool>(world_stack_id), "create world stack");
    Check(!restored.SplitStack(world_stack_id.Value(), 1), "generic split of world stack rejected");
    ItemInstance duplicate_world;
    duplicate_world.definition = apple_id.Value();
    duplicate_world.quantity = 1;
    duplicate_world.location.kind = ItemLocationKind::World;
    duplicate_world.location.world_object = Ref("world", "apple_stack");
    Check(!restored.CreateItem(duplicate_world), "duplicate world object rejected");
    ItemLocation explicit_world_target;
    explicit_world_target.kind = ItemLocationKind::World;
    explicit_world_target.world_object = Ref("world", "apple_piece");
    auto world_transfer = restored.PrepareTransfer(world_stack_id.Value(), explicit_world_target, 2);
    Check(static_cast<bool>(world_transfer), "prepare explicit world split transfer");
    Check(static_cast<bool>(restored.CommitTransfer(world_transfer.Value())), "commit explicit world split transfer");
    Check(restored.FindItem(world_stack_id.Value())->quantity == 3, "source world stack keeps remaining quantity");
    bool found_piece = false;
    for (const auto& item : restored.FindItemsByDefinition(apple_id.Value()))
    {
        if (item.location.kind == ItemLocationKind::World && item.location.world_object == explicit_world_target.world_object && item.quantity == 2)
        {
            found_piece = true;
        }
    }
    Check(found_piece, "partial world transfer uses explicit target world object");

    auto invalid_generator_snapshot = restored.CaptureSnapshot();
    Check(!invalid_generator_snapshot.items.empty(), "snapshot has items for generator regression");
    invalid_generator_snapshot.item_ids.next = invalid_generator_snapshot.items.front().id.value.Low();
    ItemsInventoryService invalid_generator_restore;
    Check(static_cast<bool>(invalid_generator_restore.RegisterDefinition(apple)), "bad restore apple def");
    Check(static_cast<bool>(invalid_generator_restore.RegisterDefinition(sword)), "bad restore sword def");
    invalid_generator_restore.Freeze();
    Check(!invalid_generator_restore.RestoreSnapshot(invalid_generator_snapshot), "restore rejects item generator behind restored ids");

    auto duplicate_world_snapshot = restored.CaptureSnapshot();
    auto duplicate_record = *std::find_if(duplicate_world_snapshot.items.begin(), duplicate_world_snapshot.items.end(), [](const ItemInstance& item) {
        return item.location.kind == ItemLocationKind::World;
    });
    duplicate_record.id = ItemInstanceId::FromRaw(0x9999, 1);
    duplicate_world_snapshot.items.push_back(duplicate_record);
    ItemsInventoryService duplicate_world_restore;
    Check(static_cast<bool>(duplicate_world_restore.RegisterDefinition(apple)), "duplicate world restore apple def");
    Check(static_cast<bool>(duplicate_world_restore.RegisterDefinition(sword)), "duplicate world restore sword def");
    duplicate_world_restore.Freeze();
    Check(!duplicate_world_restore.RestoreSnapshot(duplicate_world_snapshot), "restore rejects duplicate world object binding");

    auto over_capacity_snapshot = restored.CaptureSnapshot();
    for (auto& container : over_capacity_snapshot.containers)
    {
        if (container.id == chest_id.Value())
        {
            container.max_slots = 0;
            container.max_weight = 1;
        }
    }
    ItemsInventoryService over_capacity_restore;
    Check(static_cast<bool>(over_capacity_restore.RegisterDefinition(apple)), "over capacity restore apple def");
    Check(static_cast<bool>(over_capacity_restore.RegisterDefinition(sword)), "over capacity restore sword def");
    over_capacity_restore.Freeze();
    Check(!over_capacity_restore.RestoreSnapshot(over_capacity_snapshot), "restore rejects capacity violating container");

    ItemsInventoryService journal_service;
    Check(static_cast<bool>(journal_service.RegisterDefinition(apple)), "journal apple def");
    journal_service.Freeze();
    for (int i = 0; i < 8300; ++i)
    {
        ContainerRecord c;
        c.owner_object = Ref("actor", "journal_owner");
        Check(static_cast<bool>(journal_service.CreateContainer(c)), "journal container create");
    }
    auto old_batch = journal_service.ReadChangesSince(0);
    Check(old_batch.snapshot_required, "bounded journal requires snapshot for stale sequence");

    return 0;
}
