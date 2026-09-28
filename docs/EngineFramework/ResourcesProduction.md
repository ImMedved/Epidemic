# ResourcesProduction Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns resource definitions and recipes, stockpiles, nodes, production sites, reservations, transfers, capabilities/plans, quantity/index state, identity generators, revisions and the resource journal.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 88 public callables, 84 mutation obligations, 1 lifecycle candidates, 21 stale-identity candidates and 3 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Process integration remains behind resource/production ports. Three admission external-boundary candidates were reviewed. Accepted semantic state is not published before local primary/index/journal publication is proven.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkResourcesProductionTests` from `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_tests.cpp`.

Additional registered target: `EpidemicGameFrameworkResourcesProductionSeparationTests` from `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_separation_tests.cpp`.

`G4-RESPROD-001` is closed by staging all relevant ID generators until revision/publication preflight succeeds; `TestRevisionExhaustionKeepsAllGenerators` covers stockpile, node, site, reservation, transaction, capability and plan IDs.

`G4-RESPROD-002` is closed by staged primary/index/journal publication with private fault points; `TestPublicationFaultAtomicity` checks primary insert, reserved-index insert, transaction publication and journal append.

## Goal 4 functional checklist

- [x] Resource definitions/stores/producers.
- [x] Production reservation.
- [x] Consume/produce atomicity.
- [x] Capacity and quantity boundaries.
- [x] Negative/overflow quantities rejected.
- [x] Producer lifecycle.
- [x] Process integration ports remain external.
- [x] Snapshot/restore.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `071c2de1a85622c3` | `FACTORY` | `epidemic::gameplay::resources::ResourceSinkId` | `static constexpr ResourceSinkId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `0ab63decd4f610d9` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ResourceReservationId> Reserve(ResourceStockpileId stockpile,std::vector<ResourceQuantity> quantities,GameplayObjectRef owner={},TypeId reason={},GameplayContext context={});`
- `0bf6beadf65352fb` | `QUERY` | `epidemic::gameplay::resources::ProductionCapabilityId` | `[[nodiscard]] constexpr bool operator==(const ProductionCapabilityId&)const noexcept=default;`
- `0c35fb40775699f5` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> Add(ResourceStockpileId stockpile,ResourceQuantity quantity,GameplayContext context={});`
- `105bd8cbc52d12a6` | `QUERY` | `epidemic::gameplay::resources::ProductionSiteId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `14318725e3bf7422` | `QUERY` | `epidemic::gameplay::resources::ResourceNodeId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceNodeId&)const noexcept=default;`
- `1760a75877f3c420` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `1995247a1118b8e2` | `QUERY` | `epidemic::gameplay::resources::ProductionPlanId` | `[[nodiscard]] constexpr auto operator<=>(const ProductionPlanId&)const noexcept=default;`
- `1a3613c48d37a3e1` | `QUERY` | `epidemic::gameplay::resources::ResourceSinkId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1a7856be287b0ea8` | `FACTORY` | `epidemic::gameplay::resources::ProductionPlanId` | `static constexpr ProductionPlanId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `1c32019357c0e167` | `QUERY` | `epidemic::gameplay::resources::ResourceTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceTypeId&)const noexcept=default;`
- `1cd111107618ff81` | `QUERY` | `epidemic::gameplay::resources::ResourceTransactionId` | `[[nodiscard]] constexpr bool operator==(const ResourceTransactionId&)const noexcept=default;`
- `1d6695491433a4fc` | `QUERY` | `epidemic::gameplay::resources::ResourceNodeId` | `[[nodiscard]] constexpr bool operator==(const ResourceNodeId&)const noexcept=default;`
- `1f7110970f3637d3` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ResourceType*FindResourceType(ResourceTypeId id)const noexcept;`
- `26ea9827e6e74442` | `QUERY` | `epidemic::gameplay::resources::ProductionSiteId` | `[[nodiscard]] constexpr auto operator<=>(const ProductionSiteId&)const noexcept=default;`
- `2c6a824b1888bedc` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] ResourceChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `30d3b85e9a19744b` | `FACTORY` | `epidemic::gameplay::resources::ResourceUnitId` | `static constexpr ResourceUnitId FromString(std::string_view s)noexcept`
- `329e26525c167961` | `QUERY` | `epidemic::gameplay::resources::ResourceTypeId` | `[[nodiscard]] constexpr bool operator==(const ResourceTypeId&)const noexcept=default;`
- `39923fdb80ce900c` | `FACTORY` | `epidemic::gameplay::resources::ResourceStockpileId` | `static constexpr ResourceStockpileId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `3ba9577d746cdeaf` | `QUERY` | `epidemic::gameplay::resources::ResourceStockpileId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `40e9de88a1b143a6` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ProductionPlan*FindProductionPlan(ProductionPlanId id)const noexcept;`
- `416569bbf2922b97` | `QUERY` | `epidemic::gameplay::resources::ProductionRecipeId` | `[[nodiscard]] constexpr bool operator==(const ProductionRecipeId&)const noexcept=default;`
- `432555c623998c7a` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `void PruneChangesBefore(std::uint64_t sequence)noexcept;`
- `46dddb127f560fe7` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ProductionSiteId> CreateProductionSite(ProductionSite site);`
- `497cd0dc720802fa` | `QUERY` | `epidemic::gameplay::resources::ResourceStockpileId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceStockpileId&)const noexcept=default;`
- `4d1b2ae7ef024b76` | `LIFECYCLE` | `epidemic::gameplay::resources::ResourcesProductionService` | `void Freeze()noexcept`
- `4e6f4101f695d5d1` | `FACTORY` | `epidemic::gameplay::resources::ResourceTypeId` | `static constexpr ResourceTypeId FromString(std::string_view s)noexcept`
- `51896dcc8d3ee643` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] Fixed GetAvailableAmount(ResourceStockpileId stockpile,ResourceTypeId type)const noexcept;`
- `554a1e76569f3178` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ProductionRecipe*FindProductionRecipe(ProductionRecipeId id)const noexcept;`
- `55da674e35eaf952` | `QUERY` | `epidemic::gameplay::resources::ResourceReservationId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceReservationId&)const noexcept=default;`
- `57a49e6902741887` | `FACTORY` | `epidemic::gameplay::resources::ResourceSourceId` | `static constexpr ResourceSourceId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `57e04088a5749167` | `FACTORY` | `epidemic::gameplay::resources::ProductionRecipeId` | `static constexpr ProductionRecipeId FromString(std::string_view s)noexcept`
- `5debe8597a8c9bc1` | `QUERY` | `epidemic::gameplay::resources::ResourceUnitId` | `[[nodiscard]] constexpr bool operator==(const ResourceUnitId&)const noexcept=default;`
- `6443f89a3f3fb3dd` | `QUERY` | `epidemic::gameplay::resources::ResourceSinkId` | `[[nodiscard]] constexpr bool operator==(const ResourceSinkId&)const noexcept=default;`
- `678225a2ad29a666` | `FACTORY` | `epidemic::gameplay::resources::ResourceTransactionId` | `static constexpr ResourceTransactionId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `67f02001ec3b68a4` | `QUERY` | `epidemic::gameplay::resources::ResourceReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `68e60b9f07874fa7` | `QUERY` | `epidemic::gameplay::resources::ResourceQuantity` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `69d1ade4a26862fd` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] bool CanReserve(ResourceStockpileId stockpile,std::span<const ResourceQuantity> quantities)const;`
- `6ba490c73555a920` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] Fixed GetAmount(ResourceStockpileId stockpile,ResourceTypeId type)const noexcept;`
- `6c2e3c73b3c7440b` | `FACTORY` | `epidemic::gameplay::resources::ProductionCapabilityId` | `static constexpr ProductionCapabilityId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `6c58767185ae78fc` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ProductionCapabilityId> CreateProductionCapability(ProductionCapability capability);`
- `6e145a647f2e83c1` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] ResourcesSnapshot CaptureSnapshot()const;`
- `6e2ec842c82adf0b` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> Remove(ResourceStockpileId stockpile,ResourceQuantity quantity,GameplayContext context={});`
- `708c088abfd2ac36` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> ConsumeReservation(ResourceReservationId reservation,GameplayContext context={});`
- `7531712b451465c3` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ResourceTypeId> RegisterResourceType(ResourceType type);`
- `781319a51699bbef` | `QUERY` | `epidemic::gameplay::resources::ResourceStockpileId` | `[[nodiscard]] constexpr bool operator==(const ResourceStockpileId&)const noexcept=default;`
- `7938b62236136487` | `CONSTRUCTOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `ResourcesProductionService();`
- `7d3feaf41157a25f` | `QUERY` | `epidemic::gameplay::resources::ResourceSinkId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceSinkId&)const noexcept=default;`
- `7d7060b87fb97d3b` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ProductionPlanId> CreateProductionPlan(ProductionPlan plan);`
- `7f6ba2170f853fef` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ProductionRecipeId> RegisterProductionRecipe(ProductionRecipe recipe);`
- `810ab3e12a85db5c` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> RegenerateNode(ResourceNodeId node,GameplayTimePoint to,GameplayContext context={});`
- `8584b056e795eb9f` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ResourcesSnapshot snapshot);`
- `86a4731fd5ff908e` | `QUERY` | `epidemic::gameplay::resources::ProductionPlanId` | `[[nodiscard]] constexpr bool operator==(const ProductionPlanId&)const noexcept=default;`
- `8cb35063ea546df6` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> RegenerateNode(ResourceNodeId node,GameplayDuration elapsed,GameplayContext context={});`
- `8d7798bea5db284d` | `QUERY` | `epidemic::gameplay::resources::ProductionCapabilityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `90d529ec551884ac` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> ReleaseReservation(ResourceReservationId reservation,GameplayContext context={});`
- `91653db5709074da` | `FACTORY` | `epidemic::gameplay::resources::ResourceNodeId` | `static constexpr ResourceNodeId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `96985442812837a5` | `QUERY` | `epidemic::gameplay::resources::ProductionRecipeId` | `[[nodiscard]] constexpr auto operator<=>(const ProductionRecipeId&)const noexcept=default;`
- `9e609da69de4128e` | `QUERY` | `epidemic::gameplay::resources::ResourceUnitId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceUnitId&)const noexcept=default;`
- `a0278957a3edd7fc` | `QUERY` | `epidemic::gameplay::resources::ResourceNodeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a53f41a094c65183` | `FACTORY` | `epidemic::gameplay::resources::ProductionSiteId` | `static constexpr ProductionSiteId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `a84eb61e7847bc3a` | `QUERY` | `epidemic::gameplay::resources::ResourceReservationId` | `[[nodiscard]] constexpr bool operator==(const ResourceReservationId&)const noexcept=default;`
- `aa5fd628de2db3ac` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ResourceReservation*FindReservation(ResourceReservationId reservation)const noexcept;`
- `ad28588cbc0f0585` | `QUERY` | `epidemic::gameplay::resources::ProductionCapabilityId` | `[[nodiscard]] constexpr auto operator<=>(const ProductionCapabilityId&)const noexcept=default;`
- `af2ca067e1977d8e` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> SetProductionPlanState(ProductionPlanId plan,ProductionPlanState state,GameplayContext context={});`
- `b61fd3966f7108ef` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ResourceTransactionId> Transfer(ResourceStockpileId from,ResourceStockpileId to,std::vector<ResourceQuantity> quantities,TypeId reason={},GameplayContext context={});`
- `bc38e77cb8b0905f` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> DepleteNode(ResourceNodeId node,Fixed amount,GameplayContext context={});`
- `bcf74ac59a27c1cc` | `QUERY` | `epidemic::gameplay::resources::ProductionPlanId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `c1dfbe7ad87f4516` | `QUERY` | `epidemic::gameplay::resources::ProductionRecipeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ccead73d8363bb8a` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<void> SetStockpileState(ResourceStockpileId stockpile,StockpileState state,GameplayContext context={});`
- `cd45e0419f994e7c` | `QUERY` | `epidemic::gameplay::resources::ResourceSourceId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceSourceId&)const noexcept=default;`
- `cf730fc4afcfd12c` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] std::uint64_t OldestChangeSequence()const noexcept;`
- `d146752542ce5a69` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ResourceStockpileId> CreateStockpile(ResourceStockpile stockpile);`
- `d879d349de0944a1` | `QUERY` | `epidemic::gameplay::resources::ResourceSourceId` | `[[nodiscard]] constexpr bool operator==(const ResourceSourceId&)const noexcept=default;`
- `d9f1c57d1b952043` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] std::vector<ProductionPlan> FindProductionPlans(GameplayObjectRef owner={})const;`
- `e19ebb432e48997b` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ProductionCapability*FindProductionCapability(ProductionCapabilityId id)const noexcept;`
- `e1e1b9d3c977651c` | `QUERY` | `epidemic::gameplay::resources::ResourceSourceId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `e30e21796e6fe6fe` | `QUERY` | `epidemic::gameplay::resources::ProductionSiteId` | `[[nodiscard]] constexpr bool operator==(const ProductionSiteId&)const noexcept=default;`
- `e5f93aabdea65208` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] ResourcesDiagnostics GetDiagnostics()const noexcept;`
- `e627ec4e5cb418f7` | `MUTATOR` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] foundation::Result<ResourceNodeId> CreateNode(ResourceNode node);`
- `e922970b5d37129b` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] const ResourceStockpile*FindStockpile(ResourceStockpileId id)const noexcept;`
- `eb03c94d8156dbdd` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `f02e58a553e8d56a` | `QUERY` | `epidemic::gameplay::resources::ResourceTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f4291119b3e76df4` | `QUERY` | `epidemic::gameplay::resources::ResourceUnitId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f7ea53bf603dcef6` | `QUERY` | `epidemic::gameplay::resources::ResourceTransactionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `fc42082a4ffa174e` | `QUERY` | `epidemic::gameplay::resources::ResourcesProductionService` | `[[nodiscard]] Fixed GetReservedAmount(ResourceStockpileId stockpile,ResourceTypeId type)const noexcept;`
- `fd0b70119f50c86d` | `FACTORY` | `epidemic::gameplay::resources::ResourceReservationId` | `static constexpr ResourceReservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `fee62bca6363f472` | `QUERY` | `epidemic::gameplay::resources::ResourceTransactionId` | `[[nodiscard]] constexpr auto operator<=>(const ResourceTransactionId&)const noexcept=default;`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
