# InteractionEffectsIntegration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/InteractionEffectsIntegration`.
Admission: 11 public API, 24 mutation obligations, 1 lifecycle, 5 stale-identity, 0 external-boundary candidates.

## Public contract review

- Interaction completion -> Effects exactly once.
- Effect prepare failure.
- Effect commit failure.
- Reconciliation state.
- Duplicate interaction completion.
- Restore delivery records.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

## Persistence and reconciliation

The module exposes local snapshot/checkpoint state. Capture/restore validation and continuation are covered by the owned integration test; whole-engine ordered persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_effects_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkInteractionEffectsIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-11c64ad1558d1a09: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `InteractionEffectExecutor(effects::EffectService&effects,interaction::InteractionTypeId interaction_type)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-178673e2b9752c4b: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `void PruneTerminalExecution(interaction::InteractionExecutionId execution)noexcept;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-1f6f4bad1f02afcb: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<InteractionEffectReconciliationStatus> ReconcileDelivery(const InteractionEffectReconciliationRequest&request);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-21d9c98593030481: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] const InteractionEffectDeliveryRecord*FindDelivery(interaction::InteractionExecutionId execution,std::uint32_t effect_index)const noexcept;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-2cd9c62bdcd7df6a: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] InteractionEffectsSnapshot CaptureSnapshot()const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-47047527fb89d2c2: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<void> RestoreSnapshot(InteractionEffectsSnapshot snapshot);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-68070653cceff53b: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] bool IsFrozen()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a05be62f5dc08537: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<void> Commit(const interaction::InteractionPlan&plan,interaction::InteractionExecutionId execution)noexcept override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a3a1508ba6742bca: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<void> Freeze(const interaction::InteractionService&interactions);`; classification `LIFECYCLE`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-cb7bcfcfbec3a859: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<void> Validate(const interaction::InteractionPlan&plan)const override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ea98cc20b207b09e: `epidemic::gameplay::interaction_effects_integration::InteractionEffectExecutor` — `[[nodiscard]] foundation::Result<void> RegisterEffect(effects::EffectDefinitionId definition);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

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
