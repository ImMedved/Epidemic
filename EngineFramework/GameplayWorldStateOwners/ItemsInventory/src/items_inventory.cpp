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
    return foundation::Error::Create(std::string(c), std::string(m));
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


template <class TWrappedId>
void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId>& generator, TWrappedId id) noexcept
{
    if (!id.IsValid()) return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next) return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class TWrappedId>
void TrackMaxLowForScope(TWrappedId id, std::uint64_t scope, std::uint64_t& max_low) noexcept
{
    if (id.IsValid() && id.value.High() == scope && id.value.Low() > max_low)
    {
        max_low = id.value.Low();
    }
}

[[nodiscard]] bool IsKnownContainerState(ContainerState state) noexcept
{
    switch (state)
    {
    case ContainerState::Active:
    case ContainerState::Locked:
    case ContainerState::Disabled:
    case ContainerState::Destroyed:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsRuntimeContainerState(ContainerState state) noexcept
{
    return state == ContainerState::Active || state == ContainerState::Locked || state == ContainerState::Disabled;
}

[[nodiscard]] bool IsValid(ItemStackPolicy value) noexcept
{
    switch (value)
    {
    case ItemStackPolicy::NonStackable:
    case ItemStackPolicy::StackByDefinition:
    case ItemStackPolicy::StackByEquivalentState:
    case ItemStackPolicy::CustomRegistered:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsValid(ItemDurabilityPolicy value) noexcept
{
    return value == ItemDurabilityPolicy::None || value == ItemDurabilityPolicy::InstanceValue;
}

[[nodiscard]] bool IsValid(ItemChargePolicy value) noexcept
{
    return value == ItemChargePolicy::None || value == ItemChargePolicy::InstanceValue;
}

} // namespace

ItemsInventoryService::ItemsInventoryService() = default;

foundation::Result<Revision> ItemsInventoryService::PrepareRevision() const
{
    const auto next = CheckedNext(revision_);
    if (!next)
        return foundation::Result<Revision>::Failure(
            Error("gameplay.revision_exhausted", "items revision counter is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}
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
    if (!IsValid(d.stack_policy) || !IsValid(d.durability_policy) || !IsValid(d.charge_policy))
        return foundation::Result<ItemDefinitionId>::Failure(
            Error("gameplay.items.invalid_definition", "item definition contains an invalid policy enum"));
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
foundation::Result<ContainerId> ItemsInventoryService::CreateContainer(ContainerRecord container)
{
    if (!frozen_)
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.registry_not_frozen", "item registry must be frozen before runtime mutation"));
    if (container.max_weight < 0 || container.max_volume < 0 || !IsRuntimeContainerState(container.state))
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.invalid_container", "container capacities or state are invalid"));
    if (container.policy.IsValid() && !container_policies_.contains(container.policy))
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.container_policy_missing", "container policy is not registered"));

    auto staged_ids = container_ids_;
    const bool caller_id = container.id.IsValid();
    if (!caller_id) container.id = ContainerId{staged_ids.Next()};
    if (!container.id.IsValid() || containers_.contains(container.id))
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.duplicate_container", "duplicate container"));
    if (container.parent.IsValid())
    {
        const auto parent = containers_.find(container.parent);
        if (parent == containers_.end() || parent->second.state == ContainerState::Destroyed)
            return foundation::Result<ContainerId>::Failure(
                Error("gameplay.items.parent_missing", "parent container is missing or destroyed"));
        if (container.parent == container.id)
            return foundation::Result<ContainerId>::Failure(
                Error("gameplay.items.container_cycle", "container cannot parent itself"));
        if (ContainerDepth(container.parent) + 1u > container.max_nesting_depth)
            return foundation::Result<ContainerId>::Failure(
                Error("gameplay.items.container_depth", "container nesting depth exceeds maximum"));
    }
    if (caller_id) AdvanceGeneratorPastAcceptedId(staged_ids, container.id);
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<ContainerId>::Failure(revision.GetError());
    container.revision = revision.Value();
    const auto id = container.id;
    try
    {
        if (!containers_.emplace(id, std::move(container)).second)
            return foundation::Result<ContainerId>::Failure(
                Error("gameplay.items.duplicate_container", "duplicate container"));
    }
    catch (...)
    {
        return foundation::Result<ContainerId>::Failure(
            Error("gameplay.items.storage_failed", "failed to store item container"));
    }
    container_ids_ = staged_ids;
    revision_ = revision.Value();
    if (diagnostics_.containers != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.containers;
    Record({0, ItemChangeKind::ContainerCreated, {}, id, 0, {}, revision_});
    return foundation::Result<ContainerId>::Success(id);
}

foundation::Result<void> ItemsInventoryService::SetContainerState(ContainerId id, ContainerState state,
                                                                  GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "item registry must be frozen before runtime mutation"));
    if (!IsRuntimeContainerState(state))
        return foundation::Result<void>::Failure(Error("gameplay.items.invalid_container_state", "container state is not a runtime state"));
    auto it = containers_.find(id);
    if (it == containers_.end() || it->second.state == ContainerState::Destroyed)
        return foundation::Result<void>::Failure(Error("gameplay.items.container_missing", "container missing"));
    if (it->second.state == state) return foundation::Result<void>::Success();
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
    it->second.state = state;
    it->second.revision = revision_;
    ItemChange change{0, ItemChangeKind::ContainerStateChanged, {}, id, 0, context, revision_};
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> ItemsInventoryService::RemoveContainer(ContainerId id, GameplayContext context)
{
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "item registry must be frozen before runtime mutation"));
    auto it = containers_.find(id);
    if (it == containers_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.container_missing", "container missing"));
    if (auto found_items = container_items_.find(id); found_items != container_items_.end() && !found_items->second.empty())
        return foundation::Result<void>::Failure(Error("gameplay.items.container_not_empty", "container contains items"));
    for (const auto& [cid, container] : containers_)
    {
        (void)cid;
        if (container.parent == id)
            return foundation::Result<void>::Failure(Error("gameplay.items.container_not_empty", "container has child containers"));
    }
    const auto parent = it->second.parent;
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
    containers_.erase(it);
    if (parent.IsValid())
    {
        if (auto parent_it = containers_.find(parent); parent_it != containers_.end())
        {
            parent_it->second.revision = revision_;
        }
    }
    if (diagnostics_.containers > 0) --diagnostics_.containers;
    RebuildIndexes();
    ItemChange change{0, ItemChangeKind::ContainerRemoved, {}, id, 0, context, revision_};
    Record(std::move(change));
    return foundation::Result<void>::Success();
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

bool ItemsInventoryService::CanCreateItem(ItemDefinitionId definition_id, Fixed quantity, const ItemLocation& location) const
{
    const auto* definition = FindDefinition(definition_id);
    if (!definition || quantity <= 0 || !ValidateLocation(location)) return false;
    if (definition->stack_policy == ItemStackPolicy::NonStackable && quantity != 1) return false;
    if (location.kind == ItemLocationKind::World && IsWorldObjectBound(location.world_object)) return false;
    if (location.kind != ItemLocationKind::Container) return location.kind != ItemLocationKind::Destroyed;

    const auto* container = FindContainer(location.container);
    if (!container || container->state != ContainerState::Active) return false;
    auto usage = GetContainerUsage(location.container);
    Fixed added_weight = 0, added_volume = 0, final_weight = 0, final_volume = 0;
    if (!CheckedMul(definition->base_weight, quantity, added_weight) ||
        !CheckedMul(definition->base_volume, quantity, added_volume) ||
        !CheckedAdd(usage.weight, added_weight, final_weight) ||
        !CheckedAdd(usage.volume, added_volume, final_volume) ||
        usage.slots == std::numeric_limits<std::uint32_t>::max()) return false;
    ++usage.slots;
    return PolicyAllows(location.container, {}, definition_id, quantity) &&
           (container->max_weight <= 0 || final_weight <= container->max_weight) &&
           (container->max_volume <= 0 || final_volume <= container->max_volume) &&
           (container->max_slots == 0 || usage.slots <= container->max_slots);
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
    else if (item.durability != 0)
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_durability", "item definition does not allow durability state"));
    if (def->charge_policy == ItemChargePolicy::InstanceValue)
        item.charges = request.charges_override.value_or(def->default_charges);
    else if (item.charges != 0)
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_charges", "item definition does not allow charge state"));
    if (item.quantity <= 0)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.invalid_quantity", "item quantity must be positive"));
    if (def->stack_policy == ItemStackPolicy::NonStackable && item.quantity != 1)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.nonstackable_quantity", "non-stackable item quantity must be one"));
    if (!ValidateLocation(item.location) || !ValidateProperties(item.properties))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_item_state", "item location or properties are invalid"));
    if (def->durability_policy == ItemDurabilityPolicy::InstanceValue && (item.durability < 0 || item.durability > def->max_durability))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_durability", "item durability is outside definition bounds"));
    if (def->charge_policy == ItemChargePolicy::InstanceValue && (item.charges < 0 || item.charges > def->max_charges))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.invalid_charges", "item charges are outside definition bounds"));
    if (item.location.kind == ItemLocationKind::World && IsWorldObjectBound(item.location.world_object))
        return foundation::Result<ItemInstanceId>::Failure(Error("gameplay.items.duplicate_world_identity", "world object is already bound to an item"));
    if (item.location.kind == ItemLocationKind::Container)
    {
        const auto *container = FindContainer(item.location.container);
        if (!container || container->state != ContainerState::Active)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_reject", "target container rejects item"));
        auto usage = GetContainerUsage(item.location.container);
        Fixed added_weight = 0, added_volume = 0, final_weight = 0, final_volume = 0;
        if (!CheckedMul(def->base_weight, item.quantity, added_weight) || !CheckedMul(def->base_volume, item.quantity, added_volume) ||
            !CheckedAdd(usage.weight, added_weight, final_weight) || !CheckedAdd(usage.volume, added_volume, final_volume) ||
            usage.slots == std::numeric_limits<std::uint32_t>::max())
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
    auto staged_ids = item_ids_;
    const bool caller_supplied_id = item.id.IsValid();
    if (!item.id.IsValid()) item.id = ItemInstanceId{staged_ids.Next()};
    if (!item.id.IsValid() || items_.contains(item.id))
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.duplicate_item", "duplicate item instance"));
    if (caller_supplied_id) AdvanceGeneratorPastAcceptedId(staged_ids, item.id);
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<ItemInstanceId>::Failure(revision.GetError());
    item.revision = revision.Value();
    const auto id = item.id;
    const auto quantity = item.quantity;
    try
    {
        if (!items_.emplace(id, std::move(item)).second)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.duplicate_item", "duplicate item instance"));
    }
    catch (...)
    {
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.storage_failed", "failed to store item instance"));
    }
    item_ids_ = staged_ids;
    revision_ = revision.Value();
    if (items_.at(id).location.kind == ItemLocationKind::Container)
        if (auto container = containers_.find(items_.at(id).location.container); container != containers_.end())
            container->second.revision = revision_;
    if (diagnostics_.items != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.items;
    RebuildIndexes();
    Record({0, ItemChangeKind::ItemCreated, id, {}, quantity, context, revision_});
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
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    items_.erase(it);
    revision_ = revision.Value();
    if (old_location.kind == ItemLocationKind::Container)
    {
        if (auto container = containers_.find(old_location.container); container != containers_.end())
        {
            container->second.revision = revision_;
        }
    }
    if (diagnostics_.items > 0)
        --diagnostics_.items;
    RebuildIndexes();
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
        return !location.container.IsValid() && !location.world_object.IsValid() && !location.area.IsValid();
    case ItemLocationKind::Container:
        return location.container.IsValid() && containers_.contains(location.container) &&
               !location.world_object.IsValid() && !location.area.IsValid();
    case ItemLocationKind::World:
        return location.world_object.IsValid() && !location.container.IsValid();
    default:
        return false;
    }
}

bool ItemsInventoryService::IsWorldObjectBound(GameplayObjectRef world_object, ItemInstanceId except) const noexcept
{
    if (!world_object.IsValid()) return false;
    for (const auto& [id, item] : items_)
    {
        if (except.IsValid() && id == except) continue;
        if (item.location.kind == ItemLocationKind::World && item.location.world_object == world_object)
        {
            return true;
        }
    }
    return false;
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
    if (policy == container_policies_.end() || !policy->second) return false;
    try
    {
        return policy->second->AllowsInsert(ContainerPolicyContext{container, moving, definition, quantity});
    }
    catch (...)
    {
        return false;
    }
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
    if (it->second.location.kind == ItemLocationKind::World)
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.world_split_requires_target", "world item stack split requires an explicit target world object or container"));
    if (it->second.location.kind == ItemLocationKind::Container)
    {
        const auto container = containers_.find(it->second.location.container);
        if (container == containers_.end() || container->second.state != ContainerState::Active)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_reject", "source container is invalid"));
        const auto usage = GetContainerUsage(it->second.location.container);
        if (container->second.max_slots > 0 &&
            (usage.slots == std::numeric_limits<std::uint32_t>::max() || usage.slots + 1 > container->second.max_slots))
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.container_capacity", "split would exceed container slot capacity"));
    }
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<ItemInstanceId>::Failure(revision.GetError());
    auto staged_ids = item_ids_;
    ItemInstance copy = it->second;
    copy.id = ItemInstanceId{staged_ids.Next()};
    if (!copy.id.IsValid())
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.id_exhausted", "item id generator is exhausted"));
    copy.quantity = quantity;
    copy.revision = revision.Value();
    const auto new_id = copy.id;
    try
    {
        if (!items_.emplace(new_id, std::move(copy)).second)
            return foundation::Result<ItemInstanceId>::Failure(
                Error("gameplay.items.duplicate_item", "generated split item id already exists"));
    }
    catch (...)
    {
        return foundation::Result<ItemInstanceId>::Failure(
            Error("gameplay.items.storage_failed", "failed to store split item"));
    }
    item_ids_ = staged_ids;
    revision_ = revision.Value();
    it = items_.find(id);
    it->second.quantity -= quantity;
    it->second.revision = revision_;
    if (it->second.location.kind == ItemLocationKind::Container)
        if (auto container = containers_.find(it->second.location.container); container != containers_.end())
            container->second.revision = revision_;
    if (diagnostics_.items != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.items;
    RebuildIndexes();
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
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
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
    RebuildIndexes();
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
    std::vector<ItemInstanceId> fallback_ids;
    const std::vector<ItemInstanceId>* ids = nullptr;
    if (indexes_valid_)
    {
        const auto indexed = container_items_.find(id);
        if (indexed == container_items_.end()) return out;
        ids = &indexed->second;
    }
    else
    {
        for (const auto& [item_id, item] : items_)
            if (item.location.kind == ItemLocationKind::Container && item.location.container == id) fallback_ids.push_back(item_id);
        ids = &fallback_ids;
    }
    for (const auto item_id : *ids)
    {
        const auto found_item = items_.find(item_id);
        if (found_item == items_.end()) continue;
        const auto& item = found_item->second;
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
    if (target.kind == ItemLocationKind::World)
    {
        if (item->location.kind == ItemLocationKind::World && item->location.world_object == target.world_object)
            return false;
        if (IsWorldObjectBound(target.world_object, id))
            return false;
    }
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
    auto staged_ids = transfer_ids_;
    ItemTransferPlan p;
    p.id = ItemTransferId{staged_ids.Next()};
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
    transfer_ids_ = staged_ids;
    return foundation::Result<ItemTransferPlan>::Success(std::move(p));
}
foundation::Result<void> ItemsInventoryService::CommitTransfer(const ItemTransferPlan &plan)
{
    auto item = items_.find(plan.item);
    if (item == items_.end())
        return foundation::Result<void>::Failure(Error("gameplay.items.item_missing", "item missing"));
    if (!plan.id.IsValid() || plan.quantity <= 0 || plan.source == plan.target ||
        item->second.revision != plan.item_revision || LocationRevision(plan.source) != plan.source_revision ||
        LocationRevision(plan.target) != plan.target_revision || item->second.location != plan.source ||
        !CanTransfer(plan.item, plan.target, plan.quantity))
        return foundation::Result<void>::Failure(
            Error("gameplay.items.stale_transfer", "item transfer plan is stale"));
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());

    ItemInstanceId moved_id{};
    auto staged_item_ids = item_ids_;
    if (plan.quantity < item->second.quantity)
    {
        ItemInstance moved = item->second;
        moved.id = ItemInstanceId{staged_item_ids.Next()};
        if (!moved.id.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.items.id_exhausted", "item id generator is exhausted"));
        moved.quantity = plan.quantity;
        moved.location = plan.target;
        moved.revision = revision.Value();
        moved_id = moved.id;
        try
        {
            if (!items_.emplace(moved_id, std::move(moved)).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.items.duplicate_item", "generated transfer item id already exists"));
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.items.storage_failed", "failed to store partial transfer item"));
        }
    }

    revision_ = revision.Value();
    if (moved_id.IsValid())
    {
        item_ids_ = staged_item_ids;
        item = items_.find(plan.item);
        item->second.quantity -= plan.quantity;
        item->second.revision = revision_;
        if (diagnostics_.items != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.items;
        if (plan.source.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(plan.source.container); c != containers_.end()) c->second.revision = revision_;
        if (plan.target.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(plan.target.container); c != containers_.end()) c->second.revision = revision_;
        Record({0, ItemChangeKind::StackSplit, plan.item, {}, plan.quantity, plan.context, revision_});
        Record({0, ItemChangeKind::TransferCommitted, moved_id, plan.target.container, plan.quantity, plan.context, revision_});
    }
    else
    {
        item->second.location = plan.target;
        item->second.revision = revision_;
        if (plan.source.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(plan.source.container); c != containers_.end()) c->second.revision = revision_;
        if (plan.target.kind == ItemLocationKind::Container)
            if (auto c = containers_.find(plan.target.container); c != containers_.end()) c->second.revision = revision_;
        Record({0, ItemChangeKind::TransferCommitted, plan.item, plan.target.container, plan.quantity, plan.context, revision_});
    }
    if (diagnostics_.transfers != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.transfers;
    RebuildIndexes();
    return foundation::Result<void>::Success();
}

foundation::Result<void> ItemsInventoryService::CommitReservedTransfer(ItemReservationId reservation_id, ItemLocation target,
                                                                               GameplayContext context)
{
    const auto reservation_it = reservations_.find(reservation_id);
    if (reservation_it == reservations_.end() || reservation_it->second.state != ReservationState::Active)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.reservation_missing", "active reservation missing"));
    const auto reservation = reservation_it->second;
    auto item_it = items_.find(reservation.item);
    if (item_it == items_.end() || reservation.quantity <= 0 || item_it->second.quantity != reservation.quantity)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.reserved_transfer_invalid", "reserved trade transfer requires the complete item instance"));
    if (!ValidateLocation(target) || target == item_it->second.location || target.kind == ItemLocationKind::Destroyed)
        return foundation::Result<void>::Failure(
            Error("gameplay.items.reserved_transfer_invalid", "reserved item transfer target is invalid"));
    if (target.kind == ItemLocationKind::World)
    {
        if (item_it->second.location.kind == ItemLocationKind::World && item_it->second.location.world_object == target.world_object)
            return foundation::Result<void>::Failure(
                Error("gameplay.items.reserved_transfer_invalid", "reserved item is already bound to target world object"));
        if (IsWorldObjectBound(target.world_object, reservation.item))
            return foundation::Result<void>::Failure(
                Error("gameplay.items.duplicate_world_identity", "target world object is already bound"));
    }
    if (target.kind == ItemLocationKind::Container &&
        !ValidateContainerTarget(reservation.item, target.container, reservation.quantity))
        return foundation::Result<void>::Failure(
            Error("gameplay.items.container_capacity", "target container rejects reserved item transfer"));

    const auto source = item_it->second.location;
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
    reservations_.erase(reservation_it);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    item_it->second.location = target;
    item_it->second.revision = revision_;
    if (source.kind == ItemLocationKind::Container)
        if (auto container = containers_.find(source.container); container != containers_.end()) container->second.revision = revision_;
    if (target.kind == ItemLocationKind::Container)
        if (auto container = containers_.find(target.container); container != containers_.end()) container->second.revision = revision_;
    ++diagnostics_.transfers;
    RebuildIndexes();

    ItemChange released{0, ItemChangeKind::ReservationReleased, reservation.item, {}, reservation.quantity, context, revision_};
    released.reservation = reservation.id;
    Record(std::move(released));
    ItemChange moved{0, ItemChangeKind::TransferCommitted, reservation.item, target.container, reservation.quantity, context, revision_};
    moved.source = source;
    moved.target = target;
    Record(std::move(moved));
    return foundation::Result<void>::Success();
}

Fixed ItemsInventoryService::ReservedQuantity(ItemInstanceId id) const noexcept
{
    if (!indexes_valid_)
    {
        Fixed total = 0;
        for (const auto& [reservation_id, reservation] : reservations_)
        {
            (void)reservation_id;
            if (reservation.item != id || reservation.state != ReservationState::Active) continue;
            Fixed next = 0;
            if (!CheckedAdd(total, reservation.quantity, next)) return std::numeric_limits<Fixed>::max();
            total = next;
        }
        return total;
    }
    const auto found = active_reserved_quantities_.find(id);
    return found == active_reserved_quantities_.end() ? Fixed{0} : found->second;
}
foundation::Result<ItemReservationId> ItemsInventoryService::ReserveItem(ItemInstanceId id, Fixed quantity,
                                                                         GameplayObjectRef owner, TypeId reason,
                                                                         GameplayContext context)
{
    const auto *item = FindItem(id);
    if (!item || quantity <= 0 || item->quantity - ReservedQuantity(id) < quantity)
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.reservation_unavailable", "requested item quantity is unavailable"));
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<ItemReservationId>::Failure(revision.GetError());
    auto staged_ids = reservation_ids_;
    ItemReservation r;
    r.id = ItemReservationId{staged_ids.Next()};
    if (!r.id.IsValid()) return foundation::Result<ItemReservationId>::Failure(Error("gameplay.items.id_exhausted", "reservation id generator is exhausted"));
    r.item = id;
    r.quantity = quantity;
    r.owner = owner;
    r.reason = reason;
    r.revision = revision.Value();
    const auto rid = r.id;
    try
    {
        if (!reservations_.emplace(rid, r).second)
            return foundation::Result<ItemReservationId>::Failure(Error("gameplay.items.duplicate_reservation", "generated reservation id already exists"));
    }
    catch (...)
    {
        return foundation::Result<ItemReservationId>::Failure(Error("gameplay.items.storage_failed", "failed to store item reservation"));
    }
    reservation_ids_ = staged_ids;
    revision_ = revision.Value();
    if (diagnostics_.active_reservations != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.active_reservations;
    RebuildIndexes();
    Record({0, ItemChangeKind::ReservationCreated, id, {}, quantity, context, revision_});
    return foundation::Result<ItemReservationId>::Success(rid);
}
foundation::Result<void> ItemsInventoryService::ReleaseReservation(ItemReservationId id, GameplayContext context)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != ReservationState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.items.reservation_missing", "active reservation missing"));
    const auto copy = it->second;
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
    reservations_.erase(it);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    RebuildIndexes();
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
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
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
    RebuildIndexes();
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

    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<ItemReservationId>::Failure(revision.GetError());
    auto staged_ids = reservation_ids_;
    const ItemReservationId new_id{staged_ids.Next()};
    if (!new_id.IsValid())
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.id_exhausted", "reservation id generator is exhausted"));

    ItemReservation created;
    created.id = new_id;
    created.item = reserve_item;
    created.quantity = reserve_quantity;
    created.owner = reserve_owner;
    created.reason = reserve_reason;
    created.revision = revision.Value();
    std::unordered_map<ItemReservationId, ItemReservation, IdHash> staged_reservations;
    try
    {
        staged_reservations = reservations_;
        for (const auto& old : releases) staged_reservations.erase(old.id);
        staged_reservations.emplace(new_id, created);
    }
    catch (...)
    {
        return foundation::Result<ItemReservationId>::Failure(
            Error("gameplay.items.storage_failed", "failed to stage reservation exchange"));
    }

    reservations_.swap(staged_reservations);
    reservation_ids_ = staged_ids;
    revision_ = revision.Value();
    for (const auto& old : releases)
    {
        if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
        ItemChange change{0, ItemChangeKind::ReservationReleased, old.item, {}, old.quantity, context, revision_};
        change.reservation = old.id;
        Record(std::move(change));
    }
    if (diagnostics_.active_reservations != std::numeric_limits<std::uint64_t>::max()) ++diagnostics_.active_reservations;
    RebuildIndexes();
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
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
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
    auto revision = PrepareRevision();
    if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    revision_ = revision.Value();
    it->second.charges = next;
    it->second.revision = revision_;
    Record({0, ItemChangeKind::ChargesChanged, id, {}, actual_delta, context, revision_});
    return foundation::Result<void>::Success();
}
std::vector<ItemInstance> ItemsInventoryService::FindItemsInContainer(ContainerId c) const
{
    std::vector<ItemInstance> out;
    if (!indexes_valid_)
    {
        for (const auto& [id, item] : items_)
        {
            (void)id;
            if (item.location.kind == ItemLocationKind::Container && item.location.container == c) out.push_back(item);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        return out;
    }
    const auto indexed = container_items_.find(c);
    if (indexed == container_items_.end()) return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
    {
        if (const auto item = items_.find(id); item != items_.end()) out.push_back(item->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ItemInstance> ItemsInventoryService::FindItemsByDefinition(ItemDefinitionId d) const
{
    std::vector<ItemInstance> out;
    if (!indexes_valid_)
    {
        for (const auto& [id, item] : items_)
        {
            (void)id;
            if (item.definition == d) out.push_back(item);
        }
        std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
        return out;
    }
    const auto indexed = definition_items_.find(d);
    if (indexed == definition_items_.end()) return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
    {
        if (const auto item = items_.find(id); item != items_.end()) out.push_back(item->second);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ItemChange> ItemsInventoryService::ChangesSinceSequence(std::uint64_t seq) const
{
    return ReadChangesSinceSequence(seq).changes;
}

ItemsChangeBatch ItemsInventoryService::ReadChangesSinceSequence(std::uint64_t seq) const
{
    ItemsChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max() : next_change_sequence_ - 1;
    if (!changes_.empty() && seq < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    const auto found = std::upper_bound(changes_.begin(), changes_.end(), seq,
        [](std::uint64_t value, const ItemChange& change) { return value < change.sequence; });
    batch.changes.assign(found, changes_.end());
    return batch;
}

void ItemsInventoryService::PruneChangesBefore(std::uint64_t sequence)
{
    const auto found = std::lower_bound(changes_.begin(), changes_.end(), sequence,
        [](const ItemChange& change, std::uint64_t value) { return change.sequence < value; });
    changes_.erase(changes_.begin(), found);
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
    s.change_epoch = journal_epoch_;
    return s;
}
foundation::Result<void> ItemsInventoryService::RestoreSnapshot(ItemsSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    if (!frozen_) return foundation::Result<void>::Failure(Error("gameplay.registry_not_frozen", "item registry must be frozen before restore"));
    if (s.revision.value == 0 && (!s.items.empty() || !s.containers.empty() || !s.reservations.empty()))
        return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "non-empty snapshot requires a non-zero revision"));

    std::unordered_map<ContainerId, ContainerRecord, IdHash> containers;
    containers.reserve(s.containers.size());
    std::uint64_t max_container_low = 0;
    for (auto& v : s.containers)
    {
        if (!v.id.IsValid() || containers.contains(v.id) || v.revision.value > s.revision.value ||
            v.max_weight < 0 || v.max_volume < 0 || !IsKnownContainerState(v.state) ||
            (v.policy.IsValid() && !container_policies_.contains(v.policy)) || v.parent == v.id)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.items.restore_invalid", "invalid container snapshot"));
        }
        TrackMaxLowForScope(v.id, s.container_ids.scope, max_container_low);
        containers.emplace(v.id, std::move(v));
    }

    for (const auto& [id, container] : containers)
    {
        (void)id;
        if (container.parent.IsValid() && !containers.contains(container.parent))
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container parent is missing"));
        std::unordered_set<ContainerId, IdHash> seen;
        ContainerId cursor = container.parent;
        std::uint32_t depth = 1;
        while (cursor.IsValid())
        {
            if (!seen.insert(cursor).second)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container parent cycle"));
            const auto parent = containers.find(cursor);
            if (parent == containers.end() || parent->second.state == ContainerState::Destroyed)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container parent is missing or destroyed"));
            ++depth;
            cursor = parent->second.parent;
            if (depth > container.max_nesting_depth || depth > 1024)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container nesting depth exceeds maximum"));
        }
    }

    auto validate_location_against = [&containers](const ItemLocation& location) noexcept -> bool
    {
        switch (location.kind)
        {
        case ItemLocationKind::None:
            return !location.container.IsValid() && !location.world_object.IsValid() && !location.area.IsValid();
        case ItemLocationKind::Container:
            return location.container.IsValid() && containers.contains(location.container) &&
                   !location.world_object.IsValid() && !location.area.IsValid();
        case ItemLocationKind::World:
            return location.world_object.IsValid() && !location.container.IsValid();
        default:
            return false;
        }
    };

    std::unordered_map<ItemInstanceId, ItemInstance, IdHash> items;
    items.reserve(s.items.size());
    std::unordered_set<GameplayObjectRef> world_objects;
    std::uint64_t max_item_low = 0;
    for (auto& v : s.items)
    {
        const auto* definition = FindDefinition(v.definition);
        if (!v.id.IsValid() || definition == nullptr || v.quantity <= 0 || items.contains(v.id) ||
            v.revision.value > s.revision.value || !validate_location_against(v.location) || !ValidateProperties(v.properties))
        {
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "invalid item snapshot"));
        }
        if (definition->stack_policy == ItemStackPolicy::NonStackable && v.quantity != 1)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "non-stackable snapshot item has invalid quantity"));
        if (definition->durability_policy == ItemDurabilityPolicy::InstanceValue)
        {
            if (v.durability < 0 || v.durability > definition->max_durability)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item durability is outside definition bounds"));
        }
        else if (v.durability != 0)
        {
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item definition does not allow durability state"));
        }
        if (definition->charge_policy == ItemChargePolicy::InstanceValue)
        {
            if (v.charges < 0 || v.charges > definition->max_charges)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item charges are outside definition bounds"));
        }
        else if (v.charges != 0)
        {
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item definition does not allow charge state"));
        }
        if (v.location.kind == ItemLocationKind::Container)
        {
            const auto container = containers.find(v.location.container);
            if (container == containers.end() || container->second.state == ContainerState::Destroyed)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item references destroyed container"));
        }
        if (v.location.kind == ItemLocationKind::World)
        {
            if (!world_objects.insert(v.location.world_object).second)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "world object is bound to multiple items"));
        }
        TrackMaxLowForScope(v.id, s.item_ids.scope, max_item_low);
        items.emplace(v.id, std::move(v));
    }

    std::unordered_map<ContainerId, ContainerUsage, IdHash> usage_by_container;
    for (const auto& [item_id, item] : items)
    {
        if (item.location.kind != ItemLocationKind::Container) continue;
        const auto& container = containers.at(item.location.container);
        const auto* definition = FindDefinition(item.definition);
        if (!definition)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "item definition missing"));
        Fixed item_weight = 0;
        Fixed item_volume = 0;
        Fixed next_weight = 0;
        Fixed next_volume = 0;
        auto& usage = usage_by_container[item.location.container];
        if (!CheckedMul(definition->base_weight, item.quantity, item_weight) ||
            !CheckedMul(definition->base_volume, item.quantity, item_volume) ||
            !CheckedAdd(usage.weight, item_weight, next_weight) ||
            !CheckedAdd(usage.volume, item_volume, next_volume) ||
            usage.slots == std::numeric_limits<std::uint32_t>::max())
        {
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container usage arithmetic overflow"));
        }
        usage.weight = next_weight;
        usage.volume = next_volume;
        ++usage.slots;
        if (container.policy.IsValid())
        {
            const auto policy = container_policies_.find(container.policy);
            if (policy == container_policies_.end() || !policy->second)
            {
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container policy rejects restored item"));
            }
            bool allowed = false;
            try
            {
                allowed = policy->second->AllowsInsert(ContainerPolicyContext{item.location.container, item_id, item.definition, item.quantity});
            }
            catch (...)
            {
                allowed = false;
            }
            if (!allowed)
            {
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container policy rejects restored item"));
            }
        }
    }
    for (const auto& [container_id, usage] : usage_by_container)
    {
        const auto& container = containers.at(container_id);
        if ((container.max_weight > 0 && usage.weight > container.max_weight) ||
            (container.max_volume > 0 && usage.volume > container.max_volume) ||
            (container.max_slots > 0 && usage.slots > container.max_slots))
        {
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "container capacity exceeded in snapshot"));
        }
    }
    for (const auto& [container_id, container] : containers)
    {
        if (container.state != ContainerState::Destroyed) continue;
        if (const auto usage = usage_by_container.find(container_id); usage != usage_by_container.end() && usage->second.slots > 0)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "destroyed container contains items"));
        for (const auto& [other_id, other] : containers)
        {
            (void)other_id;
            if (other.parent == container_id)
                return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "destroyed container has child containers"));
        }
    }

    std::unordered_map<ItemReservationId, ItemReservation, IdHash> reservations;
    reservations.reserve(s.reservations.size());
    std::unordered_map<ItemInstanceId, Fixed, IdHash> reserved_sums;
    std::uint64_t max_reservation_low = 0;
    for (auto& v : s.reservations)
    {
        if (!v.id.IsValid() || !items.contains(v.item) || reservations.contains(v.id) ||
            v.state != ReservationState::Active || v.quantity <= 0 || v.revision.value > s.revision.value)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.items.restore_invalid", "invalid reservation snapshot"));
        }
        Fixed next = 0;
        if (!CheckedAdd(reserved_sums[v.item], v.quantity, next) || next > items.at(v.item).quantity)
            return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "reservation quantities exceed item quantity"));
        reserved_sums[v.item] = next;
        TrackMaxLowForScope(v.id, s.reservation_ids.scope, max_reservation_low);
        reservations.emplace(v.id, std::move(v));
    }

    const auto item_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.item_ids, item_ids_.Scope(), max_item_low);
    const auto container_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.container_ids, container_ids_.Scope(), max_container_low);
    const auto transfer_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.transfer_ids, transfer_ids_.Scope(), 0);
    const auto reservation_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(s.reservation_ids, reservation_ids_.Scope(), max_reservation_low);
    if (!item_generator_ok || !container_generator_ok || !transfer_generator_ok || !reservation_generator_ok)
    {
        return foundation::Result<void>::Failure(Error("gameplay.items.restore_invalid", "id generator snapshot is invalid"));
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
    RebuildIndexes();
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

ItemsDiagnostics ItemsInventoryService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.items = items_.size();
    d.containers = containers_.size();
    return d;
}
void ItemsInventoryService::RebuildIndexes() noexcept
{
    try
    {
        std::unordered_map<ContainerId, std::vector<ItemInstanceId>, IdHash> new_container_items;
        std::unordered_map<ItemDefinitionId, std::vector<ItemInstanceId>, IdHash> new_definition_items;
        std::unordered_map<ItemInstanceId, Fixed, IdHash> new_reserved_quantities;
        for (const auto& [id, item] : items_)
        {
            new_definition_items[item.definition].push_back(id);
            if (item.location.kind == ItemLocationKind::Container) new_container_items[item.location.container].push_back(id);
        }
        for (auto& [container, ids] : new_container_items)
        {
            (void)container;
            std::sort(ids.begin(), ids.end());
        }
        for (auto& [definition, ids] : new_definition_items)
        {
            (void)definition;
            std::sort(ids.begin(), ids.end());
        }
        for (const auto& [id, reservation] : reservations_)
        {
            (void)id;
            if (reservation.state != ReservationState::Active) continue;
            Fixed next = 0;
            auto& current = new_reserved_quantities[reservation.item];
            current = CheckedAdd(current, reservation.quantity, next) ? next : std::numeric_limits<Fixed>::max();
        }
        container_items_.swap(new_container_items);
        definition_items_.swap(new_definition_items);
        active_reserved_quantities_.swap(new_reserved_quantities);
        indexes_valid_ = true;
    }
    catch (...)
    {
        container_items_.clear();
        definition_items_.clear();
        active_reserved_quantities_.clear();
        indexes_valid_ = false;
    }
}

void ItemsInventoryService::Record(ItemChange c) noexcept
{
    if (next_change_sequence_ == 0)
    {
        const auto next_epoch = CheckedNextChangeEpoch(journal_epoch_);
        if (!next_epoch) return;
        journal_epoch_ = *next_epoch;
        next_change_sequence_ = 1;
        changes_.clear();
    }
    const auto sequence = next_change_sequence_;
    c.sequence = sequence;
    try
    {
        changes_.push_back(std::move(c));
    }
    catch (...)
    {
        changes_.clear();
        const auto next_epoch = CheckedNextChangeEpoch(journal_epoch_);
        if (next_epoch)
        {
            journal_epoch_ = *next_epoch;
            next_change_sequence_ = 1;
        }
        else next_change_sequence_ = 0;
        return;
    }
    next_change_sequence_ = sequence == std::numeric_limits<std::uint64_t>::max() ? 0 : sequence + 1;
    while (changes_.size() > kChangeJournalCapacity) changes_.pop_front();
}
} // namespace epidemic::gameplay::items

