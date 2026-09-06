#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::items
{
using Fixed = std::int64_t;

#define EPIDEMIC_ITEM_TYPE_ID(name)                                                                                    \
    struct name                                                                                                        \
    {                                                                                                                  \
        TypeId value{};                                                                                                \
        static constexpr name FromString(std::string_view s) noexcept                                                  \
        {                                                                                                              \
            return {TypeId::FromString(s)};                                                                            \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
#define EPIDEMIC_ITEM_OBJECT_ID(name)                                                                                  \
    struct name                                                                                                        \
    {                                                                                                                  \
        GameplayObjectId value{};                                                                                      \
        static constexpr name FromRaw(std::uint64_t h, std::uint64_t l) noexcept                                       \
        {                                                                                                              \
            return {GameplayObjectId::FromRaw(h, l)};                                                                  \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
EPIDEMIC_ITEM_TYPE_ID(ItemDefinitionId);
EPIDEMIC_ITEM_TYPE_ID(ItemPropertyTypeId);
EPIDEMIC_ITEM_TYPE_ID(ItemLocationTypeId);
EPIDEMIC_ITEM_TYPE_ID(ContainerPolicyId);
EPIDEMIC_ITEM_OBJECT_ID(ItemInstanceId);
EPIDEMIC_ITEM_OBJECT_ID(ContainerId);
EPIDEMIC_ITEM_OBJECT_ID(ItemTransferId);
EPIDEMIC_ITEM_OBJECT_ID(ItemReservationId);
#undef EPIDEMIC_ITEM_TYPE_ID
#undef EPIDEMIC_ITEM_OBJECT_ID

struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

struct RegisteredPayload
{
    TypeId type{};
    std::uint32_t schema_version = 1;
    std::vector<std::byte> bytes;
    [[nodiscard]] bool operator==(const RegisteredPayload &) const = default;
    template <class T> [[nodiscard]] static RegisteredPayload FromTrivial(TypeId type_id, const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredPayload out;
        out.type = type_id;
        out.schema_version = 1;
        out.bytes.resize(sizeof(T));
        std::memcpy(out.bytes.data(), &value, sizeof(T));
        return out;
    }
    template <class T> [[nodiscard]] std::optional<T> AsTrivial(TypeId expected) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (type != expected || schema_version != 1 || bytes.size() != sizeof(T))
            return std::nullopt;
        T value{};
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }
};

enum class ItemStackPolicy
{
    NonStackable,
    StackByDefinition,
    StackByEquivalentState,
    CustomRegistered
};
enum class ItemDurabilityPolicy
{
    None,
    InstanceValue
};
enum class ItemChargePolicy
{
    None,
    InstanceValue
};
enum class ItemLocationKind
{
    None,
    Container,
    World,
    Equipped,
    Reserved,
    InTransit,
    Destroyed
};
enum class ContainerState
{
    Active,
    Locked,
    Disabled,
    Destroyed
};
enum class ReservationState
{
    Active,
    Consumed,
    Released
};
enum class ItemChangeKind
{
    ItemCreated,
    ItemDestroyed,
    StackSplit,
    StacksMerged,
    TransferCommitted,
    ContainerCreated,
    ContainerStateChanged,
    ContainerRemoved,
    ReservationCreated,
    ReservationReleased,
    ReservationConsumed,
    DurabilityChanged,
    ChargesChanged
};

struct ItemProperty
{
    ItemPropertyTypeId type{};
    RegisteredPayload payload;
    [[nodiscard]] bool operator==(const ItemProperty &) const = default;
};
struct ItemDefinition
{
    ItemDefinitionId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    Fixed base_weight = 0;
    Fixed base_volume = 0;
    ItemStackPolicy stack_policy = ItemStackPolicy::NonStackable;
    ItemDurabilityPolicy durability_policy = ItemDurabilityPolicy::None;
    ItemChargePolicy charge_policy = ItemChargePolicy::None;
    Fixed default_durability = 0;
    Fixed max_durability = 0;
    Fixed default_charges = 0;
    Fixed max_charges = 0;
    RegisteredPayload payload;
    Revision revision{};
};
struct ItemLocation
{
    ItemLocationKind kind = ItemLocationKind::None;
    ContainerId container{};
    GameplayObjectRef world_object{};
    GameplayObjectRef area{};
    RegisteredPayload payload;
    [[nodiscard]] bool operator==(const ItemLocation &) const = default;
};
struct ItemInstance
{
    ItemInstanceId id{};
    ItemDefinitionId definition{};
    Fixed quantity = 1;
    ItemLocation location{};
    Fixed durability = 0;
    Fixed charges = 0;
    std::vector<ItemProperty> properties;
    Revision revision{};
};
struct ItemCreateRequest
{
    ItemInstance item;
    std::optional<Fixed> durability_override{};
    std::optional<Fixed> charges_override{};
};
struct ContainerRecord
{
    ContainerId id{};
    GameplayObjectRef owner_object{};
    Fixed max_weight = 0;
    Fixed max_volume = 0;
    std::uint32_t max_slots = 0;
    std::uint32_t max_nesting_depth = 8;
    ContainerPolicyId policy{};
    ContainerId parent{};
    ContainerState state = ContainerState::Active;
    Revision revision{};
};
struct ContainerUsage
{
    Fixed weight = 0;
    Fixed volume = 0;
    std::uint32_t slots = 0;
};
struct ItemTransferPlan
{
    ItemTransferId id{};
    ItemInstanceId item{};
    ItemLocation source{};
    ItemLocation target{};
    Fixed quantity = 0;
    Revision item_revision{};
    Revision source_revision{};
    Revision target_revision{};
    GameplayContext context{};
};
struct ItemReservation
{
    ItemReservationId id{};
    ItemInstanceId item{};
    Fixed quantity = 0;
    GameplayObjectRef owner{};
    TypeId reason{};
    ReservationState state = ReservationState::Active;
    Revision revision{};
};
struct ItemChange
{
    std::uint64_t sequence = 0;
    ItemChangeKind kind = ItemChangeKind::ItemCreated;
    ItemInstanceId item{};
    ContainerId container{};
    Fixed quantity = 0;
    GameplayContext context{};
    Revision revision{};
    ItemLocation source{};
    ItemLocation target{};
    ItemInstanceId related_item{};
    ItemReservationId reservation{};
};
struct ItemsChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    std::vector<ItemChange> changes;
};

struct ItemsSnapshot
{
    std::vector<ItemInstance> items;
    std::vector<ContainerRecord> containers;
    std::vector<ItemReservation> reservations;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot item_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot container_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot transfer_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot reservation_ids{};
    Revision revision{};
};

struct ItemPropertySchema
{
    ItemPropertyTypeId type{};
    TypeId payload_type{};
    std::uint32_t schema_version = 1;
    std::size_t max_payload_bytes = 0;
};

struct ContainerPolicyContext
{
    ContainerId container{};
    ItemInstanceId moving_item{};
    ItemDefinitionId definition{};
    Fixed quantity = 0;
};

class IContainerPolicy
{
  public:
    virtual ~IContainerPolicy() = default;
    [[nodiscard]] virtual ContainerPolicyId Id() const noexcept = 0;
    [[nodiscard]] virtual bool AllowsInsert(const ContainerPolicyContext& context) const = 0;
};

struct ItemsDiagnostics
{
    std::uint64_t items = 0;
    std::uint64_t containers = 0;
    std::uint64_t active_reservations = 0;
    std::uint64_t transfers = 0;
    std::uint64_t rejected_transfers = 0;
};

class ItemsInventoryService
{
  public:
    ItemsInventoryService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.items_inventory");
    }
    [[nodiscard]] foundation::Result<ItemDefinitionId> RegisterDefinition(ItemDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterPropertySchema(ItemPropertySchema schema);
    [[nodiscard]] foundation::Result<void> RegisterContainerPolicy(const IContainerPolicy& policy);
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    [[nodiscard]] const ItemDefinition *FindDefinition(ItemDefinitionId id) const noexcept;

    [[nodiscard]] foundation::Result<ContainerId> CreateContainer(ContainerRecord container);
    [[nodiscard]] foundation::Result<void> SetContainerState(ContainerId container, ContainerState state,
                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> RemoveContainer(ContainerId container, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ItemInstanceId> CreateItem(ItemInstance item, GameplayContext context = {});
    [[nodiscard]] foundation::Result<ItemInstanceId> CreateItem(ItemCreateRequest request, GameplayContext context = {});
    [[nodiscard]] bool CanCreateItem(ItemDefinitionId definition, Fixed quantity, const ItemLocation& location) const;
    [[nodiscard]] foundation::Result<void> DestroyItem(ItemInstanceId item, GameplayContext context = {});
    [[nodiscard]] const ItemInstance *FindItem(ItemInstanceId id) const noexcept;
    [[nodiscard]] const ContainerRecord *FindContainer(ContainerId id) const noexcept;
    [[nodiscard]] std::optional<ItemInstance> FindItemCopy(ItemInstanceId id) const noexcept;
    [[nodiscard]] std::optional<ContainerRecord> FindContainerCopy(ContainerId id) const noexcept;

    [[nodiscard]] foundation::Result<ItemInstanceId> SplitStack(ItemInstanceId item, Fixed quantity,
                                                                GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> MergeStacks(ItemInstanceId target, ItemInstanceId source,
                                                       GameplayContext context = {});

    [[nodiscard]] foundation::Result<ItemTransferPlan> PrepareTransfer(ItemInstanceId item, ItemLocation target,
                                                                       Fixed quantity, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CommitTransfer(const ItemTransferPlan &plan);
    // Commits a full-instance relocation against an active reservation. This is the
    // owner-side primitive used by cross-owner sagas such as coordinated trade: the
    // reservation remains authoritative until the location mutation succeeds.
    [[nodiscard]] foundation::Result<void> CommitReservedTransfer(ItemReservationId reservation, ItemLocation target,
                                                                  GameplayContext context = {});
    [[nodiscard]] bool CanTransfer(ItemInstanceId item, ItemLocation target, Fixed quantity) const noexcept;

    [[nodiscard]] foundation::Result<ItemReservationId> ReserveItem(ItemInstanceId item, Fixed quantity,
                                                                    GameplayObjectRef owner, TypeId reason = {},
                                                                    GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ReleaseReservation(ItemReservationId reservation,
                                                              GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ConsumeReservation(ItemReservationId reservation,
                                                              GameplayContext context = {});
    // Atomically releases an existing set of reservations and creates one new
    // reservation. No reservation state is changed if any precondition fails.
    // This is the cross-owner primitive used by Equipment for conflict swaps.
    [[nodiscard]] foundation::Result<ItemReservationId> ExchangeReservations(
        std::span<const ItemReservationId> release_reservations,
        ItemInstanceId reserve_item,
        Fixed reserve_quantity,
        GameplayObjectRef reserve_owner,
        TypeId reserve_reason = {},
        GameplayContext context = {});
    [[nodiscard]] const ItemReservation *FindReservation(ItemReservationId reservation) const noexcept;
    [[nodiscard]] std::optional<ItemReservation> FindReservationCopy(ItemReservationId reservation) const noexcept;
    [[nodiscard]] std::vector<ItemReservation> FindReservations(ItemInstanceId item) const;
    [[nodiscard]] Fixed ReservedQuantity(ItemInstanceId item) const noexcept;

    [[nodiscard]] foundation::Result<void> AdjustDurability(ItemInstanceId item, Fixed delta,
                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> AdjustCharges(ItemInstanceId item, Fixed delta,
                                                         GameplayContext context = {});

    [[nodiscard]] ContainerUsage GetContainerUsage(ContainerId container) const noexcept;
    [[nodiscard]] std::vector<ItemInstance> FindItemsInContainer(ContainerId container) const;
    [[nodiscard]] std::vector<ItemInstance> FindItemsByDefinition(ItemDefinitionId definition) const;
    [[nodiscard]] std::vector<ItemChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] ItemsChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    void PruneChangesBefore(std::uint64_t sequence);

    [[nodiscard]] ItemsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ItemsSnapshot snapshot);
    [[nodiscard]] ItemsDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(ItemChange change);
    void RebuildIndexes();
    [[nodiscard]] bool EquivalentForStack(const ItemInstance &a, const ItemInstance &b) const noexcept;
    [[nodiscard]] bool ValidateContainerTarget(ItemInstanceId moving, ContainerId target,
                                               Fixed quantity) const noexcept;
    [[nodiscard]] std::uint32_t ContainerDepth(ContainerId id) const noexcept;
    [[nodiscard]] Revision LocationRevision(const ItemLocation &location) const noexcept;
    [[nodiscard]] bool ValidateLocation(const ItemLocation& location) const noexcept;
    [[nodiscard]] bool IsWorldObjectBound(GameplayObjectRef world_object, ItemInstanceId except = {}) const noexcept;
    [[nodiscard]] bool ValidateProperties(std::vector<ItemProperty>& properties) const;
    [[nodiscard]] bool PolicyAllows(ContainerId container, ItemInstanceId moving, ItemDefinitionId definition, Fixed quantity) const;

    bool frozen_ = false;
    Revision revision_{};
    std::unordered_map<ItemDefinitionId, ItemDefinition, IdHash> definitions_;
    std::unordered_map<ItemPropertyTypeId, ItemPropertySchema, IdHash> property_schemas_;
    std::unordered_map<ContainerPolicyId, const IContainerPolicy*, IdHash> container_policies_;
    std::unordered_map<ItemInstanceId, ItemInstance, IdHash> items_;
    std::unordered_map<ContainerId, ContainerRecord, IdHash> containers_;
    std::unordered_map<ItemReservationId, ItemReservation, IdHash> reservations_;
    std::unordered_map<ContainerId, std::vector<ItemInstanceId>, IdHash> container_items_;
    std::unordered_map<ItemDefinitionId, std::vector<ItemInstanceId>, IdHash> definition_items_;
    std::unordered_map<ItemInstanceId, Fixed, IdHash> active_reserved_quantities_;
    MonotonicIdGenerator<GameplayObjectId> item_ids_{0x3400};
    MonotonicIdGenerator<GameplayObjectId> container_ids_{0x3401};
    MonotonicIdGenerator<GameplayObjectId> transfer_ids_{0x3402};
    MonotonicIdGenerator<GameplayObjectId> reservation_ids_{0x3403};
    std::deque<ItemChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    static constexpr std::size_t kChangeJournalCapacity = 8192;
    ItemsDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::items
