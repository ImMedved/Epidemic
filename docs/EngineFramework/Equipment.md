# Equipment Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns equipment profiles, slot definitions, entity loadouts, item bindings, equipment revision/journal state and reconciliation metadata.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 72 public callables, 72 mutation obligations, 1 lifecycle candidates, 18 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Item-provider reservation/reconciliation is kept behind the Equipment provider contract. Admission emitted no separate external-boundary candidate rows.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkEquipmentTests` from `EngineFramework/DevelopmentInfrastructure/Tests/equipment_tests.cpp`.


## Goal 4 functional checklist

- [x] Equipment slot definition.
- [x] Equip/unequip.
- [x] Prepare equip.
- [x] Item provider reservation.
- [x] Conflicting slots.
- [x] Requirements and compatibility.
- [x] Reconcile restored bindings.
- [x] Exchange/reconcile reservations.
- [x] Provider failure before and after prepare.
- [x] Snapshot/restore equipment and external binding metadata.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `006fe4277fc3d1eb` | `CONSTRUCTOR` | `epidemic::gameplay::equipment::EquipmentService` | `explicit EquipmentService(std::size_t change_capacity=2048);`
- `0133349dbbbf0a53` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] std::vector<EquipmentBinding> FindBindings(GameplayObjectRef subject)const;`
- `01a31d6b04c5b1dd` | `QUERY` | `epidemic::gameplay::equipment::EquipmentBindingId` | `[[nodiscard]] constexpr bool operator==(const EquipmentBindingId&)const noexcept=default;`
- `058340ef0059be5e` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentProfileId> CreateProfile(EquipmentProfile profile);`
- `08adef5b3763c9f7` | `QUERY` | `epidemic::gameplay::equipment::EquipmentGrantTypeId` | `[[nodiscard]] constexpr bool operator==(const EquipmentGrantTypeId&)const noexcept=default;`
- `0ecbf77e47ef71f8` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<void> ReconcileRestoredBindings(GameplayContext context={});`
- `1152bcd7924cf0d2` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentSlotTypeId` | `static constexpr EquipmentSlotTypeId FromString(std::string_view s)noexcept`
- `125ea5b63b73e87e` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileDefinitionId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentProfileDefinitionId&)const noexcept=default;`
- `152cd3f7ed0ec5fe` | `MUTATOR` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `[[nodiscard]] virtual foundation::Result<void> ReleaseFromEquipment(EquipmentItemId item,GameplayObjectRef subject,GameplayContext context)=0;`
- `1b3db19645198d41` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileId` | `[[nodiscard]] constexpr bool operator==(const EquipmentProfileId&)const noexcept=default;`
- `1fc290a9409576b0` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] EquipmentChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `249e7be5412c6bcc` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentProfileDefinitionId> RegisterProfileDefinition(EquipmentProfileDefinition definition);`
- `254a75c7db1d3b45` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `void SetItemProvider(IEquipmentItemProvider*provider)noexcept`
- `2bbb86c6edc8795d` | `QUERY` | `epidemic::gameplay::equipment::EquipmentLoadoutId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3030f5de7095bc0a` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentItemId` | `static constexpr EquipmentItemId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `324700df704181ee` | `QUERY` | `epidemic::gameplay::equipment::EquipmentLoadoutId` | `[[nodiscard]] constexpr bool operator==(const EquipmentLoadoutId&)const noexcept=default;`
- `3303ea924cf5322c` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentProfileDefinitionId` | `static constexpr EquipmentProfileDefinitionId FromString(std::string_view s)noexcept`
- `3ba5179c48f17cef` | `QUERY` | `epidemic::gameplay::equipment::EquipmentGrantTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3cc156c8ad5bff64` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `3d4dd385a443699c` | `FACTORY` | `epidemic::gameplay::equipment::EquipOperationId` | `static constexpr EquipOperationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4455d8b3f1fb252d` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<void> SetBindingState(EquipmentBindingId binding,EquipmentBindingState state,GameplayContext context={});`
- `45ff879438b270da` | `QUERY` | `epidemic::gameplay::equipment::EquipmentItemId` | `[[nodiscard]] constexpr bool operator==(const EquipmentItemId&)const noexcept=default;`
- `47082c7e500927f9` | `QUERY` | `epidemic::gameplay::equipment::EquipOperationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `4aedbd7edea400ed` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] const EquipmentProfile*FindProfile(GameplayObjectRef subject)const noexcept;`
- `4b48609fe6f4d14f` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] EquipmentSnapshot CaptureSnapshot()const;`
- `504c8a3311375c2a` | `QUERY` | `epidemic::gameplay::equipment::EquipOperationId` | `[[nodiscard]] constexpr bool operator==(const EquipOperationId&)const noexcept=default;`
- `516e57c56a11fea3` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentGrantTypeId` | `static constexpr EquipmentGrantTypeId FromString(std::string_view s)noexcept`
- `530e49d51e6b3a18` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotTypeId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentSlotTypeId&)const noexcept=default;`
- `5549895979b3a34b` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `558140a247284d1d` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] const EquipmentBinding*FindBinding(EquipmentBindingId id)const noexcept;`
- `5a2e869e0539d502` | `QUERY` | `epidemic::gameplay::equipment::EquipmentBindingId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentBindingId&)const noexcept=default;`
- `5cf187a54088e1a8` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `5d9d46d053e48d38` | `MUTATOR` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `[[nodiscard]] virtual foundation::Result<void> ReconcileEquipmentReservation(EquipmentItemId,GameplayObjectRef,EquipmentBindingId,GameplayContext)`
- `5eee75f5f9fd3fa5` | `QUERY` | `epidemic::gameplay::equipment::EquipmentBindingId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `60e0932f8e2a8b2d` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] std::optional<EquipmentItemId> GetEquippedItem(GameplayObjectRef subject,EquipmentSlotId slot)const;`
- `639192b10b093c33` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<void> Unequip(EquipmentBindingId binding,GameplayContext context={});`
- `644cc72bcf2eb377` | `MUTATOR` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `[[nodiscard]] virtual foundation::Result<void> ExchangeEquipmentReservations(std::span<const EquipmentItemId> release_items,std::optional<EquipmentItemId> reserve_item,GameplayObjectRef subject,GameplayContext context)`
- `675ecf40570a7ed4` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileDefinitionId` | `[[nodiscard]] constexpr bool operator==(const EquipmentProfileDefinitionId&)const noexcept=default;`
- `691418ecda33a2a5` | `QUERY` | `epidemic::gameplay::equipment::EquipmentItemId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentItemId&)const noexcept=default;`
- `6cf4f793b20c7e16` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentBindingId` | `static constexpr EquipmentBindingId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `726d87ca887a1e8f` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `729c71eb3db33826` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] bool CanEquip(GameplayObjectRef subject,EquipmentItemId item,const std::vector<EquipmentSlotId>&slots)const;`
- `75ad14ef81dd4aa8` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] bool NeedsReconciliation()const noexcept`
- `7880166f21563635` | `QUERY` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `[[nodiscard]] virtual std::optional<EquipmentItemDescriptor> Describe(EquipmentItemId item)const=0;`
- `824bea1dcba599b3` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentSlotId> AddSlot(EquipmentProfileId profile,EquipmentSlotDefinition slot);`
- `831575099ed45d27` | `DESTRUCTOR` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `virtual ~IEquipmentItemProvider()=default;`
- `900bb838b5407dbb` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] std::optional<EquipmentBinding> FindBindingCopy(EquipmentBindingId id)const noexcept;`
- `9507226f21e6dcc0` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentLoadoutId> SaveLoadout(EquipmentLoadout loadout);`
- `96625ab526b9cb96` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] EquipmentReconciliationState ReconciliationState()const noexcept`
- `97c1c7031b758dd1` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentProfileId> CreateProfileFromDefinition(GameplayObjectRef subject,EquipmentProfileDefinitionId definition);`
- `988d0f715a040db9` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentProfileId` | `static constexpr EquipmentProfileId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `994fecab4775a49c` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipPlan> PrepareEquip(GameplayObjectRef subject,EquipmentItemId item,std::vector<EquipmentSlotId> slots,GameplayContext context={});`
- `9bd668af87319d59` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentSlotId&)const noexcept=default;`
- `9c371341302bb0e3` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] std::optional<EquipmentProfile> FindProfileCopy(GameplayObjectRef subject)const noexcept;`
- `9e29b8c78837d302` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a1aa79924fb50509` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EquipmentSnapshot snapshot);`
- `a1eed81127b3eb01` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentLoadoutId` | `static constexpr EquipmentLoadoutId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `b2f149e2f0516c02` | `QUERY` | `epidemic::gameplay::equipment::EquipmentItemId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `b32eb92ca48b0ba2` | `QUERY` | `epidemic::gameplay::equipment::EquipmentGrantTypeId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentGrantTypeId&)const noexcept=default;`
- `b45d3862551ee63a` | `QUERY` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] EquipmentDiagnostics GetDiagnostics()const noexcept;`
- `bd287d39a12d280e` | `QUERY` | `epidemic::gameplay::equipment::EquipmentLoadoutId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentLoadoutId&)const noexcept=default;`
- `c0a110128bef03d8` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<EquipmentBindingId> CommitEquip(const EquipPlan&plan,std::vector<EquipmentGrantDescriptor> grants={});`
- `c46a5e1ea58c666c` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotId` | `[[nodiscard]] constexpr bool operator==(const EquipmentSlotId&)const noexcept=default;`
- `c7d2cdd46da2841c` | `MUTATOR` | `epidemic::gameplay::equipment::EquipmentService` | `[[nodiscard]] foundation::Result<void> RegisterGrantSchema(EquipmentGrantSchema schema);`
- `cd3b89832299c171` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileId` | `[[nodiscard]] constexpr auto operator<=>(const EquipmentProfileId&)const noexcept=default;`
- `dc686d250636deee` | `QUERY` | `epidemic::gameplay::equipment::EquipOperationId` | `[[nodiscard]] constexpr auto operator<=>(const EquipOperationId&)const noexcept=default;`
- `e064e340e277257f` | `QUERY` | `epidemic::gameplay::equipment::EquipmentProfileDefinitionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e071108d22449e01` | `FACTORY` | `epidemic::gameplay::equipment::EquipmentSlotId` | `static constexpr EquipmentSlotId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `e6f69d9508ca23d6` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e725afcdc21854d9` | `MUTATOR` | `epidemic::gameplay::equipment::IEquipmentItemProvider` | `[[nodiscard]] virtual foundation::Result<void> ReserveForEquipment(EquipmentItemId item,GameplayObjectRef subject,GameplayContext context)=0;`
- `ea19da5302245c37` | `QUERY` | `epidemic::gameplay::equipment::EquipmentSlotTypeId` | `[[nodiscard]] constexpr bool operator==(const EquipmentSlotTypeId&)const noexcept=default;`
- `ec660a87d61ed3b8` | `LIFECYCLE` | `epidemic::gameplay::equipment::EquipmentService` | `void Freeze()noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
