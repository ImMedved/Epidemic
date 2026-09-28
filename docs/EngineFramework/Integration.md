# Integration

Goal 4 block: `b08_integration_adapters`.
Freeze unit: `EngineFramework/IntegrationLayer/Integration`.
Admission: 34 public API, 68 mutation obligations, 1 lifecycle, 14 stale-identity, 2 external-boundary candidates.

## Public contract review

- FactsQueryAdapter query mapping.
- RuntimeTimeAdapter time projection.
- ScheduledTriggerDispatcher registration/freeze.
- Trigger handler failure.
- Delivery modes.
- Duplicate trigger prevention.
- Dispatcher checkpoint/save participant.
- TimeFactsAdapter exactly-once event projection.

## Failure atomicity and accepted-work review

Every local adapter mutation was reviewed for success, invalid input, retry/duplicate behavior, stale identities/cursors and publication ordering. Fallible local bookkeeping must complete before an externally owned side effect is accepted, or a durable reconciliation record must already exist.

B08-INT-001 was found during the block audit: completion bookkeeping is now capacity-staged before a ScheduledTrigger handler can accept work, so allocation failure cannot occur after the external callback has produced a side effect.

## Persistence and reconciliation

The module exposes local snapshot/checkpoint state. Capture/restore validation and continuation are covered by the owned integration test; whole-engine ordered persistence remains Goal 5.

## Test evidence

- Source: `EngineFramework/DevelopmentInfrastructure/Tests/core_integration_tests.cpp`.
- Target: `EpidemicGameFrameworkCoreIntegrationTests`.
- Reviewed paths include happy path, invalid/stale input, duplicate/retry behavior and local failure/reconciliation behavior applicable to this module.
- The B08 worker compiled and executed this target under C++23 with Clang warnings-as-errors in the available Linux environment. Required Windows MSVC Debug/Release qualification remains a serial-integration gate.

## Goal 4 result

`LOCAL_READY_CANDIDATE` for block-local evidence. This is not a whole-Framework `LOCAL_READY`, `SYSTEM_READY` or `FROZEN` claim.

<!-- goal4-exact-evidence -->

## Exact Goal 4 callable contracts

- G4-API-02f1a3b4a333182b: `epidemic::gameplay::integration::FindFactsQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-0e235e51ac234209: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `void CommitRestore(savegame::IRestoreStage&stage)noexcept override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-264a74b839daad80: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `explicit ScheduledTriggerDispatcherSaveParticipant(ScheduledTriggerDispatcher&dispatcher)noexcept`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-26518bbe6c02ae55: `epidemic::gameplay::integration::FactsQueryAdapter` — `FactsQueryAdapter(facts::GameplayFactsService&facts_service,queries::GameplayQueryService&query_service)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-2937979fc5fb80bc: `epidemic::gameplay::integration::RuntimeTimeAdapter` — `[[nodiscard]] foundation::Result<void> Synchronize();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-33bd4f6ab10bea4c: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `foundation::Result<void> Freeze();`; classification `LIFECYCLE`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-38b9f330102526ca: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<void> RegisterObserver(ScheduledTriggerHandlerId id,int priority,Handler handler);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-4b70003976e4741e: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] bool IsFrozen()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-57d0aa681fb5df0a: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<void> DeclareObserverOnlyAction(ActionTypeId action);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-5d0d69dddfe66b26: `epidemic::gameplay::integration::TimeFactsAdapter` — `[[nodiscard]] foundation::Result<EventTypeId> RegisterContracts();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-5f1f2962c0ba13f7: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] ScheduledTriggerDispatcherSnapshot CaptureSnapshot()const;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-658e68e868629496: `epidemic::gameplay::integration::TimeFactsAdapter` — `[[nodiscard]] foundation::Result<void> RegisterWithDispatcher(ScheduledTriggerDispatcher&dispatcher,int priority=0);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-6972fbf140ddf630: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] foundation::Result<void> ValidateSnapshot(const savegame::SaveSection&section,const savegame::RestoreContext&context)const override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-6a3359c45abe4531: `epidemic::gameplay::integration::FactsQueryAdapter` — `[[nodiscard]] bool IsRegistered()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-72877e568f898a2f: `epidemic::gameplay::integration::TimeFactsAdapter` — `[[nodiscard]] foundation::Result<std::uint64_t> ExpireTimedFacts(GameplayContext context);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-7a3ccd8f1d5554a1: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] std::size_t PendingCount()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-811fff9abfc5a36a: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] std::vector<savegame::SaveParticipantId> Dependencies()const override`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-8e141314f392e17a: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<void> RestoreSnapshot(ScheduledTriggerDispatcherSnapshot snapshot);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-8fdc47df51f2f3ef: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] foundation::Result<savegame::SaveSection> CaptureSnapshot(const savegame::SaveContext&context)const override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-97a19960ce1ecf80: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] foundation::Result<std::unique_ptr<savegame::IRestoreStage>> StageRestore(const savegame::SaveSection&section,const savegame::RestoreContext&context)override;`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a3713bf4c9c8b7c6: `epidemic::gameplay::integration::FactsQueryAdapter` — `[[nodiscard]] foundation::Result<void> RegisterProviders();`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-a7364951af7bbf87: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] savegame::SaveParticipantId Id()const noexcept override;`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-bd2f0144590154c8: `epidemic::gameplay::integration::TimeFactsAdapter` — `[[nodiscard]] foundation::Result<ScheduledTriggerDisposition> PublishTrigger(const time::ScheduledTrigger&trigger,const GameplayContext&context);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-c0966256e8a3e547: `epidemic::gameplay::integration::FindHistoryQuery` — `[[nodiscard]] static constexpr QueryTypeId Type()noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-c9264bc306dad337: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<ScheduledTriggerPumpReport> Pump(ClockId clock,GameplayContext context,time::SchedulerBudget budget={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-ccd56b0ff9a01765: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<void> RegisterActionHandler(ActionTypeId action,ScheduledTriggerHandlerId id,Handler handler);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-cd270b9399c6e453: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `ScheduledTriggerDispatcher(time::GameplayTimeService&time_service,ScheduledTriggerDispatcherPolicy policy={})`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-cd30a1730439e4c9: `epidemic::gameplay::integration::TimeFactsAdapter` — `[[nodiscard]] EventTypeId ScheduledDueEventType()const noexcept`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-d9a7cc0fe40717df: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] ScheduledTriggerPumpReport DispatchPending(std::uint64_t max_attempts=0);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-dece04b2fbada448: `epidemic::gameplay::integration::RuntimeTimeAdapter` — `RuntimeTimeAdapter(runtime::IGameClock&runtime_clock,time::GameplayTimeService&gameplay_time,ClockId clock)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-eb0a0d705381a21b: `epidemic::gameplay::integration::ScheduledTriggerDispatcherSaveParticipant` — `[[nodiscard]] savegame::SaveSchemaVersion SchemaVersion()const noexcept override`; classification `QUERY`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f42dc1299d3df18f: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<std::uint64_t> CollectDue(ClockId clock,GameplayContext context,time::SchedulerBudget budget={});`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-f81eab15a525ca61: `epidemic::gameplay::integration::ScheduledTriggerDispatcher` — `[[nodiscard]] foundation::Result<void> DeclareRequiresActionHandler(ActionTypeId action);`; classification `MUTATOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.
- G4-API-fccab4dfaf4e9d62: `epidemic::gameplay::integration::TimeFactsAdapter` — `TimeFactsAdapter(facts::GameplayFactsService&facts_service)`; classification `CONSTRUCTOR`. The callable follows the module's reviewed validation, success, no-op and failure-publication rules applicable to that classification.

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
