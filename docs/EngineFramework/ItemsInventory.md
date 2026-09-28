# ItemsInventory Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns item definitions, item instances, containers, reservations, transfer preparation state, bindings, identity generators, revisions and the item change journal.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 82 public callables, 92 mutation obligations, 1 lifecycle candidates, 23 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Transfer and reservation protocols are represented by owned durable records. No external-boundary candidates were emitted by the admission scanner.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkItemsInventoryTests` from `EngineFramework/DevelopmentInfrastructure/Tests/items_inventory_tests.cpp`.


## Goal 4 functional checklist

- [x] Item definition and container registration.
- [x] Item create/remove.
- [x] Stack split/merge rules.
- [x] Capacity and slot limits.
- [x] Transfer within/between containers.
- [x] Reservation create/consume/release.
- [x] Prepare/commit/cancel transfer.
- [x] Exchange reservations.
- [x] Durability and charges boundaries.
- [x] World/container bindings.
- [x] Secondary container/item indexes.
- [x] Failed transfer leaves source and target unchanged.
- [x] Snapshot/restore items, containers, reservations, bindings, generators, journal.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `014823d63ab843de` | `QUERY` | `epidemic::gameplay::items::ItemPropertyTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `019e2b8ca4d679ec` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> ConsumeReservation(ItemReservationId reservation,GameplayContext context={});`
- `059b24066c432c4a` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] ItemsSnapshot CaptureSnapshot()const;`
- `0be786ca02f442c1` | `QUERY` | `epidemic::gameplay::items::ItemLocationTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ItemLocationTypeId&)const noexcept=default;`
- `0c4bf264e85c14d6` | `QUERY` | `epidemic::gameplay::items::ItemDefinitionId` | `[[nodiscard]] constexpr bool operator==(const ItemDefinitionId&)const noexcept=default;`
- `0e96045e869f59f1` | `QUERY` | `epidemic::gameplay::items::ContainerPolicyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `19e4383811bcc8be` | `QUERY` | `epidemic::gameplay::items::ItemPropertyTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ItemPropertyTypeId&)const noexcept=default;`
- `1befc786bbc70cd0` | `QUERY` | `epidemic::gameplay::items::ItemInstanceId` | `[[nodiscard]] constexpr bool operator==(const ItemInstanceId&)const noexcept=default;`
- `1e197c2026b002ec` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] ItemsChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `1e5246423899fa21` | `QUERY` | `epidemic::gameplay::items::ItemReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `2102690871d234b6` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> RemoveContainer(ContainerId container,GameplayContext context={});`
- `23a718e3196963b4` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> CommitReservedTransfer(ItemReservationId reservation,ItemLocation target,GameplayContext context={});`
- `2a78a2d737099ec8` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemInstanceId> CreateItem(ItemInstance item,GameplayContext context={});`
- `2ec09b727114a0da` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] const ItemReservation*FindReservation(ItemReservationId reservation)const noexcept;`
- `3168f25d78380362` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::vector<ItemReservation> FindReservations(ItemInstanceId item)const;`
- `317f0fc739ac989e` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::optional<ItemReservation> FindReservationCopy(ItemReservationId reservation)const noexcept;`
- `31d3bb25c93d4d6f` | `FACTORY` | `epidemic::gameplay::items::ItemInstanceId` | `static constexpr ItemInstanceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `321d30ae1e038b59` | `QUERY` | `epidemic::gameplay::items::ItemInstanceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3491cd9204bafc16` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> CommitTransfer(const ItemTransferPlan&plan);`
- `38a21114eafdb0aa` | `QUERY` | `epidemic::gameplay::items::ContainerId` | `[[nodiscard]] constexpr bool operator==(const ContainerId&)const noexcept=default;`
- `38fa244c4173b90d` | `QUERY` | `epidemic::gameplay::items::ItemDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `390dccb61379c0bf` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ContainerId> CreateContainer(ContainerRecord container);`
- `39d94324ad295656` | `QUERY` | `epidemic::gameplay::items::ItemPropertyTypeId` | `[[nodiscard]] constexpr bool operator==(const ItemPropertyTypeId&)const noexcept=default;`
- `3e61a93707736f90` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] Fixed ReservedQuantity(ItemInstanceId item)const noexcept;`
- `42c7995a96a97882` | `QUERY` | `epidemic::gameplay::items::ContainerPolicyId` | `[[nodiscard]] constexpr auto operator<=>(const ContainerPolicyId&)const noexcept=default;`
- `431ece32455ce665` | `FACTORY` | `epidemic::gameplay::items::ContainerId` | `static constexpr ContainerId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `43c0988fa2f06377` | `QUERY` | `epidemic::gameplay::items::RegisteredPayload` | `[[nodiscard]] bool operator==(const RegisteredPayload&)const=default;`
- `44d30943a04b8216` | `LIFECYCLE` | `epidemic::gameplay::items::ItemsInventoryService` | `void Freeze()noexcept`
- `44d97fcd1ba96b2f` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::optional<ItemInstance> FindItemCopy(ItemInstanceId id)const noexcept;`
- `4852926b73564753` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemInstanceId> SplitStack(ItemInstanceId item,Fixed quantity,GameplayContext context={});`
- `4b13e192736d89b9` | `QUERY` | `epidemic::gameplay::items::ItemReservationId` | `[[nodiscard]] constexpr auto operator<=>(const ItemReservationId&)const noexcept=default;`
- `4c5c79de2bea3f7b` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::vector<ItemInstance> FindItemsByDefinition(ItemDefinitionId definition)const;`
- `505399fde353b216` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> DestroyItem(ItemInstanceId item,GameplayContext context={});`
- `539b3ad2d7cf79ae` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemTransferPlan> PrepareTransfer(ItemInstanceId item,ItemLocation target,Fixed quantity,GameplayContext context={});`
- `55ae86deb581786a` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::vector<ItemInstance> FindItemsInContainer(ContainerId container)const;`
- `57dd6c28b29566d2` | `QUERY` | `epidemic::gameplay::items::ItemReservationId` | `[[nodiscard]] constexpr bool operator==(const ItemReservationId&)const noexcept=default;`
- `5e9f3b273c6b1c12` | `CONSTRUCTOR` | `epidemic::gameplay::items::ItemsInventoryService` | `ItemsInventoryService();`
- `62c3557973f24f07` | `QUERY` | `epidemic::gameplay::items::ContainerPolicyId` | `[[nodiscard]] constexpr bool operator==(const ContainerPolicyId&)const noexcept=default;`
- `654e2bd6abb1686c` | `QUERY` | `epidemic::gameplay::items::ItemTransferId` | `[[nodiscard]] constexpr bool operator==(const ItemTransferId&)const noexcept=default;`
- `69ca41e39526418b` | `FACTORY` | `epidemic::gameplay::items::ItemPropertyTypeId` | `static constexpr ItemPropertyTypeId FromString(std::string_view s)noexcept`
- `6a31440b244bc210` | `QUERY` | `epidemic::gameplay::items::ContainerId` | `[[nodiscard]] constexpr auto operator<=>(const ContainerId&)const noexcept=default;`
- `6aa9ca25eb024b29` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemInstanceId> CreateItem(ItemCreateRequest request,GameplayContext context={});`
- `6b12303ab52e860d` | `FACTORY` | `epidemic::gameplay::items::ItemDefinitionId` | `static constexpr ItemDefinitionId FromString(std::string_view s)noexcept`
- `6c78d70cfb3df325` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> SetContainerState(ContainerId container,ContainerState state,GameplayContext context={});`
- `6d710752772b3542` | `QUERY` | `epidemic::gameplay::items::ItemTransferId` | `[[nodiscard]] constexpr auto operator<=>(const ItemTransferId&)const noexcept=default;`
- `794588876db96ffb` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] ItemsDiagnostics GetDiagnostics()const noexcept;`
- `7973c69b3c47fed5` | `QUERY` | `epidemic::gameplay::items::ItemProperty` | `[[nodiscard]] bool operator==(const ItemProperty&)const=default;`
- `79d0d08fea825c72` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> RegisterContainerPolicy(const IContainerPolicy&policy);`
- `80143400dca3a62d` | `QUERY` | `epidemic::gameplay::items::ItemInstanceId` | `[[nodiscard]] constexpr auto operator<=>(const ItemInstanceId&)const noexcept=default;`
- `894baabb9c375e8a` | `QUERY` | `epidemic::gameplay::items::ItemLocationTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `8a1e41a78105d7f1` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `void PruneChangesBefore(std::uint64_t sequence);`
- `8b0ef0984f011348` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] const ContainerRecord*FindContainer(ContainerId id)const noexcept;`
- `967836e7b06b1029` | `FACTORY` | `epidemic::gameplay::items::ItemReservationId` | `static constexpr ItemReservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `96e93756b60580af` | `FACTORY` | `epidemic::gameplay::items::ContainerPolicyId` | `static constexpr ContainerPolicyId FromString(std::string_view s)noexcept`
- `9a46a69cce10f0f8` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] bool CanCreateItem(ItemDefinitionId definition,Fixed quantity,const ItemLocation&location)const;`
- `9e14ae2290e948cb` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] const ItemDefinition*FindDefinition(ItemDefinitionId id)const noexcept;`
- `9e263d4f96b02a4c` | `QUERY` | `epidemic::gameplay::items::ItemLocation` | `[[nodiscard]] bool operator==(const ItemLocation&)const=default;`
- `a93e0e0c5ed49e50` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> AdjustCharges(ItemInstanceId item,Fixed delta,GameplayContext context={});`
- `ab2408ead002e87c` | `QUERY` | `epidemic::gameplay::items::IContainerPolicy` | `[[nodiscard]] virtual ContainerPolicyId Id()const noexcept=0;`
- `ac5b326f75e04aa1` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemReservationId> ReserveItem(ItemInstanceId item,Fixed quantity,GameplayObjectRef owner,TypeId reason={},GameplayContext context={});`
- `b0834b38b51fbfe3` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `b09aa626631f9701` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ItemsSnapshot snapshot);`
- `c27c2e421b17f226` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] bool CanTransfer(ItemInstanceId item,ItemLocation target,Fixed quantity)const noexcept;`
- `c53eb3fe351c3dc5` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemReservationId> ExchangeReservations(std::span<const ItemReservationId> release_reservations,ItemInstanceId reserve_item,Fixed reserve_quantity,GameplayObjectRef reserve_owner,TypeId reserve_reason={},GameplayContext context={});`
- `c5c23df44ec7641d` | `DESTRUCTOR` | `epidemic::gameplay::items::IContainerPolicy` | `virtual ~IContainerPolicy()=default;`
- `c7f26abe422988d1` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> AdjustDurability(ItemInstanceId item,Fixed delta,GameplayContext context={});`
- `ca05182d1d794850` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> MergeStacks(ItemInstanceId target,ItemInstanceId source,GameplayContext context={});`
- `d0f75a1579baf09c` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> ReleaseReservation(ItemReservationId reservation,GameplayContext context={});`
- `d154279b284e7028` | `QUERY` | `epidemic::gameplay::items::IContainerPolicy` | `[[nodiscard]] virtual bool AllowsInsert(const ContainerPolicyContext&context)const=0;`
- `d206b0eace20c532` | `FACTORY` | `epidemic::gameplay::items::ItemLocationTypeId` | `static constexpr ItemLocationTypeId FromString(std::string_view s)noexcept`
- `d2a26dd80664d33d` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] ContainerUsage GetContainerUsage(ContainerId container)const noexcept;`
- `d315beaae62c63e7` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] const ItemInstance*FindItem(ItemInstanceId id)const noexcept;`
- `d4f6040459c98026` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<ItemDefinitionId> RegisterDefinition(ItemDefinition definition);`
- `d5b8fc65b0ac15df` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `d6618cdebc15c2cb` | `QUERY` | `epidemic::gameplay::items::ContainerId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `da4550ba728e99c8` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `ddfc1318eac87638` | `QUERY` | `epidemic::gameplay::items::ItemTransferId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ec4bf828c7b2eb9b` | `QUERY` | `epidemic::gameplay::items::ItemLocationTypeId` | `[[nodiscard]] constexpr bool operator==(const ItemLocationTypeId&)const noexcept=default;`
- `f32b0e1a71e2770f` | `MUTATOR` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] foundation::Result<void> RegisterPropertySchema(ItemPropertySchema schema);`
- `f55574bf42f0fdee` | `QUERY` | `epidemic::gameplay::items::ItemDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const ItemDefinitionId&)const noexcept=default;`
- `f5f2e0ad6097d473` | `QUERY` | `epidemic::gameplay::items::ItemsInventoryService` | `[[nodiscard]] std::optional<ContainerRecord> FindContainerCopy(ContainerId id)const noexcept;`
- `f99e69036af5bd50` | `FACTORY` | `epidemic::gameplay::items::ItemTransferId` | `static constexpr ItemTransferId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
