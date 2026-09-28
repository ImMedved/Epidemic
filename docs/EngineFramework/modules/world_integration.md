# WorldIntegration

Status: B07 Goal 4 review. Local integration contract only.

Responsibility: provides read/projection adapters between semantic World, Environment, Interaction and Entities state and shared Facts/Queries contracts. Linked gameplay owners remain authoritative; the adapter owns only its checkpoint/progress bookkeeping.

Dependencies: World, Environment, Interaction, Entities, Framework Facts/Queries and EngineBase Foundation. There is no direct Runtime/platform backend dependency.

Query contracts: world/environment/entity interaction adapters validate requested identities and map authoritative owner state into detached query results without creating duplicate semantic ownership.

Facts projection: pending World changes are published in deterministic owner-journal order. A source cursor advances only after the corresponding Facts publication succeeds, preserving accepted prefix progress and making failure retry-safe without replaying already committed records.

Checkpoint/reconciliation: checkpoint state records source revisions, cursor epochs/sequences and adapter progress. Restore validates schema/current owner revision. If an owner has been restored with a fresh journal epoch, the adapter normalizes to a reconciliation boundary only when authoritative revisions agree; stale or incompatible checkpoints are rejected.

Failure and atomicity: provider/publication failure leaves uncommitted source progress pending. Duplicate/stale change delivery is handled through journal cursor semantics rather than a second authoritative cache.

Persistence boundary: only adapter checkpoint/progress is persisted locally. Whole-engine ordered restore and RuntimeBridge rematerialization remain Goal 5 and later system goals.

Threading contract: synchronous caller-serialized adapter with no hidden worker.

Local invariants and tests: `world_integration_tests.cpp` covers query revision propagation, object/subject mapping, facts projection, failure-safe checkpoint progression, checkpoint roundtrip, stale/resynced journal epochs and publication after owner restore.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-073725a784f8293d: `epidemic::gameplay::world_integration::WorldQueryAdapter` — `WorldQueryAdapter(world::WorldService&w,environment::EnvironmentService&e,interaction::InteractionService&i,queries::GameplayQueryService&q)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-1ea60f37fd5ef0e2: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `[[nodiscard]] WorldFactsCheckpoint CaptureCheckpoint()const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-37b359a8777912da: `epidemic::gameplay::world_integration::EnvironmentSampleQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-38f5d3084359c090: `epidemic::gameplay::world_integration::EntityInteractionStateProvider` — `[[nodiscard]] bool IsMaterialized(GameplayObjectRef object)const override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-3e8e39662833fd63: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `WorldFactsAdapter(world::WorldService&w,environment::EnvironmentService&e,interaction::InteractionService&i,facts::GameplayFactsService&f)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-3f290d68339d4ede: `epidemic::gameplay::world_integration::EntityInteractionStateProvider` — `[[nodiscard]] Revision RevisionOf(GameplayObjectRef object)const override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-5dc7dbd4945a2cdc: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `void ResetCursorsToLatest()noexcept;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-62d00f34070a80ff: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `[[nodiscard]] foundation::Result<std::uint64_t> PublishPending(GameplayContext context={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-79ddb77e09c140b0: `epidemic::gameplay::world_integration::EntityInteractionStateProvider` — `explicit EntityInteractionStateProvider(const entities::EntityService&e)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-7f781d18a1bdbf41: `epidemic::gameplay::world_integration::AreasAtPositionQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-8c9767cd47637ba9: `epidemic::gameplay::world_integration::AlterationsInAreaQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-95b6c557e32054ce: `epidemic::gameplay::world_integration::WorldQueryAdapter` — `[[nodiscard]] foundation::Result<void> RegisterProviders();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-c5c38e55799b70a8: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `[[nodiscard]] foundation::Result<void> RegisterContracts();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e10ef9078e53ca96: `epidemic::gameplay::world_integration` — `[[nodiscard]] constexpr GameplayObjectRef GlobalWorldScope()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ec659bc1ed298345: `epidemic::gameplay::world_integration::ActiveInteractionsQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f6adcc4df011e574: `epidemic::gameplay::world_integration::WorldFactsAdapter` — `[[nodiscard]] foundation::Result<void> RestoreCheckpoint(WorldFactsCheckpoint checkpoint);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

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
