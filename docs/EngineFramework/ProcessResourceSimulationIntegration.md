# ProcessResourceSimulationIntegration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration`.
Admission: 22 public API, 32 mutation obligations, 0 lifecycle, 8 stale-identity, 1 external-boundary candidates.

## Public contract review

- Processes <-> resource input reservation.
- Output prepare/commit.
- Provider failure.
- Resource quantity rollback.
- Simulation layer proposal/application.
- Duplicate simulation execution.
- Restore checkpoint/reconciliation.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

B08-PRSINT-001 was found during the block audit: provider-token storage is staged before Resources accepts a reservation, eliminating the post-accept allocation gap.

## Persistence and reconciliation

No independent adapter-owned snapshot/checkpoint surface is required by this freeze unit. Durable state is either carried by owner-issued tokens/outputs or explicitly delegated to the authoritative owners; whole-engine persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/process_resource_simulation_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkProcessResourceSimulationIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-06ed642941e59f2f: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `[[nodiscard]] foundation::Result<processes::PreparedProcessOutput> Prepare(const processes::ProcessOutputDefinition&output,const processes::ProcessInstance&instance,GameplayContext context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-0a1e0df2d562a4c8: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] foundation::Result<void> Validate(const processes::ProcessInputDefinition&input,const processes::StartProcessRequest&request,processes::ProcessInstanceId instance)override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-23fa1848831901c6: `epidemic::gameplay::integration` — `[[nodiscard]] std::optional<ProcessResourcePayload> DecodeProcessResourcePayload(const processes::RegisteredPayload&payload);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-26dd2eaa7a815ab9: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `explicit ResourceProcessInputProvider(resources::ResourcesProductionService&resources)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-33e27f88774e03f7: `epidemic::gameplay::integration::ProcessesSimulationLayer` — `[[nodiscard]] static constexpr simulation::SimulationLayerId StaticLayer()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-4c777398663c5f04: `epidemic::gameplay::integration` — `[[nodiscard]] processes::RegisteredPayload EncodeProcessResourcePayload(ProcessResourcePayload payload);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-57214928ac4f1127: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `explicit ResourceProcessOutputHandler(resources::ResourcesProductionService&resources)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-77b874ab572514d1: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `[[nodiscard]] static constexpr processes::ProcessOutputTypeId OutputType()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-782ffc63510efbd4: `epidemic::gameplay::integration` — `[[nodiscard]] processes::RegisteredPayload EncodeProcessResourceReservationPayload(ProcessResourceReservationPayload payload);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-817c48a0155a6e26: `epidemic::gameplay::integration` — `[[nodiscard]] std::optional<ProcessResourceReservationPayload> DecodeProcessResourceReservationPayload(const processes::RegisteredPayload&payload);`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-869a25f5bccba56e: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] static constexpr TypeId ReservationPayloadType()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a8dc4e0a74ed67b0: `epidemic::gameplay::integration::ProcessesSimulationLayer` — `[[nodiscard]] simulation::SimulationLayerId Layer()const noexcept override`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-acda318405ede45e: `epidemic::gameplay::integration::ProcessesSimulationLayer` — `[[nodiscard]] foundation::Result<void> Commit(const simulation::SimulationTask&task,const simulation::SimulationLayerSummary&summary)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ca479fc6903219de: `epidemic::gameplay::integration::ProcessesSimulationLayer` — `[[nodiscard]] foundation::Result<simulation::SimulationLayerSummary> Prepare(const simulation::SimulationTask&task)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-cbd2ba63ab794c67: `epidemic::gameplay::integration::ProcessesSimulationLayer` — `explicit ProcessesSimulationLayer(processes::ProcessesService&processes)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-d1f676c0bc39ca7b: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] static constexpr TypeId PayloadType()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-e7b6010fcebdec13: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `[[nodiscard]] bool Supports(processes::ProcessOutputTypeId type)const noexcept override`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ea0667c73e478042: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `[[nodiscard]] foundation::Result<void> Commit(const processes::PreparedProcessOutput&output,const processes::ProcessInstance&instance,GameplayContext context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f71c76cc144091d6: `epidemic::gameplay::integration::ResourceProcessOutputHandler` — `[[nodiscard]] foundation::Result<void> Cancel(const processes::PreparedProcessOutput&output,const processes::ProcessInstance&instance,GameplayContext context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-fd9e2e25eae6d788: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] foundation::Result<processes::ReservedProcessInput> Reserve(const processes::ProcessInputDefinition&input,const processes::StartProcessRequest&request,processes::ProcessInstanceId instance)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-fe5c85fe541900ff: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] foundation::Result<void> Consume(const processes::ReservedProcessInput&reservation,GameplayContext context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ff12b7b6b2e9c538: `epidemic::gameplay::integration::ResourceProcessInputProvider` — `[[nodiscard]] foundation::Result<void> Release(const processes::ReservedProcessInput&reservation,GameplayContext context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

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
