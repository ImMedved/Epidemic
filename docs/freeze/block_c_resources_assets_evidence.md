# Block C evidence fragment: Resources + Assets

Date: 2026-09-21.

This file is a serial-integration input. It intentionally does not edit `module_dossiers.md`, `coverage_manifests.json`, `public_api_inventory.md`, `public_surface_manifest.json`, `local_ready_ledger.json`, `public_api_anchors.json` or `module_dossier_reviews.json`.

## Qualification summary

Block C changes the Assets allocation-fault harness, adds one Resources time-budget hardening regression, and updates module-local documentation/evidence. Resources production code was source-reviewed and requalified without a production change because the four required G3-RES regressions and the ownership/retry implementation are already present.

Local isolated builds with warnings-as-errors:

- GCC 14 Debug: Assets PASS, Resources PASS.
- GCC 14 Release: Assets PASS, Resources PASS.
- Clang 17 Debug: Assets PASS, Resources PASS.
- Clang 17 Release: Assets PASS, Resources PASS.
- GCC 14 Debug + ASan/UBSan: Assets PASS, Resources PASS.
- GCC 14 Debug + libstdc++ debug iterators/assertions: Assets PASS, Resources PASS.
- Public-header self-containment: GCC 9/9 Assets and 11/11 Resources PASS; Clang 9/9 Assets and 11/11 Resources PASS.
- Allocator scan: no process-global `operator new`/`operator new[]` replacement in Assets or Resources tests.

`TestTimeBudgetStopsFurtherJobs` explicitly proves that a completed current unit can consume the time budget and prevents the next queued load from starting.

MSVC is not available in this Linux environment. `LOCAL_READY` must remain a candidate until the merged tree passes the required MSVC Debug/Release runs.

## Defect evidence

| ID | Module | Fix/evidence | Permanent regression | Target |
| --- | --- | --- | --- | --- |
| G3-RES-001 | Resources | `ResourceDependencyGraph::ReplaceDependencies()` stages the replacement before final swap/insert commit. | `TestDependencyReplacementStrongCommit` | `EpidemicRuntimeResourcesTests` |
| G3-RES-002 | Resources | Load-queue owner is retained until waiting-owner publication succeeds. | `TestWaitingOwnerPublicationFailureKeepsLoadOwner` | `EpidemicRuntimeResourcesTests` |
| G3-RES-003 | Resources | Loader success is moved into `pending_artifact` before fallible Runtime-local finalization. | `TestLocalFinalizationFailureDoesNotReloadArtifact` | `EpidemicRuntimeResourcesTests` |
| G3-RES-004 | Resources | Selected queue item is not popped before loading ownership is durably published. | `TestSelectedJobOwnershipSurvivesLoadingPublicationFailure` | `EpidemicRuntimeResourcesTests` |
| G3-ASSET-001 | Assets | Manifest requiredness is merged by `AssetId` with logical OR and sorted deterministically. | `TestDependencyManifestMergesRequirednessIndependentlyOfTraversalOrder` | `EpidemicRuntimeAssetsTests` |
| G3-ASSET-002 | Assets | `IsSafeRelativePath()` rejects embedded NUL before host/filesystem boundaries. | `TestPathValidationRejectsTraversalAndRequiresMounts` | `EpidemicRuntimeAssetsTests` |
| G3-ASSET-003 | Assets | `Resolve(AssetId{})` returns `asset.invalid_id`, distinct from valid-but-missing `asset.not_found`. | `TestInvalidQueryInputsAreControlled` | `EpidemicRuntimeAssetsTests` |
| M3C-ASSET-HARNESS-001 | Assets | Process-global allocator replacement removed. Named private seams cover metadata candidate build and final catalog publication. | `TestRegistrationAllocationFailurePreservesCatalog` | `EpidemicRuntimeAssetsTests` |
| C2-RES-TIME-BUDGET | Resources | Time-budget semantics are now explicit rather than inferred from the general budget loop. | `TestTimeBudgetStopsFurtherJobs` | `EpidemicRuntimeResourcesTests` |

`BuildDependencyManifest()` does not receive a new allocation seam: its public contract explicitly allows process allocation failure to propagate and the method is read-only. C3 only requires manifest staging injection if allocation failure is a module `Result` boundary.

## Stale-identity and lifecycle candidate decisions

### Assets

- `IAssetCatalogWriter::RegisterAsset(...)`: stale-identity scanner false positive. `AssetId` is caller-provided and immutable; duplicate registration is rejected. Assets owns no generation/reuse allocator.
- `IAssetCatalogWriter::Seal()`: stale-identity scanner false positive. It is a monotonic lifecycle transition, not identity reuse. Seal idempotence and registration-after-seal are covered by `TestCatalogSeal` and `TestSealIdempotenceAndEmptyCatalogContracts`.

### Resources

- `IResourceManager::Release(ResourceLease)`: real stale/acquisition obligation. Covered by `TestResourceLeasePreventsCrossConsumerDoubleRelease` and `TestUnknownAndStaleHandles`.
- `IResourceManager::Evict(ResourceId)`: real stale-generation obligation. Covered by `TestUnknownAndStaleHandles` and `TestGenerationExhaustionPreventsAba`.
- `IResourceManager::EvictUnreferenced()`: real generation/cleanup obligation. Covered by `TestInFlightResourcesAreNotEvicted`, `TestGenerationExhaustionPreventsAba` and `TestEvictingRetryKeepsCompletedPrefix`.
- `IResourceLoaderRegistry::RegisterLoader(...)`: stale-identity scanner false positive. Registry key is a non-recycled `ResourceType`; duplicate registration is a normal validation failure.
- `IResourceLoaderRegistry::Freeze()`: stale-identity scanner false positive, real lifecycle candidate. Covered by `TestLoaderRegistryFreezeAndEmptyQueueBoundaries`.
- `IResourceManager::SetMemoryBudgetBytes(...)`: stale-identity scanner false positive. It mutates a numeric budget and owns no identity.

## Dossier review decisions

### Assets

All 15 dossier fields remain substantively reviewed by `engineruntime_assets_audit.md`. Block C changes only failure-injection infrastructure and therefore does not change the ownership, state, persistence, lifecycle or public-contract conclusions. The audit status is downgraded from admitted `LOCAL_READY` to `LOCAL_READY candidate` until post-rewrite MSVC qualification.

### Resources

| Dossier field | Review decision |
| --- | --- |
| Responsibility | Resources owns runtime residency/loading, consumer leases, dependency leases/graph, budgets and eviction. Keep `REVIEWED`. |
| Dependency list | Direct build dependencies are Foundation + RuntimeFoundation only; Runtime peer dependencies are absent. `REVIEWED`. |
| Public headers and types | Public types/interfaces are the 11 headers under `EngineRuntime/Resources/include/Epidemic/Runtime/Resources`. `REVIEWED` after self-contained-header check. |
| Authoritative state | `ResourceSlot` state plus loader registry membership/frozen flag are authoritative. `REVIEWED`. |
| Derived/cache/index state | load/wait queues, dependency graph, `loading_resources_` and resident-byte aggregate are derived/coordination state with regression coverage. `REVIEWED`. |
| ID spaces, generations, revisions and cursors | `ResourceAcquisitionId` uint64 monotonic allocator, uint32 resource generation and uint32 refcount are bounded and tested for exhaustion/ABA. No revision/cursor space. `REVIEWED`. |
| State machines | `ResourceState` lifecycle and registry mutable→frozen lifecycle reviewed; eviction has explicit retryable `Evicting` prefix. `REVIEWED`. |
| Local invariants | Lease uniqueness, current-generation handles, payload/accounting consistency, dependency ownership and queue ownership reviewed. `REVIEWED`. |
| Public mutation API | Registry registration/freeze and manager request/process/release/evict/budget mutators reviewed with named tests. `REVIEWED`. |
| Read/query API for invariants | loader lookup, handle validation/state/readiness/id/payload and memory statistics expose detached/non-owning observations. `REVIEWED`. |
| External ports/callbacks/providers/backends | `IResourceLoader::Load` is the only external execution boundary; Result/exception failure and accepted-once staging reviewed. `REVIEWED`. |
| Hard limits, budgets and complexity bounds | item/time/byte budgets, memory budget, size_t accounting, uint32 generation/refcount and uint64 acquisition exhaustion, iterative deep dependency search reviewed. `REVIEWED`. |
| Threading contract | public mutation is runtime-thread-only; no worker threads are created. `REVIEWED`. |
| Persistent and transient state | all module state is transient runtime state; no persistence API exists. `REVIEWED`. |
| Snapshot/restore contract | no snapshot/restore API or local persistence contract; explicit `N/A` rationale reviewed. `REVIEWED`. |

## Resources mutator obligation decisions

Every generated Resources mutator obligation is proposed `REVIEWED`; the notes below state the concrete semantics rather than mechanically setting the status.

| Callable | Preconditions | Success | Failure | No-op / repeated call |
| --- | --- | --- | --- | --- |
| `IResourceLoader::Load(ResourceRequest)` | Port receives a validated request from manager. | Artifact is only accepted after ID/type/payload validation and durable `pending_artifact` staging. | Result failure and exceptions are contained; no false ready publication. | No module-owned no-op semantics; review is of the external port contract. |
| `IResourceLoaderRegistry::RegisterLoader(IResourceLoader&)` | Not frozen, valid type, no duplicate type. | Publishes one non-owning pointer. | Validation/allocation failure leaves registry unchanged. | No successful duplicate no-op; duplicate/frozen are failures. |
| `IResourceLoaderRegistry::Freeze()` | None. | Monotonically sets frozen. | No fallible step after mutation. | Repeated freeze succeeds and remains frozen. |
| `IResourceManager::RequestLease(ResourceRequest)` | Valid ID/type, compatible existing type/state, representable acquisition/refcount. | Unique acquisition is published and queued work created when required; acquisition ID commits last. | Queue/acquisition allocation failure rolls back local publication; invalid/exhausted inputs do not mutate. | Ready resource request does not reload but creates a new independent lease. |
| `IResourceManager::ProcessPendingLoads(RuntimeBudget)` | Budget values use RuntimeBudget semantics; queue entries are validated against current slot generation. | Processes deterministic accepted prefix and publishes ready state only after all required checks. | Retryable local failures retain durable queue/pending-artifact ownership; terminal loader failures become Failed. | Empty/zero-item budget performs no work. |
| `IResourceManager::Release(ResourceLease)` | Valid current lease with active acquisition and positive refcount. | Removes exactly one acquisition and decrements refcount once. | Invalid/stale/double release leaves state unchanged. | No successful duplicate no-op. |
| `IResourceManager::Evict(ResourceId)` | Valid existing unreferenced slot and advanceable generation. | Cleanup completes, payload/dependency state is removed, generation advances, state becomes Evicted. | Failed dependency cleanup retains `Evicting` ownership for retry; generation exhaustion rejects before mutation. | No successful repeated no-op contract. |
| `IResourceManager::EvictUnreferenced()` | None. | Evicts eligible completed entries and returns count. | Entries whose cleanup/generation cannot complete remain owned/retryable and are not counted. | No eligible entries returns 0. |
| `IResourceManager::SetMemoryBudgetBytes(std::size_t)` | Any `size_t`; zero means unlimited. | Replaces future admission budget. | No allocation/failure boundary. | Setting same value is observationally idempotent. |

## Callable anchor proposals

The serial integrator should bind final generated IDs by exact scope + signature after all blocks merge. Scope is included because signatures such as `IsValid() const noexcept` occur in more than one public type. Contract anchors intentionally use module-local audit sections; test/audit anchors name stable test functions instead of pre-merge line numbers.

### Assets public callables

| Scope | Signature | Contract anchor | Test/audit anchor |
| --- | --- | --- | --- |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual bool Contains(AssetId id)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual bool IsSealed()const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual foundation::Result<AssetDependencyManifest> BuildDependencyManifest(AssetId root)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual std::optional<AssetMetadata> FindById(AssetId id)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual std::vector<AssetMetadata> FindByTag(foundation::StringId tag)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `[[nodiscard]] virtual std::vector<AssetMetadata> FindByType(AssetType type)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalog` | `virtual ~IAssetCatalog()=default;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestFindByIdReturnsSnapshot / TestFindByTypeAndTagWork / TestDependencyManifestAndCycle` |
| `epidemic::runtime::IAssetCatalogWriter` | `[[nodiscard]] virtual foundation::Result<void> RegisterAsset(const AssetMetadata&metadata)=0;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCatalogSeal / TestRegistrationAllocationFailurePreservesCatalog` |
| `epidemic::runtime::IAssetCatalogWriter` | `[[nodiscard]] virtual foundation::Result<void> Seal()=0;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCatalogSeal / TestRegistrationAllocationFailurePreservesCatalog` |
| `epidemic::runtime::IAssetCatalogWriter` | `virtual ~IAssetCatalogWriter()=default;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCatalogSeal / TestRegistrationAllocationFailurePreservesCatalog` |
| `epidemic::runtime` | `[[nodiscard]] AssetLocation CanonicalizeAssetLocation(AssetLocation location);` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime` | `[[nodiscard]] bool IsValidAssetLocation(const AssetLocation&location)noexcept;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime` | `[[nodiscard]] constexpr bool IsValidAssetLocationKind(AssetLocationKind value)noexcept` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime::AssetLocation` | `AssetLocation()=default;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime::AssetLocation` | `AssetLocation(AssetLocationKind location_kind,std::string location_path)` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime::AssetLocation` | `[[nodiscard]] bool Empty()const noexcept` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime::AssetLocation` | `[[nodiscard]] bool operator==(const AssetLocation&)const=default;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestCanonicalPaths / TestPathValidationRejectsTraversalAndRequiresMounts` |
| `epidemic::runtime::IAssetLocationResolver` | `[[nodiscard]] virtual foundation::Result<AssetLocation> Resolve(AssetId id)const=0;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestAssetLocationResolverReturnsRegisteredLocation / TestInvalidQueryInputsAreControlled` |
| `epidemic::runtime::IAssetLocationResolver` | `virtual ~IAssetLocationResolver()=default;` | `engineruntime_assets_audit.md::Query and lifecycle audit` | `assets_tests.cpp::TestAssetLocationResolverReturnsRegisteredLocation / TestInvalidQueryInputsAreControlled` |
| `epidemic::runtime::AssetDependency` | `[[nodiscard]] constexpr bool operator==(const AssetDependency&)const noexcept=default;` | `engineruntime_assets_audit.md::Dependency manifest audit` | `assets_tests.cpp::TestDependencyManifestMergesRequirednessIndependentlyOfTraversalOrder` |
| `epidemic::runtime` | `[[nodiscard]] foundation::Result<AssetServices> CreateAssetServices(const AssetsOptions&options={});` | `engineruntime_assets_audit.md::Scope and ownership` | `assets_tests.cpp::TestAssetServicesFactory` |
| `epidemic::runtime` | `[[nodiscard]] constexpr bool IsValidAssetState(AssetState value)noexcept` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestEnumTagAndFailureAtomicity` |
| `epidemic::runtime::AssetType` | `[[nodiscard]] constexpr bool IsValid()const noexcept` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestValidationFailures` |
| `epidemic::runtime::AssetType` | `[[nodiscard]] constexpr bool operator==(const AssetType&)const noexcept=default;` | `engineruntime_assets_audit.md::Registration and validation audit` | `assets_tests.cpp::TestValidationFailures` |

### Resources public callables

| Scope | Signature | Contract anchor | Test/audit anchor |
| --- | --- | --- | --- |
| `epidemic::runtime::ResourceDependency` | `[[nodiscard]] constexpr bool operator==(const ResourceDependency&)const noexcept=default;` | `engineruntime_resources_audit.md::State, identities and derived ownership` | `resources_tests.cpp::TestDependencyGraphStoresDependencies` |
| `epidemic::runtime::ResourceHandle` | `[[nodiscard]] constexpr bool IsValid()const noexcept` | `engineruntime_resources_audit.md::State, identities and derived ownership` | `resources_tests.cpp::TestBasicTypesAndStateTransitions / TestUnknownAndStaleHandles` |
| `epidemic::runtime::ResourceHandle` | `[[nodiscard]] constexpr bool operator==(const ResourceHandle&)const noexcept=default;` | `engineruntime_resources_audit.md::State, identities and derived ownership` | `resources_tests.cpp::TestBasicTypesAndStateTransitions / TestUnknownAndStaleHandles` |
| `epidemic::runtime::ResourceLease` | `[[nodiscard]] constexpr bool IsValid()const noexcept` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestResourceLeasePreventsCrossConsumerDoubleRelease` |
| `epidemic::runtime::ResourceLease` | `[[nodiscard]] constexpr bool operator==(const ResourceLease&)const noexcept=default;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestResourceLeasePreventsCrossConsumerDoubleRelease` |
| `epidemic::runtime::IResourceLoader` | `[[nodiscard]] virtual ResourceType GetResourceType()const=0;` | `engineruntime_resources_audit.md::External loader boundary and accepted-once semantics` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestInvalidLoaderArtifactsFail / TestLoaderExceptionDoesNotPoisonRetry` |
| `epidemic::runtime::IResourceLoader` | `[[nodiscard]] virtual foundation::Result<ResourceLoadArtifact> Load(ResourceRequest request)=0;` | `engineruntime_resources_audit.md::External loader boundary and accepted-once semantics` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestInvalidLoaderArtifactsFail / TestLoaderExceptionDoesNotPoisonRetry` |
| `epidemic::runtime::IResourceLoader` | `virtual ~IResourceLoader()=default;` | `engineruntime_resources_audit.md::External loader boundary and accepted-once semantics` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestInvalidLoaderArtifactsFail / TestLoaderExceptionDoesNotPoisonRetry` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual IResourceLoader*FindLoader(ResourceType type)=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual bool HasLoader(ResourceType type)const=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual bool IsFrozen()const noexcept=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual const IResourceLoader*FindLoader(ResourceType type)const=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual foundation::Result<void> Freeze()=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `[[nodiscard]] virtual foundation::Result<void> RegisterLoader(IResourceLoader&loader)=0;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceLoaderRegistry` | `virtual ~IResourceLoaderRegistry()=default;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestLoaderRegistryNonOwningContract / TestLoaderRegistryFreezeAndEmptyQueueBoundaries` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual ResourceMemoryStats GetMemoryStatistics()const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestMemoryStatisticsTrackUnreferencedCache / TestResidentAccountingOverflowRejectsBeforePublication` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual ResourcePayloadPtr GetPayload(ResourceHandle handle)const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestUnknownAndStaleHandles` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual ResourceState GetState(ResourceHandle handle)const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestUnknownAndStaleHandles` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual bool IsReady(ResourceHandle handle)const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload / TestUnknownAndStaleHandles` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual foundation::Result<ResourceLease> RequestLease(ResourceRequest request)=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestResourceLeasePreventsCrossConsumerDoubleRelease / TestReferenceOverflowAndQueuePublicationAreAtomic / TestAcquisitionIdCommitLastAndDependencyPublicationRollback` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual foundation::Result<ResourceProcessingStats> ProcessPendingLoads(RuntimeBudget budget={})=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestProcessBudgetLimitsJobs / TestTimeBudgetStopsFurtherJobs / TestByteBudgetStopsFurtherJobs / TestWaitingOwnerPublicationFailureKeepsLoadOwner / TestLocalFinalizationFailureDoesNotReloadArtifact / TestSelectedJobOwnershipSurvivesLoadingPublicationFailure` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual foundation::Result<void> Evict(ResourceId id)=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestUnknownAndStaleHandles / TestGenerationExhaustionPreventsAba / TestEvictingRetryKeepsCompletedPrefix` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual foundation::Result<void> Release(ResourceLease lease)=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestResourceLeasePreventsCrossConsumerDoubleRelease / TestUnknownAndStaleHandles / TestFailedDependencyReleasePreservesLeaseForRetry` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual foundation::Result<void> ValidateHandle(ResourceHandle handle)const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestUnknownAndStaleHandles` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual std::optional<ResourceId> GetResourceId(ResourceHandle handle)const=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestUnknownAndStaleHandles` |
| `epidemic::runtime::IResourceManager` | `[[nodiscard]] virtual std::size_t EvictUnreferenced()=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestInFlightResourcesAreNotEvicted / TestEvictingRetryKeepsCompletedPrefix` |
| `epidemic::runtime::IResourceManager` | `virtual void SetMemoryBudgetBytes(std::size_t bytes)=0;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestMemoryBudgetFailureRollsBackPayload / TestMemoryBudgetOverflowCannotFit / TestResidentAccountingOverflowRejectsBeforePublication` |
| `epidemic::runtime::IResourceManager` | `virtual ~IResourceManager()=default;` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestFactoryCreatesUsableServices` |
| `epidemic::runtime::ByteResourcePayload` | `[[nodiscard]] const std::vector<std::byte>&Bytes()const noexcept` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload` |
| `epidemic::runtime::ByteResourcePayload` | `[[nodiscard]] std::size_t GetSizeBytes()const noexcept override` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload` |
| `epidemic::runtime::ByteResourcePayload` | `explicit ByteResourcePayload(std::vector<std::byte> bytes={})` | `engineruntime_resources_audit.md::Public mutation and query contract` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload` |
| `epidemic::runtime::IResourcePayload` | `[[nodiscard]] virtual std::size_t GetSizeBytes()const noexcept=0;` | `engineruntime_resources_audit.md::External loader boundary and accepted-once semantics` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload` |
| `epidemic::runtime::IResourcePayload` | `virtual ~IResourcePayload()=default;` | `engineruntime_resources_audit.md::External loader boundary and accepted-once semantics` | `resources_tests.cpp::TestRequestQueuesAndProcessLoadsPayload` |
| `epidemic::runtime` | `[[nodiscard]] foundation::Result<ResourceServices> CreateResourceServices(const ResourceOptions&options={});` | `engineruntime_resources_audit.md::Scope and ownership` | `resources_tests.cpp::TestFactoryCreatesUsableServices` |
| `epidemic::runtime` | `[[nodiscard]] constexpr bool CanTransition(ResourceState from,ResourceState to)noexcept` | `engineruntime_resources_audit.md::State, identities and derived ownership` | `resources_tests.cpp::TestBasicTypesAndStateTransitions` |
| `epidemic::runtime::ResourceType` | `[[nodiscard]] constexpr bool IsValid()const noexcept` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestBasicTypesAndStateTransitions / TestLoaderRegistryNonOwningContract` |
| `epidemic::runtime::ResourceType` | `[[nodiscard]] constexpr bool operator==(const ResourceType&)const noexcept=default;` | `engineruntime_resources_audit.md::Loader registry lifecycle` | `resources_tests.cpp::TestBasicTypesAndStateTransitions / TestLoaderRegistryNonOwningContract` |

## LOCAL_READY criteria proposals

These are proposed serial-integration decisions, not edits to the shared ledger.

### Assets

The substantive contract decisions remain the existing 37 reviewed criteria, reproduced here as serial-integration proposals so Block C returns a complete criterion fragment rather than asking the integrator to infer it. Test anchors that moved in `assets_tests.cpp` must be rebound to final lines after merge. Overall module status remains `LOCAL_READY candidate` until MSVC Debug/Release passes the rewritten harness.

| Criterion | Proposed status | Evidence/rationale |
| --- | --- | --- |
| ARCH-RESPONSIBILITY | PASS | Verified by the generated architecture/ownership gate. Evidence: `docs/freeze/architecture_ownership_matrix.md::## EngineRuntime/Assets` |
| ARCH-SINGLE-OWNER | PASS | Verified by the generated architecture/ownership gate. Evidence: `docs/freeze/architecture_ownership_matrix.md::## EngineRuntime/Assets` |
| ARCH-DIRECT-DEPS | PASS | Verified by the generated architecture/ownership gate. Evidence: `docs/freeze/architecture_ownership_matrix.md::## EngineRuntime/Assets` |
| ARCH-PORTS | PASS | Verified by the generated architecture/ownership gate. Evidence: `docs/freeze/architecture_ownership_matrix.md::## EngineRuntime/Assets` |
| API-CLASSIFIED | PASS | `docs/freeze/engineruntime_assets_audit.md::## Scope and ownership` |
| API-PRECONDITIONS | PASS | `docs/freeze/engineruntime_assets_audit.md::## Registration and validation audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestValidationFailures()` |
| API-SUCCESS | PASS | `docs/freeze/engineruntime_assets_audit.md::## Registration and validation audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestRegisterAssetStoresMetadata(); EngineRuntime/Assets/tests/assets_tests.cpp::bool TestSealIdempotenceAndEmptyCatalogContracts()` |
| API-FAILURE | PASS | `docs/freeze/engineruntime_assets_audit.md::## Failure atomicity and observable state; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestRegistrationAllocationFailurePreservesCatalog()` |
| API-OVERLOADS | PASS | Assets has no ambiguous public overload family; each callable is represented by its exact public signature. Evidence: `docs/freeze/engineruntime_assets_audit.md::## Scope and ownership` |
| API-INVALID | PASS | `docs/freeze/engineruntime_assets_audit.md::## Registration and validation audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestInvalidQueryInputsAreControlled()` |
| STATE-PRIMARY | PASS | `docs/freeze/engineruntime_assets_audit.md::## Scope and ownership; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestRegistrationAllocationFailurePreservesCatalog()` |
| STATE-INDEXES | N/A | Assets owns no secondary derived index or cache; queries scan the authoritative metadata map. |
| STATE-COUNTERS | N/A | Assets generates no IDs, generations, revisions, or cursors; AssetMetadata::version is caller-supplied per-asset metadata. |
| STATE-NO-FALSE-PUBLISH | N/A | Assets publishes no revision, event, or journal stream. |
| STATE-NOOP | PASS | `docs/freeze/engineruntime_assets_audit.md::## Query and lifecycle audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestSealIdempotenceAndEmptyCatalogContracts()` |
| LIFE-ALLOWED | PASS | `docs/freeze/engineruntime_assets_audit.md::## Query and lifecycle audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestCatalogSeal()` |
| LIFE-FORBIDDEN | PASS | `docs/freeze/engineruntime_assets_audit.md::## Query and lifecycle audit; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestCatalogSeal()` |
| LIFE-SHUTDOWN | N/A | Assets owns no external resource, thread, registration, or backend handle requiring shutdown. |
| LIFE-RETRY-CLEANUP | N/A | Assets has no fallible external cleanup protocol. |
| ATOMIC-SINGLE | PASS | `docs/freeze/engineruntime_assets_audit.md::## Failure atomicity and observable state; EngineRuntime/Assets/tests/assets_tests.cpp::bool TestRegistrationAllocationFailurePreservesCatalog()` |
| ATOMIC-MULTI | N/A | RegisterAsset publishes to one authoritative map and Seal mutates one boolean; no mutator spans multiple authoritative containers. |
| ATOMIC-EXTERNAL | N/A | Assets invokes no external transactional prepare/commit/cancel/rollback boundary. |
| ATOMIC-RECONCILE | N/A | Assets has no fallible external rollback and therefore no reconciliation ownership. |
| PERSIST-SNAPSHOT | N/A | Assets exposes no local snapshot/restore contract; persistent catalog production is outside Goal 3.5. |
| PERSIST-VALIDATE | N/A | Assets exposes no restore operation. |
| PERSIST-CANDIDATE | N/A | Assets exposes no restore candidate or live restore mutation. |
| PERSIST-FAILURE | N/A | Assets exposes no restore operation whose failure could mutate live catalog state. |
| PERSIST-CONTINUITY | N/A | Assets owns no generated identity/revision/cursor continuity and exposes no restore contract. |
| TEST-HAPPY | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestRegisterAssetStoresMetadata()` |
| TEST-INVALID | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestValidationFailures()` |
| TEST-DUPLICATE | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestDuplicateAssetIdReturnsError()` |
| TEST-STALE | N/A | Asset IDs are never removed or reused by this module, so stale-generation identity cannot be presented. |
| TEST-EMPTY | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestSealIdempotenceAndEmptyCatalogContracts()` |
| TEST-BOUNDARY | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestMetadataBoundsRejectBeforeMutation()` |
| TEST-WRONG-LIFECYCLE | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestCatalogSeal()` |
| TEST-CALLBACK-FAILURE | N/A | Assets invokes no callback, provider, sink, or backend. |
| TEST-REGRESSION | PASS | `EngineRuntime/Assets/tests/assets_tests.cpp::bool TestPathValidationRejectsTraversalAndRequiresMounts(); EngineRuntime/Assets/tests/assets_tests.cpp::bool TestInvalidQueryInputsAreControlled(); EngineRuntime/Assets/tests/assets_tests.cpp::bool TestDependencyManifestMergesRequirednessIndependentlyOfTraversalOrder()` |

### Resources

| Criterion | Proposed status | Evidence/rationale |
| --- | --- | --- |
| ARCH-RESPONSIBILITY | PASS | Existing generated ownership matrix plus this audit. |
| ARCH-SINGLE-OWNER | PASS | Slots/queues/dependency ownership remain inside Resources. |
| ARCH-DIRECT-DEPS | PASS | CMake links Foundation + RuntimeFoundation only; no Runtime peer major. |
| ARCH-PORTS | PASS | Loader is an explicit public port; no Framework dependency. |
| API-CLASSIFIED | PASS | 37 callables reviewed; mutator obligations enumerated above. |
| API-PRECONDITIONS | PASS | Invalid request/type/lease/id, frozen registry, type mismatch, in-use eviction and bounds are tested. |
| API-SUCCESS | PASS | Happy request/load/read/release/evict/factory paths covered. |
| API-FAILURE | PASS | Loader Result/exception, allocation seams, budget and cleanup failures are controlled. |
| API-OVERLOADS | PASS | Const/non-const `FindLoader` pair has consistent lookup semantics; no ambiguous overload family. |
| API-INVALID | PASS | Invalid request, handle, lease, artifact and dependency inputs have explicit tests. |
| STATE-PRIMARY | PASS | Slot/registry authoritative state reviewed and queried after failures. |
| STATE-INDEXES | PASS | Queues, dependency graph, loading set and accounting stay consistent across rollback/retry regressions. |
| STATE-COUNTERS | PASS | acquisition/generation/refcount/accounting exhaustion boundaries are tested. |
| STATE-NO-FALSE-PUBLISH | PASS | Failed loads/finalization/accounting do not publish false Ready or consumed ownership. |
| STATE-NOOP | PASS | repeated ready request avoids reload; freeze idempotent; zero/empty work is no-op. |
| LIFE-ALLOWED | PASS | ResourceState and registry freeze allowed transitions covered. |
| LIFE-FORBIDDEN | PASS | registration after freeze, acquire while Evicting, in-use eviction and stale operations rejected. |
| LIFE-SHUTDOWN | N/A | Resources exposes no shutdown lifecycle. |
| LIFE-RETRY-CLEANUP | PASS | dependency release and eviction prefix failures retain retry ownership. |
| ATOMIC-SINGLE | PASS | slot mutation boundaries reject before/rollback on failure. |
| ATOMIC-MULTI | PASS | queue+slot+acquisition and dependency lease/graph publication are regression-tested. |
| ATOMIC-EXTERNAL | PASS | loader callback success is durably staged before local finalization. |
| ATOMIC-RECONCILE | PASS | pending artifact and Evicting state retain post-success/cleanup retry ownership. |
| PERSIST-SNAPSHOT | N/A | no snapshot/restore API. |
| PERSIST-VALIDATE | N/A | no restore input. |
| PERSIST-CANDIDATE | N/A | no restore candidate/live commit. |
| PERSIST-FAILURE | N/A | no restore operation. |
| PERSIST-CONTINUITY | N/A | acquisitions/generations are transient runtime identities; no persistence contract. |
| TEST-HAPPY | PASS | `TestRequestQueuesAndProcessLoadsPayload`, factory and dependency happy paths. |
| TEST-INVALID | PASS | invalid requests, handles, artifacts and self-dependency tests. |
| TEST-DUPLICATE | PASS | duplicate loader, acquisition/double release and dependency ownership cases. |
| TEST-STALE | PASS | stale handle/release and generation exhaustion cases. |
| TEST-EMPTY | PASS | empty queue/zero-item budget and missing queries. |
| TEST-BOUNDARY | PASS | acquisition/generation/refcount/size_t budgets and deep dependency depth. |
| TEST-WRONG-LIFECYCLE | PASS | frozen registry, Evicting acquisition and in-flight eviction restrictions. |
| TEST-CALLBACK-FAILURE | PASS | loader Result/exception and invalid-artifact regressions. |
| TEST-REGRESSION | PASS | G3-RES-001..004 permanent regressions all execute in the module suite. |

## Block C exit status

Local evidence supports the code-level C exit conditions except the explicitly Windows-only MSVC qualification. Assets no longer overrides process-global allocation operators. G3-RES-001..004 and G3-ASSET-001..003 remain covered. Resources ownership/retry regressions pass without lost queue/job/dependency ownership in the available local builds.
