# Construction Goal 4 local audit

Status: B06 worker review complete. This document is block-local evidence only and does not claim whole-engine freeze.

## Responsibility

Owns construction definitions, placement validation/plans, cost/socket reservations, sites, output envelopes, socket occupancy and construction change journal.

## Public headers and types

Public surface is rooted at `EngineFramework/GameplayWorldStateOwners/Construction/include/Epidemic/GameFramework/Construction/construction.h`. No B06 public-surface change was required.

## Dependency list

Direct production links are GameFramework Foundation and EngineBase Foundation. No Traversal, Navigation or World peer dependency is introduced.

## External ports/callbacks/providers/backends

Placement/cost/output ports are borrowed. The service owns semantic reservation and output-envelope state, not external world/runtime objects.

## Authoritative state

Rules/recipes, plans, sites, sockets/reservations, execution/output records, ID generators, global revision and bounded journal.

## Derived/cache/index state

Validation summaries and diagnostics derive from authoritative placement/site/socket state.

## ID spaces, generations, revisions and cursors

Plan/site/execution/output/reservation identities are service-controlled. Revision preflight is used before state-changing publication and journal cursors are bounded.

## State machines

Placement plan and construction site lifecycles include validate/reserve/commit/cancel/expire/start/pause/resume/progress/complete/fail/destroy/compact paths. Output envelopes have queued/acknowledged/dead-letter states.

## Persistent and transient state

Snapshot state covers persistent semantic construction records and journal/generator continuity according to the public snapshot contract. Borrowed ports remain transient.

## Snapshot/restore contract

Restore validates references, lifecycle states, generator/revision/journal continuity and rebuilds local indices before publication.

## Threading contract

Owner-thread/external-serialization gameplay state.

## Public mutation API

Placement, reservation, site and output mutations use checked revision preflight and staged commit patterns. B06 audit found no additional production defect requiring code change in this module.

## Read/query API for invariants

Plan/site/socket/output lookup and diagnostics witness lifecycle, reservation, occupancy and delivery-state consistency.

## Local invariants

Socket reservation/occupancy is unique, terminal plans/sites are not reused, failed placement does not partially publish outputs, and output acknowledgement/dead-letter operations preserve envelope identity.

## Hard limits, budgets and complexity bounds

Revision/generator/journal boundaries are controlled and output retention has explicit lifecycle cleanup.

## Test evidence

`EngineFramework/DevelopmentInfrastructure/Tests/construction_tests.cpp` exercises placement validation, reservation/commit/cancel, site lifecycle, socket semantics, outputs, snapshot/restore and revision boundaries. Target: `EpidemicGameFrameworkConstructionTests`. No production change was needed after the B06 review.

## Exact public API anchors

Each row is the exact reviewed contract for one B06 callable. The matching assertion is recorded in `_goal4_handoff/B06/public_api_anchors.json`.

- `012b9f789b28565f` | `MUTATOR` | `epidemic::gameplay::construction::IConstructionCostProvider` | `[[nodiscard]] virtual foundation::Result<ConstructionCostReservation> Reserve(GameplayObjectRef actor,const ConstructionCost&cost,const GameplayContext&context)=0;`
- `0199821b998d6789` | `QUERY` | `epidemic::gameplay::construction::PlacementReasonId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `036c3e140b871faf` | `QUERY` | `epidemic::gameplay::construction::ConstructionCostTypeId` | `[[nodiscard]] constexpr bool operator==(const ConstructionCostTypeId&)const noexcept=default;`
- `03cf583c6a2a374d` | `QUERY` | `epidemic::gameplay::construction::PlacementSocketId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `05f66bed52e96887` | `LIFECYCLE` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> ResumeConstructionSite(ConstructionSiteId id,GameplayContext context={});`
- `07866b977147c80c` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `0aaca75152e02055` | `QUERY` | `epidemic::gameplay::construction::PlacementSocketId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementSocketId&)const noexcept=default;`
- `0cb2d18f32d92d58` | `FACTORY` | `epidemic::gameplay::construction::ConstructionRecipeId` | `static constexpr ConstructionRecipeId FromString(std::string_view s)noexcept`
- `0dd63c57a6709101` | `QUERY` | `epidemic::gameplay::construction::WorldVolumeRef` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `0e716f69c8d0ed6f` | `QUERY` | `epidemic::gameplay::construction::WorldPosition` | `[[nodiscard]] constexpr bool operator==(const WorldPosition&)const noexcept=default;`
- `12078dc01a2f8a97` | `QUERY` | `epidemic::gameplay::construction::PlacementPlanId` | `[[nodiscard]] constexpr bool operator==(const PlacementPlanId&)const noexcept=default;`
- `13cf7d184e2b60f7` | `QUERY` | `epidemic::gameplay::construction::IConstructionPlacementProvider` | `[[nodiscard]] virtual foundation::Result<PlacementSemanticProjection> Project(const PlacementRequest&request)const=0;`
- `152d3978ed70de88` | `QUERY` | `epidemic::gameplay::construction::RefHash` | `[[nodiscard]] std::size_t operator()(GameplayObjectRef ref)const noexcept`
- `15956e008747ba90` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `15c120a8cd366583` | `QUERY` | `epidemic::gameplay::construction::PlacementRuleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `17b415c25e6e605a` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `void SetPlacementProvider(const IConstructionPlacementProvider*provider)noexcept;`
- `18765a00deaec5ba` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] ConstructionDiagnostics GetDiagnostics()const noexcept;`
- `1ba1f376134ab5d4` | `QUERY` | `epidemic::gameplay::construction::ConstructionSiteId` | `[[nodiscard]] constexpr auto operator<=>(const ConstructionSiteId&)const noexcept=default;`
- `1e6ba86a6654361d` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> DestroyConstructionSite(ConstructionSiteId id,GameplayContext context={});`
- `1f0447bd58c11bfa` | `QUERY` | `epidemic::gameplay::construction::PlacementRuleId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementRuleId&)const noexcept=default;`
- `21f3f51c3378d110` | `FACTORY` | `epidemic::gameplay::construction::ConstructionSiteId` | `static constexpr ConstructionSiteId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `221af0de5a3fa6eb` | `QUERY` | `epidemic::gameplay::construction::PlacementReasonId` | `[[nodiscard]] constexpr bool operator==(const PlacementReasonId&)const noexcept=default;`
- `2d2bb9d69a82aabd` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> RegisterRecipe(ConstructionRecipe recipe);`
- `32d07695571631b5` | `QUERY` | `epidemic::gameplay::construction::PlacementPlanId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementPlanId&)const noexcept=default;`
- `338a112f4b85ba94` | `FACTORY` | `epidemic::gameplay::construction::PlacedObjectId` | `static constexpr PlacedObjectId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `35a7c6e276c99806` | `QUERY` | `epidemic::gameplay::construction::ConstructionRecipeId` | `[[nodiscard]] constexpr bool operator==(const ConstructionRecipeId&)const noexcept=default;`
- `37c71cd007cd9c8c` | `QUERY` | `epidemic::gameplay::construction::ConstructionCostTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ConstructionCostTypeId&)const noexcept=default;`
- `3ed15e5f777c99ae` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] const PlacementPlan*FindPlan(PlacementPlanId id)const noexcept;`
- `40125208d80e781e` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputTypeId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementOutputTypeId&)const noexcept=default;`
- `43680d2e022c0889` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] const PlacementSocket*FindSocket(PlacementSocketId id)const noexcept;`
- `45458c1d3185c63a` | `FACTORY` | `epidemic::gameplay::construction::PlacementPlanId` | `static constexpr PlacementPlanId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `454f15e2c8503639` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] std::vector<PlacementOutputEnvelope> PendingOutputs()const;`
- `476fd4ff18047425` | `LIFECYCLE` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<ConstructionSiteId> StartConstructionSite(PlacementPlanId plan,GameplayTimePoint started_at={},GameplayContext context={});`
- `4e3ce960145c9f58` | `QUERY` | `epidemic::gameplay::construction::PlacedObjectId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `50d8c7c2496100e3` | `LIFECYCLE` | `epidemic::gameplay::construction::ConstructionService` | `void Freeze()noexcept`
- `51965250c877babc` | `QUERY` | `epidemic::gameplay::construction::ConstructionCostTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `52c245d7e12a047a` | `QUERY` | `epidemic::gameplay::construction::PlacementSocketId` | `[[nodiscard]] constexpr bool operator==(const PlacementSocketId&)const noexcept=default;`
- `5305f56f29bbd961` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> CancelConstructionSite(ConstructionSiteId id,GameplayContext context={});`
- `548272dd7d9f6401` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ConstructionSnapshot snapshot);`
- `55ab2cb5daebe46a` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> DeadLetterOutput(PlacementOutputId id,PlacementReasonId reason,GameplayContext context={});`
- `59392943bb3e13fc` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `void SetCostProvider(IConstructionCostProvider*provider)noexcept;`
- `619a5d7cf5910cb6` | `QUERY` | `epidemic::gameplay::construction::ConstructionSiteId` | `[[nodiscard]] constexpr bool operator==(const ConstructionSiteId&)const noexcept=default;`
- `63534b1ca579a7ea` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<ConstructionSocketReservationId> ReserveSocket(PlacementSocketId id,GameplayObjectRef owner,GameplayContext context={});`
- `64d7ddc7f246c735` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> RegisterPlacementDefinition(PlacementDefinition definition);`
- `65795543ba36dbfa` | `QUERY` | `epidemic::gameplay::construction::PlacementRuleId` | `[[nodiscard]] constexpr bool operator==(const PlacementRuleId&)const noexcept=default;`
- `665f1b1273b91fa2` | `QUERY` | `epidemic::gameplay::construction::PlacementReasonId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementReasonId&)const noexcept=default;`
- `6680cce299520094` | `CONSTRUCTOR` | `epidemic::gameplay::construction::ConstructionService` | `ConstructionService();`
- `686ba9e0975226d2` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] const ConstructionSite*FindSite(ConstructionSiteId id)const noexcept;`
- `69386f27472a4ba9` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> AdvanceConstructionProgress(ConstructionSiteId id,Fixed delta_micro,GameplayContext context={});`
- `6a5062f5ee4ac181` | `QUERY` | `epidemic::gameplay::construction::OrientationFixed` | `[[nodiscard]] constexpr bool operator==(const OrientationFixed&)const noexcept=default;`
- `6e84ef1086477c18` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<PlacementPlan> PreparePlacementPlan(const PlacementRequest&request);`
- `6f820829135cde16` | `FACTORY` | `epidemic::gameplay::construction::PlacementOutputId` | `static constexpr PlacementOutputId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `76c7803186e77f27` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `77a738b6d4331c41` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] std::vector<ConstructionSite> FindConstructionSites(GameplayObjectRef actor={})const;`
- `7973d7949913b69c` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<PlacementValidationResult> ValidatePlacement(const PlacementRequest&request)const;`
- `7aeb10e071226fc7` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] std::size_t ExpirePlacementPlans(GameplayTimePoint now,GameplayContext context={});`
- `7df296ef17ff8f3c` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> ReleaseSocket(ConstructionSocketReservationId reservation,GameplayContext context={});`
- `7f41ee28e197386a` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> RegisterSocket(PlacementSocket socket);`
- `80b1b14eb25780b2` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> CompleteConstructionSite(ConstructionSiteId id,GameplayContext context={});`
- `819dd921ffe5e187` | `QUERY` | `epidemic::gameplay::construction::IConstructionCostProvider` | `[[nodiscard]] virtual foundation::Result<bool> CanAfford(GameplayObjectRef actor,const ConstructionCost&cost,const GameplayContext&context)const=0;`
- `83466b81bd35003e` | `QUERY` | `epidemic::gameplay::construction::ConstructionSocketReservationId` | `[[nodiscard]] constexpr auto operator<=>(const ConstructionSocketReservationId&)const noexcept=default;`
- `8410efcead91ea83` | `QUERY` | `epidemic::gameplay::construction::PlacementExecutionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `873a7c32fac5ddca` | `FACTORY` | `epidemic::gameplay::construction::PlacementReasonId` | `static constexpr PlacementReasonId FromString(std::string_view s)noexcept`
- `8a96c6d9d339eb45` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] const ConstructionRecipe*FindRecipe(ConstructionRecipeId id)const noexcept;`
- `8f3d50e3dfd366ec` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] ConstructionSnapshot CaptureSnapshot()const;`
- `93b5a37dc9ec930d` | `QUERY` | `epidemic::gameplay::construction::ConstructionRecipeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `9aa19e46f6df49ee` | `FACTORY` | `epidemic::gameplay::construction::PlacementSocketId` | `static constexpr PlacementSocketId FromString(std::string_view s)noexcept`
- `9bbdcabd974db427` | `FACTORY` | `epidemic::gameplay::construction::PlacementExecutionId` | `static constexpr PlacementExecutionId FromString(std::string_view s)noexcept`
- `9c6693430e224a37` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `a26ba6d9f13639ce` | `QUERY` | `epidemic::gameplay::construction::ConstructionRecipeId` | `[[nodiscard]] constexpr auto operator<=>(const ConstructionRecipeId&)const noexcept=default;`
- `a34c5ff98113ef04` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `a4f3b9bb3f0828f5` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] bool IsFrozen()const noexcept`
- `a6772a8c6a0bdc0b` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] ConstructionChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `a90afc7e4277c313` | `LIFECYCLE` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<ConstructionSiteId> StartConstructionSite(const PlacementPlan&plan,GameplayTimePoint started_at={},GameplayContext context={});`
- `aaaed4b3d351965f` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] const PlacementDefinition*FindPlacementDefinition(PlacementRuleId id)const noexcept;`
- `b435281879c38893` | `FACTORY` | `epidemic::gameplay::construction::PlacementExecutionId` | `static constexpr PlacementExecutionId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `b6398adb4bd9d602` | `QUERY` | `epidemic::gameplay::construction::ConstructionSocketReservationId` | `[[nodiscard]] constexpr bool operator==(const ConstructionSocketReservationId&)const noexcept=default;`
- `b88ff2a8ca574bb0` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> CompactPlacementState(PlacementPlanId plan,GameplayContext context={});`
- `bb13f81b9fab00ed` | `FACTORY` | `epidemic::gameplay::construction::PlacementOutputTypeId` | `static constexpr PlacementOutputTypeId FromString(std::string_view s)noexcept`
- `bc244f72449b3174` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementOutputId&)const noexcept=default;`
- `be118e00c223f723` | `QUERY` | `epidemic::gameplay::construction::PlacedObjectId` | `[[nodiscard]] constexpr bool operator==(const PlacedObjectId&)const noexcept=default;`
- `c1ace9dc237a0965` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputId` | `[[nodiscard]] constexpr bool operator==(const PlacementOutputId&)const noexcept=default;`
- `c228d159a1552a52` | `LIFECYCLE` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> PauseConstructionSite(ConstructionSiteId id,GameplayContext context={});`
- `c38e161b102692be` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<PlacementCommitResult> CommitPlacement(const PlacementPlan&plan);`
- `ca86f311966f5683` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<PlacementOutputId> EnqueuePlacedObjectOutput(PlacedObjectId placed_object,PlacementOutputOperation operation,GameplayContext context={});`
- `cedcad849ba34da4` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> AcknowledgeOutput(PlacementOutputId id,GameplayContext context={});`
- `d06aca4c221a2966` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> CancelPlacementPlan(PlacementPlanId plan,GameplayContext context={});`
- `d3338efd186dfebe` | `QUERY` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] std::vector<PlacementSocket> FindSocketsForObject(GameplayObjectRef owner)const;`
- `d3a9898b2c42b1db` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> OccupySocket(PlacementSocketId id,std::optional<ConstructionSocketReservationId> reservation={},GameplayContext context={});`
- `d6a704632e44f21a` | `DESTRUCTOR` | `epidemic::gameplay::construction::IConstructionCostProvider` | `virtual ~IConstructionCostProvider()=default;`
- `dafec5aa529b4aa7` | `QUERY` | `epidemic::gameplay::construction::IConstructionPlacementProvider` | `[[nodiscard]] virtual Revision CurrentRevision()const noexcept=0;`
- `dc771a578adf514d` | `FACTORY` | `epidemic::gameplay::construction::PlacementRuleId` | `static constexpr PlacementRuleId FromString(std::string_view s)noexcept`
- `dd79b71a0f1f89a9` | `QUERY` | `epidemic::gameplay::construction::PlacedObjectId` | `[[nodiscard]] constexpr auto operator<=>(const PlacedObjectId&)const noexcept=default;`
- `dd99371adb55f77c` | `QUERY` | `epidemic::gameplay::construction::PlacementPlanId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `dfda5a35266c9eb5` | `FACTORY` | `epidemic::gameplay::construction::ConstructionCostTypeId` | `static constexpr ConstructionCostTypeId FromString(std::string_view s)noexcept`
- `dfe5568f007920b4` | `QUERY` | `epidemic::gameplay::construction::PlacementExecutionId` | `[[nodiscard]] constexpr bool operator==(const PlacementExecutionId&)const noexcept=default;`
- `e076998e711ea041` | `MUTATOR` | `epidemic::gameplay::construction::IConstructionCostProvider` | `virtual void Release(const ConstructionCostReservation&reservation,const GameplayContext&context)noexcept=0;`
- `e175c4df426aa362` | `FACTORY` | `epidemic::gameplay::construction::PlacementPlanId` | `static constexpr PlacementPlanId FromString(std::string_view s)noexcept`
- `e1c2319cd463a37b` | `QUERY` | `epidemic::gameplay::construction::PlacementOutputTypeId` | `[[nodiscard]] constexpr bool operator==(const PlacementOutputTypeId&)const noexcept=default;`
- `e6133696ceea1d18` | `QUERY` | `epidemic::gameplay::construction::ConstructionSiteId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f2c96a437dd67356` | `QUERY` | `epidemic::gameplay::construction::PlacementExecutionId` | `[[nodiscard]] constexpr auto operator<=>(const PlacementExecutionId&)const noexcept=default;`
- `f430da016f437538` | `FACTORY` | `epidemic::gameplay::construction::ConstructionSocketReservationId` | `static constexpr ConstructionSocketReservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `f5e024906d5d18ad` | `MUTATOR` | `epidemic::gameplay::construction::IConstructionCostProvider` | `virtual void Commit(const ConstructionCostReservation&reservation,const GameplayContext&context)noexcept=0;`
- `fa3de7888b29e832` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> PruneTerminalSite(ConstructionSiteId id,GameplayContext context={});`
- `fb7ffe75545b17bd` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<void> FailConstructionSite(ConstructionSiteId id,PlacementReasonId reason,GameplayContext context={});`
- `fc698ff45647d1d5` | `DESTRUCTOR` | `epidemic::gameplay::construction::IConstructionPlacementProvider` | `virtual ~IConstructionPlacementProvider()=default;`
- `fd26fcfaf6e5e085` | `QUERY` | `epidemic::gameplay::construction::ConstructionSocketReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `ffe419a2c61262ff` | `MUTATOR` | `epidemic::gameplay::construction::ConstructionService` | `[[nodiscard]] foundation::Result<PlacementCommitResult> CommitPlacement(PlacementPlanId plan);`
