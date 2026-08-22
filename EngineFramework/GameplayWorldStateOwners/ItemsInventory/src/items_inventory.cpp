#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::items
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}

[[nodiscard]] bool CheckedAdd(Fixed a, Fixed b, Fixed& out) noexcept
{
    if ((b > 0 && a > std::numeric_limits<Fixed>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Fixed>::min() - b)) return false;
    out = a + b;
    return true;
}
[[nodiscard]] bool CheckedMul(Fixed a, Fixed b, Fixed& out) noexcept
{
    if (a == 0 || b == 0) { out = 0; return true; }
    if (a == -1 && b == std::numeric_limits<Fixed>::min()) return false;
    if (b == -1 && a == std::numeric_limits<Fixed>::min()) return false;
    if (a > 0)
    {
        if ((b > 0 && a > std::numeric_limits<Fixed>::max() / b) ||
            (b < 0 && b < std::numeric_limits<Fixed>::min() / a)) return false;
    }
    else
    {
        if ((b > 0 && a < std::numeric_limits<Fixed>::min() / b) ||
            (b < 0 && a < std::numeric_limits<Fixed>::max() / b)) return false;
    }
    out = a * b;
    return true;
}
} // namespace

ItemsInventoryService::ItemsInventoryService() = default;
foundation::Result<ItemDefinitionId> ItemsInventoryService::RegisterDefinition(ItemDefinition d)
{
    if (frozen_)
        return foundation::Result<ItemDefinitionId>::Failure(
            Error("gameplay.items.frozen", "item definitions are frozen"));
    if (d.canonical_name.empty() && !d.id.IsValid())
        return foundation::Result<ItemDefinitionId>::Failure(
            Error("gameplay.items.invalid_definition", "item definition requires an id or canonical name"));
    if (!d.id.IsValid())
        d.id = ItemDefinitionId::FromString(d.canonical_name);
    if (definitions_.contains(d.id))
        return foundation::Result<ItemDefinitionId>::Failure(
            Error("gameplay.items.duplicate_definition", "duplicate item definition"));
    const auto expected = ItemDefinitionId::FromString(d.canonical_name);
    if (!d.canonical_name.empty() && d.id.IsValid() && d.id != expected)
        return foundation::Result<ItemDefinitionId>::Failure(Error("gameplay.items.definition_id_mismatch", "item definition id does not match canonical name"));
    if (d.stack_policy == ItemStackPolicy::CustomRegistered)
        return foundation::Result<ItemDefinitionId>::Failure(Error("gameplay.items.unsupported_stack_policy", "custom stack policy requires an explicit comparator contract"));
    if (d.base_weight < 0 || d.base_volume < 0 || d.max_durability < 0 || d.max_charges < 0 ||
        d.default_durability < 0 || d.default_charges < 0 || d.default_durability > d.max_durability ||
        d.default_charges > d.max_charges)
        return foundation::Result<ItemDefinitionId>::Failure(
            Error("gameplay.items.invalid_definition", "negative item definition values"));
    d.revision = Revision{1};
    const auto id = d.id;
    definitions_.emplace(id, std::move(d));
    return foundation::Result<ItemDefinitionId>::Success(id);
}
foundation::Result<void> ItemsInventoryService::RegisterPropertySchema(ItemPropertySchema schema)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.items.frozen", "item definitions are frozen"));
    if (!schema.type.IsValid() || !schema.payload_type.IsValid() || schema.schema_version == 0 ||
        schema.max_payload_bytes == 0 || property_schemas_.contains(schema.type))
        return foundation::Result<void>::Failure(Error("gameplay.items.invalid_property_schema", "invalid or duplicate item property schema"));
    property_schemas_.emplace(schema.type, std::move(schema));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ItemsInventoryService::RegisterContainerPolicy(const IContainerPolicy& policy)
{
    if (frozen_) return foundation::Result<void>::Failure(Error("gameplay.items.frozen", "item definitions are frozen"));
    if (!policy.Id().IsValid() || container_policies_.contains(policy.Id()))
        return foundation::Result<void>::Failure(Error("gameplay.items.invalid_container_policy", "invalid or duplicate container policy"));
    container_policies_.emplace(policy.Id(), &policy);
    return foundation::Result<void>::Success();
}

const ItemDefinition *ItemsInventoryService::FindDefinition(ItemDefinitionId id) const noexcept
{
    const auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}
foundation::Result<ContainerId> ItemsInventoryService::CreateContainer(ContainerRecord c)
{
    if (!frozen_) return foundation::Result<ContainerId>::Failure(Error("gameplay.registry_not_frozen", "item registry must be frozen before runtime mutation"));
    if (c.max_weight < 0 || c.max_volume < 0)
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.invalid_container", "container capacities must be non-negative"));
    if (c.parent.IsValid() && !containers_.contains(c.parent))
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.parent_missing", "parent container is missing"));
    if (c.policy.IsValid() && !container_policies_.contains(c.policy))
        return foundation::Result<ContainerId>::Failure(Error("gameplay.items.container_policy_missing", "container policy is not registered"));
    if (!c.id.IsValid()) c.id = ContainerId{container_ids_.Next()};
    if (!c.id.IsValid() || containers_.contains(c.id))
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.duplicate_container", "duplicate container"));
    if (c.parent == c.id)
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.container_cycle", "container cannot parent itself"));
    Bump();
    c.revision = revision_;
    const auto id = c.id;
    containers_.emplace(id, std::move(c));
    ++diagnostics_.containers;
    Record({0, ItemChangeKind::ContainerCreated, {}, id, 0, {}, revision_});
    return foundation::Result<ContainerId>::Success(id);
}
foundation::Result<ItemInstanceId> ItemsInventoryService::CreateItem(ItemInstance item, GameplayContext context)
{
    const auto* def = FindDefinition(item.definition);
    if (def)
    {
        if (def->durability_policy == ItemDurabilityPolicy::InstanceValue && item.durability == 0)
            item.durability = def->default_durability;
        if (def->charge_policy == ItemChargePolicy::InstanceValue && item.charges == 0)
            item.charges = def->default_charges;
    }
    ItemCreateRequest request;
    request.item = std::move(item);
    request.durability_override = request.item.durability;
    request.charges_override = request.item.charges;
    return CreateItem(std::move(request), context);
}

foundation::Result<ItemInstanceId> ItemsInventoryService::CreateItem(ItemCreateRequest request, GameplayContext context)
{
    ItemInstance item = std::move(request.item);
    if (!frozen_) return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.registry_not_frozen", "item registry must be frozen before runtime mutation"));
    const auto *def = FindDefinition(item.definition);
    if (!def)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.definition_missing", "item definition missing"));
    if (def->durability_policy == ItemDurabilityPolicy::InstanceValue)
        item.durability = request.durability_override.value_or(def->default_durability);
    if (def->charge_policy == ItemChargePolicy::InstanceValue)
        item.charges = request.charges_override.value_or(def->default_charges);
    if (item.quantity <= 0)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.invalid_quantity", "item quantity must be positive"));
    if (def->stack_policy == ItemStackPolicy::NonStackable && item.quantity != 1)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.nonstackable_quantity", "non-stackable item quantity must be one"));
    if (!ValidateLocation(item.location) || !ValidateProperties(item.properties))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_item_state", "item location or properties are invalid"));
    if (item.world_entity.has_value() && *item.world_entity != item.location.world_object)
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.duplicate_world_identity", "world_entity must match location.world_object"));
    if (def->durability_policy == ItemDurabilityPolicy::InstanceValue && (item.durability < 0 || item.durability > def->max_durability))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_durability", "item durability is outside definition bounds"));
    if (def->charge_policy == ItemChargePolicy::InstanceValue && (item.charges < 0 || item.charges > def->max_charges))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_charges", "item charges are outside definition bounds"));
    if (item.location.kind == ItemLocationKind::Container)
    {
        const auto *container = FindContainer(item.location.container);
        if (!container || container->state != ContainerState::Active)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_reject", "target container rejects item"));
        auto usage = GetContainerUsage(item.location.container);
        Fixed added_weight = 0, added_volume = 0, final_weight = 0, final_volume = 0;
        if (!CheckedMul(def->base_weight, item.quantity, added_weight) || !CheckedMul(def->base_volume, item.quantity, added_volume) ||
            !CheckedAdd(usage.weight, added_weight, final_weight) || !CheckedAdd(usage.volume, added_volume, final_volume))
            return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.capacity_overflow", "container usage arithmetic overflow"));
        usage.weight = final_weight;
        usage.volume = final_volume;
        ++usage.slots;
        if (!PolicyAllows(item.location.container, item.id, item.definition, item.quantity) ||
            (container->max_weight > 0 && usage.weight > container->max_weight) ||
            (container->max_volume > 0 && usage.volume > container->max_volume) ||
            (container->max_slots > 0 && usage.slots > container->max_slots))
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_capacity", "target container capacity exceeded"));
    }
    if (!item.id.IsValid()) item.id = ItemInstanceId{item_ids_.Next()};
    if (!item.id.IsValid() || items_.contains(item.id))
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.duplicate_item", "duplicate item instance"));
    Bump();
    item.revision = revision_;
    if (!ValidateLocation(item.location) || !ValidateProperties(item.properties))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_item_state", "item location or properties are invalid"));
    if (item.world_entity.has_value() && *item.world_entity != item.location.world_object)
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.duplicate_world_identity", "world_entity must match location.world_object"));
    if (def->durability_policy == ItemDurabilityPolicy::InstanceValue && (item.durability < 0 || item.durability > def->max_durability))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_durability", "item durability is outside definition bounds"));
    if (def->charge_policy == ItemChargePolicy::InstanceValue && (item.charges < 0 || item.charges > def->max_charges))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_charges", "item charges are outside definition bounds"));
    if (item.location.kind == ItemLocationKind::Container)
    {
        if (auto container = containers_.find(item.location.container); container != containers_.end())
        {
            container->second.revision = revision_;
        }
    }
    const auto id = item.id;
    items_.emplace(id, std::move(item));
    ++diagnostics_.items;
    Record({0, ItemChangeKind::ItemCreated, id, {}, items_.at(id).quantity, context, revision_});
    return foundation::Result<ItemInstanceId>::Success(id);
}
foundation::Result<void> ItemsInventoryService::DestroyItem(ItemInstanceId id, GameplayContext context)
{
    auto it = items_.find(id);
    if (it == items_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.item_missing", "item missing"));
    for (const auto &[rid, r] : reservations_)
        if (r.item == id && r.state == ReservationState::Active)
        {
            (void)rid;
            return foundation::Result<void>::Failure(
                Error("gameplay.items.item_reserved", "reserved item cannot be destroyed"));
        }
    const auto old_location = it->second.location;
    const auto old_quantity = it->second.quantity;
    items_.erase(it);
    Bump();
    if (old_location.kind == ItemLocationKind::Container)
    {
        if (auto container = containers_.find(old_location.container); container != containers_.end())
        {
            container->second.revision = revision_;
        }
    }
    if (diagnostics_.items > 0)
        --diagnostics_.items;
    ItemChange change{0, ItemChangeKind::ItemDestroyed, id, {}, old_quantity, context, revision_};
    change.source = old_location;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}
const ItemInstance *ItemsInventoryService::FindItem(ItemInstanceId id) const noexcept
{
    const auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}
const ContainerRecord *ItemsInventoryService::FindContainer(ContainerId id) const noexcept
{
    const auto it = containers_.find(id);
    return it == containers_.end() ? nullptr : &it->second;
}
std::optional<ItemInstance> ItemsInventoryService::FindItemCopy(ItemInstanceId id) const noexcept
{
    const auto* value = FindItem(id);
    return value ? std::optional<ItemInstance>{*value} : std::nullopt;
}
std::optional<ContainerRecord> ItemsInventoryService::FindContainerCopy(ContainerId id) const noexcept
{
    const auto* value = FindContainer(id);
    return value ? std::optional<ContainerRecord>{*value} : std::nullopt;
}
bool ItemsInventoryService::ValidateLocation(const ItemLocation& location) const noexcept
{
    switch (location.kind)
    {
    case ItemLocationKind::None:
        return !location.container.IsValid() && !location.world_object.IsValid();
    case ItemLocationKind::Container:
        return location.container.IsValid() && containers_.contains(location.container) && !location.world_object.IsValid();
    case ItemLocationKind::World:
        return location.world_object.IsValid() && !location.container.IsValid();
    default:
        return false;
    }
}
bool ItemsInventoryService::ValidateProperties(std::vector<ItemProperty>& properties) const
{
    std::sort(properties.begin(), properties.end(), [](const auto& a, const auto& b){ return a.type < b.type; });
    for (std::size_t i = 0; i < properties.size(); ++i)
    {
        const auto schema = property_schemas_.find(properties[i].type);
        if (schema == property_schemas_.end() || properties[i].payload.type != schema->second.payload_type ||
            properties[i].payload.schema_version != schema->second.schema_version ||
            properties[i].payload.bytes.size() > schema->second.max_payload_bytes) return false;
        if (i > 0 && properties[i-1].type == properties[i].type) return false;
    }
    return true;
}
bool ItemsInventoryService::PolicyAllows(ContainerId container, ItemInstanceId moving, ItemDefinitionId definition, Fixed quantity) const
{
    const auto c = containers_.find(container);
    if (c == containers_.end() || !c->second.policy.IsValid()) return true;
    const auto policy = container_policies_.find(c->second.policy);
    return policy != container_policies_.end() && policy->second &&
           policy->second->AllowsInsert(ContainerPolicyContext{container, moving, definition, quantity});
}
bool ItemsInventoryService::EquivalentForStack(const ItemInstance &a, const ItemInstance &b) const noexcept
{
    if (a.definition != b.definition)
        return false;
    const auto *def = FindDefinition(a.definition);
    if (!def || def->stack_policy == ItemStackPolicy::NonStackable)
        return false;
    if (def->stack_policy == ItemStackPolicy::StackByDefinition)
        return def->durability_policy == ItemDurabilityPolicy::None && def->charge_policy == ItemChargePolicy::None &&
               a.properties.empty() && b.properties.empty();
    return a.durability == b.durability && a.charges == b.charges && a.properties == b.properties;
}
foundation::Result<ItemInstanceId> ItemsInventoryService::SplitStack(ItemInstanceId id, Fixed quantity,
                                                                     GameplayContext context)
{
    auto it = items_.find(id);
    if (it == items_.end() || quantity <= 0 || quantity >= it->second.quantity)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.invalid_split", "invalid stack split"));
    if (ReservedQuantity(id) > it->second.quantity - quantity)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.reservation_conflict", "split would invalidate reservations"));
    if (it->second.location.kind == ItemLocationKind::Container)
    {
        const auto container = containers_.find(it->second.location.container);
        if (container == containers_.end() || container->second.state != ContainerState::Active)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_reject", "source container is invalid"));
        const auto usage = GetContainerUsage(it->second.location.container);
        if (container->second.max_slots > 0 && usage.slots + 1 > container->second.max_slots)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_capacity", "split would exceed container slot capacity"));
    }
    ItemInstance copy = it->second;
    copy.id = ItemInstanceId{item_ids_.Next()};
    if (!copy.id.IsValid())
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.id_exhausted", "item id generator is exhausted"));
    copy.quantity = quantity;
    Bump();
    it->second.quantity -= quantity;
    it->second.revision = revision_;
    copy.revision = revision_;
    if (it->second.location.kind == ItemLocationKind::Container)
    {
        if (auto container = containers_.find(it->second.location.container); container != containers_.end())
        {
            container->second.revision = revision_;
        }
    }
    const auto new_id = copy.id;
    items_.emplace(new_id, std::move(copy));
    ++diagnostics_.items;
    Record({0, ItemChangeKind::StackSplit, id, {}, quantity, context, revision_});
    return foundation::Result<ItemInstanceId>::Success(new_id);
}
foundation::Result<void> ItemsInventoryService::MergeStacks(ItemInstanceId target, ItemInstanceId source,
                                                            GameplayContext context)
{
    if (target == source)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.invalid_merge", "cannot merge item with itself"));
    auto t = items_.find(target), s = items_.find(source);
    if (t == items_.end() || s == items_.end() || t->second.location != s->second.location || !EquivalentForStack(t->second, s->second))
        return foundation::Result<void>::Failure(
            Error("gameplay.items.merge_incompatible", "item stacks are incompatible"));
    if (ReservedQuantity(source) > 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.item_reserved", "reserved source stack cannot be merged"));
    Fixed merged_quantity = 0;
    if (!CheckedAdd(t->second.quantity, s->second.quantity, merged_quantity))
        return foundation::Result<void>::Failure(Error("gameplay.items.quantity_overflow", "merged stack quantity would overflow"));
    Bump();
    t->second.quantity = merged_quantity;
    t->second.revision = revision_;
    if (t->second.location.kind == ItemLocationKind::Container)
    {
        if (auto container = containers_.find(t->second.location.container); container != containers_.end())
        {
            container->second.revision = revision_;
        }
    }
    const auto moved = s->second.quantity;
    items_.erase(s);
    if (diagnostics_.items > 0)
        --diagnostics_.items;
    Record({0, ItemChangeKind::StacksMerged, target, {}, moved, context, revision_});
    return foundation::Result<void>::Success();
}
std::uint32_t ItemsInventoryService::ContainerDepth(ContainerId id) const noexcept
{
    std::uint32_t depth = 0;
    ContainerId cursor = id;
    while (cursor.IsValid() && depth < 1024)
    {
        const auto *c = FindContainer(cursor);
        if (!c)
            break;
        ++depth;
        cursor = c->parent;
    }
    return depth;
}
ContainerUsage ItemsInventoryService::GetContainerUsage(ContainerId id) const noexcept
{
    ContainerUsage out;
    for (const auto &[iid, item] : items_)
    {
        (void)iid;
        if (item.location.kind != ItemLocationKind::Container || item.location.container != id)
            continue;
        const auto *def = FindDefinition(item.definition);
        if (!def)
            continue;
        Fixed item_weight = 0, item_volume = 0, next_weight = 0, next_volume = 0;
        if (!CheckedMul(def->base_weight, item.quantity, item_weight) ||
            !CheckedMul(def->base_volume, item.quantity, item_volume) ||
            !CheckedAdd(out.weight, item_weight, next_weight) ||
            !CheckedAdd(out.volume, item_volume, next_volume))
        {
            out.weight = std::numeric_limits<Fixed>::max();
            out.volume = std::numeric_limits<Fixed>::max();
        }
        else
        {
            out.weight = next_weight;
            out.volume = next_volume;
        }
        if (out.slots != std::numeric_limits<std::uint32_t>::max()) ++out.slots;
    }
    return out;
}
bool ItemsInventoryService::ValidateContainerTarget(ItemInstanceId moving, ContainerId target,
                                                    Fixed quantity) const noexcept
{
    const auto *c = FindContainer(target);
    if (!c || c->state != ContainerState::Active || quantity <= 0)
        return false;
    const auto *item = moving.IsValid() ? FindItem(moving) : nullptr;
    ContainerUsage usage = GetContainerUsage(target);
    if (item && item->location.kind == ItemLocationKind::Container && item->location.container == target)
    {
        const auto *def = FindDefinition(item->definition);
        if (def)
        {
            Fixed existing_weight = 0, existing_volume = 0;
            if (!CheckedMul(def->base_weight, item->quantity, existing_weight) ||
                !CheckedMul(def->base_volume, item->quantity, existing_volume))
                return false;
            usage.weight -= existing_weight;
            usage.volume -= existing_volume;
            if (usage.slots > 0)
                --usage.slots;
        }
    }
    if (item)
    {
        const auto *def = FindDefinition(item->definition);
        if (!def)
            return false;
        Fixed added_weight = 0, added_volume = 0, next_weight = 0, next_volume = 0;
        if (!CheckedMul(def->base_weight, quantity, added_weight) ||
            !CheckedMul(def->base_volume, quantity, added_volume) ||
            !CheckedAdd(usage.weight, added_weight, next_weight) ||
            !CheckedAdd(usage.volume, added_volume, next_volume) ||
            usage.slots == std::numeric_limits<std::uint32_t>::max())
            return false;
        usage.weight = next_weight;
        usage.volume = next_volume;
        ++usage.slots;
    }
    if (c->max_weight > 0 && usage.weight > c->max_weight)
        return false;
    if (c->max_volume > 0 && usage.volume > c->max_volume)
        return false;
    if (c->max_slots > 0 && usage.slots > c->max_slots)
        return false;
    return ContainerDepth(target) <= c->max_nesting_depth;
}
bool ItemsInventoryService::CanTransfer(ItemInstanceId id, ItemLocation target, Fixed quantity) const noexcept
{
    const auto *item = FindItem(id);
    if (!ValidateLocation(target)) return false;
    if (!item || quantity <= 0 || quantity > item->quantity || quantity > item->quantity - ReservedQuantity(id))
        return false;
    if (target.kind == ItemLocationKind::Container)
        return ValidateContainerTarget(id, target.container, quantity);
    return target.kind != ItemLocationKind::Destroyed;
}
Revision ItemsInventoryService::LocationRevision(const ItemLocation &l) const noexcept
{
    if (l.kind == ItemLocationKind::Container)
    {
        const auto *c = FindContainer(l.container);
        return c ? c->revision : Revision{};
    }
    return Revision{};
}
foundation::Result<ItemTransferPlan> ItemsInventoryService::PrepareTransfer(ItemInstanceId id, ItemLocation target,
                                                                            Fixed quantity, GameplayContext context)
{
    const auto *item = FindItem(id);
    if (!item || !CanTransfer(id, target, quantity))
    {
        ++diagnostics_.rejected_transfers;
        return foundation::Result<ItemTransferPlan>::Failure(
            Error("gameplay.items.transfer_invalid", "item transfer is not valid"));
    }
    ItemTransferPlan p;
    p.id = ItemTransferId{transfer_ids_.Next()};
    if (!p.id.IsValid())
        return foundation::Result<ItemTransferPlan>::Failure(Error("gameplay.items.id_exhausted", "transfer id generator is exhausted"));
    p.item = id;
    p.source = item->location;
    p.target = std::move(target);
    p.quantity = quantity;
    p.item_revision = item->revision;
    p.source_revision = LocationRevision(p.source);
    p.target_revision = LocationRevision(p.target);
    p.context = context;
    return foundation::Result<ItemTransferPlan>::Success(std::move(p));
}
foundation::Result<void> ItemsInventoryService::CommitTransfer(const ItemTransferPlan &p)
{
    auto it = items_.find(p.item);
    if (it == items_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.item_missing", "item missing"));
    if (!p.id.IsValid() || p.quantity <= 0 || p.source == p.target ||
        it->second.revision != p.item_revision || LocationRevision(p.source) != p.source_revision ||
        LocationRevision(p.target) != p.target_revision || it->second.location != p.source ||
        !CanTransfer(p.item, p.target, p.quantity))
        return foundation::Result<void>::Failure(Error("gameplay.items.stale_transfer", "item transfer plan is stale"));
    if (p.quantity < it->second.quantity)
    {
        ItemInstance moved = it->second;
        moved.id = ItemInstanceId{item_ids_.Next()};
        if (!moved.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.items.id_exhausted", "item id generator is exhausted"));
        moved.quantity = p.quantity;
        moved.location = p.target;
        Bump();
        it->second.quantity -= p.quantity;
        it->second.revision = revision_;
        moved.revision = revision_;
        const auto moved_id = moved.id;
        items_.emplace(moved_id, std::move(moved));
        ++diagnostics_.items;
        if (p.source.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(p.source.container); c != containers_.end()) c->second.revision = revision_;
        if (p.target.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(p.target.container); c != containers_.end()) c->second.revision = revision_;
        Record({0, ItemChangeKind::StackSplit, p.item, {}, p.quantity, p.context, revision_});
        Record({0, ItemChangeKind::TransferCommitted, moved_id, p.target.container, p.quantity, p.context, revision_});
    }
    else
    {
        Bump();
        it->second.location = p.target;
        it->second.revision = revision_;
        if (p.source.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(p.source.container); c != containers_.end()) c->second.revision = revision_;
        if (p.target.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(p.target.container); c != containers_.end()) c->second.revision = revision_;
        Record({0, ItemChangeKind::TransferCommitted, p.item, p.target.container, p.quantity, p.context, revision_});
    }
    ++diagnostics_.transfers;
    return foundation::Result<void>::Success();
}
Fixed ItemsInventoryService::ReservedQuantity(ItemInstanceId id) const noexcept
{
    Fixed out = 0;
    for (const auto &[rid, r] : reservations_)
    {
        (void)rid;
        if (r.item == id && r.state == ReservationState::Active)
        {
            Fixed next = 0;
            if (!CheckedAdd(out, r.quantity, next)) return std::numeric_limits<Fixed>::max();
            out = next;
        }
    }
    return out;
}
foundation::Result<ItemReservationId> ItemsInventoryService::ReserveItem(ItemInstanceId id, Fixed quantity,
                                                                         GameplayObjectRef owner, TypeId reason,
                                                                         GameplayContext context)
{
    const auto *item = FindItem(id);
    if (!item || quantity <= 0 || item->quantity - ReservedQuantity(id) < quantity)
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_unavailable", "requested item quantity is unavailable"));
    ItemReservation r;
    r.id = ItemReservationId{reservation_ids_.Next()};
    if (!r.id.IsValid()) return foundation::Result<ItemReservationId>::Failure(Error("gameplay.items.id_exhausted", "reservation id generator is exhausted"));
    r.item = id;
    r.quantity = quantity;
    r.owner = owner;
    r.reason = reason;
    Bump();
    r.revision = revision_;
    const auto rid = r.id;
    reservations_.emplace(rid, r);
    ++diagnostics_.active_reservations;
    Record({0, ItemChangeKind::ReservationCreated, id, {}, quantity, context, revision_});
    return foundation::Result<ItemReservationId>::Success(rid);
}
foundation::Result<void> ItemsInventoryService::ReleaseReservation(ItemReservationId id, GameplayContext context)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != ReservationState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.items.reservation_missing", "active reservation missing"));
    const auto copy = it->second;
    Bump();
    reservations_.erase(it);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    ItemChange change{0, ItemChangeKind::ReservationReleased, copy.item, {}, copy.quantity, context, revision_};
    change.reservation = copy.id;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ItemsInventoryService::ConsumeReservation(ItemReservationId id, GameplayContext context)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != ReservationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.reservation_missing", "active reservation missing"));
    auto item = items_.find(it->second.item);
    if (item == items_.end() || item->second.quantity < it->second.quantity)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.reservation_invalid", "reserved item no longer has enough quantity"));
    const auto qty = it->second.quantity;
    const auto iid = it->second.item;
    Bump();
    item->second.quantity -= qty;
    item->second.revision = revision_;

    if (item->second.quantity == 0)
    {
        items_.erase(item);
        if (diagnostics_.items > 0)
            --diagnostics_.items;
    }
    const auto rid = it->second.id;
    reservations_.erase(it);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    ItemChange change{0, ItemChangeKind::ReservationConsumed, iid, {}, qty, context, revision_};
    change.reservation = rid;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<ItemReservationId> ItemsInventoryService::ExchangeReservations(
    std::span<const ItemReservationId> release_reservations,
    ItemInstanceId reserve_item,
    Fixed reserve_quantity,
    GameplayObjectRef reserve_owner,
    TypeId reserve_reason,
    GameplayContext context)
{
    const auto item = items_.find(reserve_item);
    if (item == items_.end() || reserve_quantity <= 0)
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_unavailable", "requested item quantity is unavailable"));

    std::unordered_set<ItemReservationId, IdHash> unique_releases;
    unique_releases.reserve(release_reservations.size());
    Fixed released_from_reserved_item = 0;
    std::vector<ItemReservation> releases;
    releases.reserve(release_reservations.size());
    for (const auto rid : release_reservations)
    {
        if (!rid.IsValid() || !unique_releases.insert(rid).second)
            return foundation::Result<ItemReservationId>::Failure(
                Error("gameplay.items.reservation_exchange_invalid", "reservation exchange contains an invalid or duplicate release"));
        const auto found = reservations_.find(rid);
        if (found == reservations_.end() || found->second.state != ReservationState::Active)
            return foundation::Result<ItemReservationId>::Failure(
                Error("gameplay.items.reservation_missing", "reservation exchange requires active release reservations"));
        releases.push_back(found->second);
        if (found->second.item == reserve_item)
        {
            Fixed next = 0;
            if (!CheckedAdd(released_from_reserved_item, found->second.quantity, next))
                return foundation::Result<ItemReservationId>::Failure(
                    Error("gameplay.items.reservation_overflow", "reservation exchange release quantity overflow"));
            released_from_reserved_item = next;
        }
    }

    const Fixed currently_reserved = ReservedQuantity(reserve_item);
    if (currently_reserved == std::numeric_limits<Fixed>::max() && item->second.quantity != std::numeric_limits<Fixed>::max())
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_overflow", "reserved quantity overflow"));
    if (released_from_reserved_item > currently_reserved)
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_exchange_invalid", "released reservation quantity exceeds current reservation total"));
    const Fixed reserved_after_release = currently_reserved - released_from_reserved_item;
    if (reserved_after_release > item->second.quantity || item->second.quantity - reserved_after_release < reserve_quantity)
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_unavailable", "requested item quantity is unavailable after reservation exchange"));

    const ItemReservationId new_id{reservation_ids_.Next()};
    if (!new_id.IsValid())
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.id_exhausted", "reservation id generator is exhausted"));

    // All failure-prone work is complete. Commit the exchange under one Items revision.
    Bump();
    for (const auto& old : releases)
    {
        reservations_.erase(old.id);
        if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
        ItemChange change{0, ItemChangeKind::ReservationReleased, old.item, {}, old.quantity, context, revision_};
        change.reservation = old.id;
        Record(std::move(change));
    }

    ItemReservation created;
    created.id = new_id;
    created.item = reserve_item;
    created.quantity = reserve_quantity;
    created.owner = reserve_owner;
    created.reason = reserve_reason;
    created.revision = revision_;
    reservations_.emplace(new_id, created);
    ++diagnostics_.active_reservations;
    ItemChange created_change{0, ItemChangeKind::ReservationCreated, reserve_item, {}, reserve_quantity, context, revision_};
    created_change.reservation = new_id;
    Record(std::move(created_change));
    return foundation::Result<ItemReservationId>::Success(new_id);
}

const ItemReservation *ItemsInventoryService::FindReservation(ItemReservationId id) const noexcept
{
    const auto it = reservations_.find(id);
    return it == reservations_.end() ? nullptr : &it->second;
}
std::optional<ItemReservation> ItemsInventoryService::FindReservationCopy(ItemReservationId id) const noexcept
{
    const auto* value = FindReservation(id);
    return value ? std::optional<ItemReservation>{*value} : std::nullopt;
}
std::vector<ItemReservation> ItemsInventoryService::FindReservations(ItemInstanceId id) const
{
    std::vector<ItemReservation> out;
    for (const auto &[rid, r] : reservations_)
    {
        (void)rid;
        if (r.item == id)
            out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
foundation::Result<void> ItemsInventoryService::AdjustDurability(ItemInstanceId id, Fixed delta,
                                                                 GameplayContext context)
{
    auto it = items_.find(id);
    if (it == items_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.item_missing", "item missing"));
    const auto *d = FindDefinition(it->second.definition);
    if (!d || d->durability_policy == ItemDurabilityPolicy::None)
        return foundation::Result<void>::Failure(Error("gameplay.items.no_durability", "item has no durability"));
    Fixed requested = 0;
    if (!CheckedAdd(it->second.durability, delta, requested))
        requested = delta > 0 ? d->max_durability : 0;
    const Fixed next = std::clamp(requested, Fixed{0}, d->max_durability);
    if (next == it->second.durability) return foundation::Result<void>::Success();
    const Fixed actual_delta = next - it->second.durability;
    Bump();
    it->second.durability = next;
    it->second.revision = revision_;
    Record({0, ItemChangeKind::DurabilityChanged, id, {}, actual_delta, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ItemsInventoryService::AdjustCharges(ItemInstanceId id, Fixed delta, GameplayContext context)
{
    auto it = items_.find(id);
    if (it == items_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.item_missing", "item missing"));
    const auto *d = FindDefinition(it->second.definition);
    if (!d || d->charge_policy == ItemChargePolicy::None)
        return foundation::Result<void>::Failure(Error("gameplay.items.no_charges", "item has no charges"));
    Fixed requested = 0;
    if (!CheckedAdd(it->second.charges, delta, requested))
        requested = delta > 0 ? d->max_charges : 0;
    const Fixed next = std::clamp(requested, Fixed{0}, d->max_charges);
    if (next == it->second.charges) return foundation::Result<void>::Success();
    const Fixed actual_delta = next - it->second.charges;
    Bump();
    it->second.charges = next;
    it->second.revision = revision_;
    Record({0, ItemChangeKind::ChargesChanged, id, {}, actual_delta, context, revision_});
    return foundation::Result<void>::Success();
}
std::vector<ItemInstance> ItemsInventoryService::FindItemsInContainer(ContainerId c) const
{
    std::vector<ItemInstance> out;
    for (const auto &[id, i] : items_)
    {
        (void)id;
        if (i.location.kind == ItemLocationKind::Container && i.location.container == c)
            out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ItemInstance> ItemsInventoryService::FindItemsByDefinition(ItemDefinitionId d) const
{
    std::vector<ItemInstance> out;
    for (const auto &[id, i] : items_)
    {
        (void)id;
        if (i.definition == d)
            out.push_back(i);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ItemChange> ItemsInventoryService::ChangesSince(std::uint64_t seq) const
{
    std::vector<ItemChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
ItemsSnapshot ItemsInventoryService::CaptureSnapshot() const
{
    ItemsSnapshot s;
    for (const auto &[id, v] : items_)
    {
        (void)id;
        s.items.push_back(v);
    }
    for (const auto &[id, v] : containers_)
    {
        (void)id;
        s.containers.push_back(v);
    }
    for (const auto &[id, v] : reservations_)
    {
        (void)id;
        if (v.state == ReservationState::Active) s.reservations.push_back(v);
    }
    std::sort(s.items.begin(), s.items.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.containers.begin(), s.containers.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.reservations.begin(), s.reservations.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.item_ids = item_ids_.GetSnapshot();
    s.container_ids = container_ids_.GetSnapshot();
    s.transfer_ids = transfer_ids_.GetSnapshot();
    s.reservation_ids = reservation_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> ItemsInventoryService::RestoreSnapshot(ItemsSnapshot s)
{
    std::unordered_map<ContainerId, ContainerRecord, IdHash> containers;
    for (auto &v : s.containers)
    {
        if (!v.id.IsValid() || containers.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.items.restore_invalid", "invalid container snapshot"));
        containers.emplace(v.id, std::move(v));
    }
    std::unordered_map<ItemInstanceId, ItemInstance, IdHash> items;
    for (auto &v : s.items)
    {
        if (!v.id.IsValid() || !definitions_.contains(v.definition) || v.quantity <= 0 || items.contains(v.id) ||
            v.revision.value > s.revision.value)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "invalid item snapshot"));
        if ((v.location.kind == ItemLocationKind::Container && !containers.contains(v.location.container)) ||
            (v.location.kind == ItemLocationKind::World && !v.location.world_object.IsValid()) ||
            (v.location.kind != ItemLocationKind::None && v.location.kind != ItemLocationKind::Container && v.location.kind != ItemLocationKind::World))
            return foundation::Result<void>::Failure(
                Error("gameplay.items.restore_invalid", "item references missing container"));
        items.emplace(v.id, std::move(v));
    }
    std::unordered_map<ItemReservationId, ItemReservation, IdHash> reservations;
    for (auto &v : s.reservations)
    {
        if (!v.id.IsValid() || !items.contains(v.item) || reservations.contains(v.id) || v.state != ReservationState::Active ||
            v.quantity <= 0 || v.revision.value > s.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.items.restore_invalid", "invalid reservation snapshot"));
        reservations.emplace(v.id, std::move(v));
    }
    std::unordered_map<ItemInstanceId, Fixed, IdHash> reserved_sums;
    for (const auto& [rid, r] : reservations)
    {
        (void)rid;
        Fixed next = 0;
        if (!CheckedAdd(reserved_sums[r.item], r.quantity, next) || next > items.at(r.item).quantity)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "reservation quantities exceed item quantity"));
        reserved_sums[r.item] = next;
    }
    containers_ = std::move(containers);
    items_ = std::move(items);
    reservations_ = std::move(reservations);
    item_ids_.Restore(s.item_ids);
    container_ids_.Restore(s.container_ids);
    transfer_ids_.Restore(s.transfer_ids);
    reservation_ids_.Restore(s.reservation_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    diagnostics_.items = items_.size();
    diagnostics_.containers = containers_.size();
    for (const auto &[id, r] : reservations_)
    {
        (void)id;
        if (r.state == ReservationState::Active)
            ++diagnostics_.active_reservations;
    }
    return foundation::Result<void>::Success();
}
ItemsDiagnostics ItemsInventoryService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.items = items_.size();
    d.containers = containers_.size();
    return d;
}
void ItemsInventoryService::Record(ItemChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(std::move(c));
}
} // namespace epidemic::gameplay::items

