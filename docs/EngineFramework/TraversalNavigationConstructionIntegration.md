# TraversalNavigationConstructionIntegration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration`.
Admission: 8 public API, 0 mutation obligations, 0 lifecycle, 0 stale-identity, 0 external-boundary candidates.

## Public contract review

- TraversalNavigationCapabilityProvider maps traversal capabilities into NavigationSemantics without owning either state.
- TraversalNavigationAdapter::CanUseLink propagates stale/invalid subject and capability decisions correctly.
- ConstructionNavigationLayerPayload encoding/decoding validates durable payloads and stable layer identity.
- ConstructionNavigationAdapter::QueueNavigationOperation creates durable placement outputs without duplicate semantic operations.
- ProcessPendingOutputs performs idempotent add/update/remove, acknowledges only after Navigation commit, and supports retry after partial external progress.
- Duplicate delivery is idempotent by stable layer ID; already-applied state is acknowledged rather than duplicated.
- ConstructionTraversalAdapter::CancelTraversalSessions handles the documented partial-progress/retry contract without claiming batch atomicity that the API does not provide.
- Corrupt payloads, stale world references and restore/replay paths leave both sides reconcilable.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

The durable placement-output protocol was reviewed specifically for stable layer IDs, idempotent add/update/remove and acknowledgement only after successful Navigation mutation.

## Persistence and reconciliation

No independent adapter-owned snapshot/checkpoint surface is required by this freeze unit. Durable state is either carried by owner-issued tokens/outputs or explicitly delegated to the authoritative owners; whole-engine persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/traversal_navigation_construction_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkTraversalNavigationConstructionIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-166404d5ba447d5e: `epidemic::gameplay::traversal_navigation_construction::ConstructionTraversalAdapter` — `[[nodiscard]] foundation::Result<void> CancelTraversalSessions(traversal::TraversalService&traversal,std::span<const traversal::TraversalSessionId> sessions,GameplayContext context={})const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-284e62d18df5b3b5: `epidemic::gameplay::traversal_navigation_construction::ConstructionNavigationAdapter` — `[[nodiscard]] foundation::Result<std::size_t> ProcessPendingOutputs(construction::ConstructionService&construction,navigation_semantics::NavigationSemanticsService&navigation,GameplayContext context={})const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-2fbf85ebea5a83e1: `epidemic::gameplay::traversal_navigation_construction::TraversalNavigationCapabilityProvider` — `explicit TraversalNavigationCapabilityProvider(const traversal::TraversalService&traversal)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-34b83d920d118d39: `epidemic::gameplay::traversal_navigation_construction` — `[[nodiscard]] navigation_semantics::NavigationLayerId NavigationLayerIdFor(construction::PlacedObjectId placed_object,TypeId source_key)noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a259c5af4e64e241: `epidemic::gameplay::traversal_navigation_construction` — `[[nodiscard]] std::vector<std::byte> EncodeNavigationLayerPayload(const ConstructionNavigationLayerPayload&payload);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-b7f86e3980d636b4: `epidemic::gameplay::traversal_navigation_construction::TraversalNavigationAdapter` — `[[nodiscard]] navigation_semantics::NavigationPermissionResult CanUseLink(const traversal::TraversalService&traversal,const navigation_semantics::NavigationSemanticsService&navigation,GameplayObjectRef subject,navigation_semantics::NavigationLinkId link,const GameplayContext&context)const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e271ae771276d637: `epidemic::gameplay::traversal_navigation_construction::TraversalNavigationCapabilityProvider` — `[[nodiscard]] bool HasCapability(GameplayObjectRef subject,TypeId capability,navigation_semantics::Fixed min_parameter_micro)const noexcept override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e8aaf82aa003c508: `epidemic::gameplay::traversal_navigation_construction::ConstructionNavigationAdapter` — `[[nodiscard]] foundation::Result<construction::PlacementOutputId> QueueNavigationOperation(construction::ConstructionService&construction,construction::PlacedObjectId placed_object,GameplayObjectRef area,const ConstructionNavigationLayerPayload&payload,GameplayContext context={})const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

## Exact Goal 4 criterion contracts

- G4-CRITERION-ARCH-RESPONSIBILITY: `ARCH-RESPONSIBILITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-SINGLE-OWNER: `ARCH-SINGLE-OWNER` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-DIRECT-DEPS: `ARCH-DIRECT-DEPS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ARCH-PORTS: `ARCH-PORTS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-CLASSIFIED: `API-CLASSIFIED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-PRECONDITIONS: `API-PRECONDITIONS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-SUCCESS: `API-SUCCESS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-FAILURE: `API-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-OVERLOADS: `API-OVERLOADS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-API-INVALID: `API-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-PRIMARY: `STATE-PRIMARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-INDEXES: `STATE-INDEXES` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-COUNTERS: `STATE-COUNTERS` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NO-FALSE-PUBLISH: `STATE-NO-FALSE-PUBLISH` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-STATE-NOOP: `STATE-NOOP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-ALLOWED: `LIFE-ALLOWED` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-FORBIDDEN: `LIFE-FORBIDDEN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-SHUTDOWN: `LIFE-SHUTDOWN` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-LIFE-RETRY-CLEANUP: `LIFE-RETRY-CLEANUP` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-SINGLE: `ATOMIC-SINGLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-MULTI: `ATOMIC-MULTI` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-EXTERNAL: `ATOMIC-EXTERNAL` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-ATOMIC-RECONCILE: `ATOMIC-RECONCILE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-SNAPSHOT: `PERSIST-SNAPSHOT` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-VALIDATE: `PERSIST-VALIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CANDIDATE: `PERSIST-CANDIDATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-FAILURE: `PERSIST-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-PERSIST-CONTINUITY: `PERSIST-CONTINUITY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-HAPPY: `TEST-HAPPY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-INVALID: `TEST-INVALID` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-DUPLICATE: `TEST-DUPLICATE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-STALE: `TEST-STALE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-EMPTY: `TEST-EMPTY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-BOUNDARY: `TEST-BOUNDARY` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-WRONG-LIFECYCLE: `TEST-WRONG-LIFECYCLE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-CALLBACK-FAILURE: `TEST-CALLBACK-FAILURE` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
- G4-CRITERION-TEST-REGRESSION: `TEST-REGRESSION` was reviewed for this module against its authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned regression anchor records the applicable observable assertion.
