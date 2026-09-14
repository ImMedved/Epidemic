# Milestone 2 full audit, updated 11-09-2026

## Verdict

**All confirmed implementation blockers M2-01 through M2-06 are fixed. The Debug build and all 88 tests pass.**

Strict Milestone 2 acceptance remains `UNPROVEN` only for M2-07: the new allocator sweeps cover every Part 2/3 snapshot owner, SaveGame staging, and critical World/Abilities/AI/Time mutations, but the requirement literally asks for an allocation sweep of every public mutation that touches multiple allocating containers or a journal. That complete per-API matrix has not yet been implemented.

Baseline: branch `dev`, HEAD `c15ea6b5f32c3be5a7fcdc2213efb1e82bae4cb8` plus the current working-tree changes.

## Verification

| Check | Result |
|---|---:|
| Full Debug build | `PASS` |
| Full CTest run | `88/88 PASS` |
| CTest wall time (`-j 4`) | 152.34 s |
| `EpidemicEngineLoadTruthTests` | `PASS`, 7.79 s |
| `EpidemicEngineSmokeTruthTests` | `PASS`, 0.48 s |
| Former timeout suites: Foundation, Queries, Facts, Time, Abilities, AI | `6/6 PASS` |

Command:

```powershell
ctest --test-dir build/framework-check -C Debug --output-on-failure --timeout 300 -j 4
```

## Findings

### M2-01 [RESOLVED] World journal append atomicity

`AppendWorldChange` appends to staged journal storage before advancing sequence metadata. World mutations publish journal and cursor with no-throw swaps only after all fallible work succeeds.

Location: `EngineFramework/GameplayWorldStateOwners/World/src/world.cpp:28`.

Regression: `EngineFramework/DevelopmentInfrastructure/Tests/world_tests.cpp:215`.

### M2-02 [RESOLVED] World state/index/journal atomicity

Direct feature and placement mutations stage their journal changes; placement insertion no longer copies the complete placement map. Transactions build replacement alteration state and indexes, then commit by swap. The transaction owns a staged alteration-ID generator, so cancelled or failed transactions do not consume IDs.

Locations: `world.cpp:535`, `world.cpp:686`, `world.cpp:861`, `world.h` (`WorldTransaction::staged_alteration_ids_`).

Regression: `world_tests.cpp:230` and the transaction ID/state checks in the same suite.

### M2-03 [RESOLVED] World restore destroys old state before index rebuild

`RestoreSnapshot` validates and constructs primary state, spatial indexes, large-alteration index, journal, and generator state off-state. Live graded/free state is replaced only after all allocations succeed.

Location: `world.cpp:1102`.

### M2-04 [RESOLVED] World accepts invalid enum values

Explicit validators reject values outside `WorldAlterationState` and `WorldAlterationPersistence` at create/update and restore boundaries before state mutation.

Locations: `world.cpp:20`, `world.cpp:24`, `world.cpp:800`.

Regression: invalid mutation and corrupted-snapshot cases in `world_tests.cpp`.

### M2-05 [RESOLVED] Time and AI load paths are quadratic

`GameplayTimeService::Schedule` now inserts only the new schedule/index nodes and rolls back on failure. `AIService::RegisterAgent` and `SetNextThink` use local insertion/rollback. `AIService::Think` stages one agent and performs at most one fallible index insertion before a no-throw commit; it no longer copies all agent/index maps.

Locations: `gameplay_time.cpp:441`; `ai.cpp:306`, `ai.cpp:388`, `ai.cpp:616`.

Evidence: load truth reduced from a 240 s timeout to 7.79 s.

### M2-06 [RESOLVED] Generator restore helper terminates while building an error

`RestoreMonotonicIdGeneratorSnapshot` is no longer `noexcept`; allocation failure while constructing an invalid-snapshot error can propagate without terminating or mutating the generator.

Location: `EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/id_generator.h:188`.

Regression: `foundation_tests.cpp` invalid-snapshot allocation case.

### M2-07 [PARTIAL / GATE] Complete allocation-failure matrix

Added the common fault injector to all Part 2/3 snapshot suites. Each restore sweep moves a prebuilt snapshot into the API, injects failures inside the implementation, and checks that revision/identity state is unchanged. SaveGame additionally checks that failed staging never calls participant commit and always releases its barrier.

Coverage anchors:

- Part 2: `effects_tests.cpp:246`, `encounters_tests.cpp:122`, `entities_tests.cpp:166`, `environment_gameplay_tests.cpp:284`, `equipment_tests.cpp:207`, `interaction_gameplay_tests.cpp:122`, `items_inventory_tests.cpp:227`, `knowledge_memory_tests.cpp:355`, `loot_tests.cpp:292`, `materials_tests.cpp:145`, `narrative_tests.cpp:350`, `navigation_semantics_tests.cpp:262`, `needs_life_tests.cpp:256`.
- Part 3: `ownership_tests.cpp:252`, `perception_tests.cpp:729`, `population_tests.cpp:188`, `processes_tests.cpp:498`, `progression_tests.cpp:192`, `resources_production_tests.cpp:156`, `roles_jobs_tests.cpp:210`, `save_game_tests.cpp:397`, `simulation_tests.cpp:250`, `society_tests.cpp:206`, `traversal_tests.cpp:230`, plus World direct/transaction tests.

Still required for strict closure: enumerate every public multi-container/journal mutation and add a bounded allocation-index sweep with complete state, index, generator, revision, journal, cursor, and external-callback postconditions. Passing 88/88 does not by itself prove that larger matrix.

## Status by area

| Area / major | Status |
|---|---:|
| EngineBase (all local modules) | `PASS` |
| EngineRuntime (all local modules) | `PASS` |
| Foundation | `PASS` |
| Queries | `PASS` |
| Facts | `PASS` |
| SupportRandom | `PASS` |
| Time | `PASS` |
| Abilities | `PASS` |
| AI | `PASS` |
| Combat | `PASS` |
| Conditions | `PASS` |
| Construction | `PASS` |
| Crime | `PASS` |
| Dialogue | `PASS` |
| Economy | `PASS` |
| Effects | `PASS` |
| Encounters | `UNPROVEN` |
| Entities | `UNPROVEN` |
| Environment | `UNPROVEN` |
| Equipment | `UNPROVEN` |
| Interaction | `UNPROVEN` |
| ItemsInventory | `UNPROVEN` |
| Knowledge | `UNPROVEN` |
| Loot | `UNPROVEN` |
| Materials | `UNPROVEN` |
| Narrative | `UNPROVEN` |
| NavigationSemantics | `UNPROVEN` |
| NeedsLife | `UNPROVEN` |
| Ownership | `UNPROVEN` |
| Perception | `UNPROVEN` |
| Population | `UNPROVEN` |
| Processes | `UNPROVEN` |
| Progression | `UNPROVEN` |
| ResourcesProduction | `UNPROVEN` |
| RolesJobs | `UNPROVEN` |
| SaveGame | `UNPROVEN` |
| Simulation | `UNPROVEN` |
| Society | `UNPROVEN` |
| Traversal | `UNPROVEN` |
| World | `UNPROVEN` |

`UNPROVEN` here means the local suite passes and the restore allocation contract is covered, but the strict per-public-mutation allocation matrix is incomplete. It is not a confirmed implementation defect.

## План подтверждения Milestone 2

### 1. Зафиксировать полную матрицу mutation API

Создать в этом файле таблицу всех публичных mutation API для EngineBase, EngineRuntime и 38 Framework majors. Для каждого API указать:

- модуль, класс, метод и точную ссылку на реализацию;
- изменяемые authoritative containers, secondary indexes, ID generators, revision и journal;
- внешние callbacks и операции `Prepare` / `Commit` / `Cancel` / `Rollback`;
- возможные точки отказа: validation, allocation, callback exception/failure, overflow, stale ID, invalid lifecycle;
- требуемые postconditions успеха и отказа;
- существующий regression-тест либо статус `MISSING`;
- итог `PASS`, `FAIL` или `UNPROVEN`.

Метод нельзя помечать `PASS`, пока не проверены все его fallible commit boundaries. Definition registration до `Freeze()` включать в матрицу, но отделять от runtime state mutations.

**Выполнено 2026-09-13.** Freeze snapshot матрицы вынесен в `docs/milestone2_mutation_api_matrix.md` и воспроизводится скриптом `docs/milestone2_mutation_api_matrix.py`. Генератор сканирует публичные headers EngineBase, EngineRuntime и EngineFramework, классифицирует mutation-like entrypoints по verb/kind, фиксирует header anchor, implementation/source contract anchor, state/failure contract, required postcondition и test anchor. Текущий snapshot содержит 824 публичных mutation-like rows.

Таблица ниже остаётся человекочитаемым grouped overview для планирования, а freeze authority для пункта 1 теперь - generated per-method snapshot. Это не переводит весь Milestone 2 в `PASS`: matrix freeze означает, что поверхность API больше не неявная и новые изменения должны обновлять generator snapshot.

Общие правила чтения матрицы:

- `Definition` означает registration/setup API до `Freeze()` / `Seal()` / завершения bootstrap.
- `Runtime` означает API, меняющий рабочее authoritative state после запуска.
- `Restore` означает загрузку snapshot/candidate state с требованием temporary-state validation и atomic replacement.
- В `State touched` перечислены классы состояния, которые обязаны входить в fingerprint отказа: primary records, secondary indexes, generators, revisions, journals/cursors, queues и внешние participants.
- `Required failure postcondition` по умолчанию: validation/allocation/callback/overflow/stale/lifecycle failure не меняет observable pre-state, не публикует revision/journal/cursor и не оставляет внешнюю подготовку без rollback/reconciliation.

#### Mutation API matrix, EngineBase и EngineRuntime

| Area | API family | Source / implementation anchor | State touched | Failure boundaries | Existing coverage | Result |
|---|---|---|---|---|---|---:|
| EngineBase/Core | `Application::{Bootstrap,Initialize,Tick,Run,Shutdown,RequestStop,SetFrameLimit,AddFramePhaseHandler,ScheduleMainThreadTask}` | `EngineBase/Core/include/Epidemic/Core/application.h`; `EngineBase/Core/src/application.cpp` | lifecycle state, frame context, phase handler vectors, stop flag, service container, module registry, main-thread queue | invalid lifecycle, missing services, allocation in handlers/tasks, module callback exceptions | `EngineBase/Tests/Unit/core_unit_tests.cpp` | `PASS_BASE_SUITE` |
| EngineBase/Core | `ModuleRegistry::{Register,BootstrapAll,InitializeAll,TickAll,ShutdownAll}` | `EngineBase/Core/include/Epidemic/Core/module_registry.h`; `EngineBase/Core/src/module_registry.cpp` | module records, execution plan, per-module lifecycle | duplicate/missing deps, allocation, module exceptions, shutdown ordering | `core_unit_tests.cpp` | `PASS_BASE_SUITE` |
| EngineBase/Core | `ServiceContainer::{Register,RegisterInstance,Seal}` and `IConfiguration::{SetString,SetInt,SetBool}` | `service_container.h`, `configuration.h`, `basic_configuration.h`; `basic_configuration.cpp` | service map, sealed flag, config maps | duplicate service, sealed registry, allocation, invalid shared_ptr | `core_unit_tests.cpp` | `PASS_BASE_SUITE` |
| EngineBase/Core | `EventBus::{Subscribe,PublishSync}` / dispatcher / scheduler: `Post`, `Drain`, `Schedule`, `Wait*`, `Join`, `Shutdown` | `event_bus.h`, `main_thread_dispatcher.h`, `task_scheduler.h`; matching `src/*.cpp` | subscriber lists, task queues, worker state, task groups | callback exception, self-wait, queue allocation, shutdown race | `core_unit_tests.cpp` | `PASS_BASE_SUITE` |
| EngineBase/Diagnostics | `DiagnosticsCounters::Set`, profiling scopes, logger writes | `EngineBase/Diagnostics/include/Epidemic/Diagnostics/*.h`; `src/*.cpp` | counters, profiler state, logger sink | invalid counter, noexcept cleanup, formatting/allocation in log payloads | `diagnostics_unit_tests.cpp` | `PASS_BASE_SUITE` |
| EngineBase/Input | `InputSystem::{QueuePlatformEvent,QueuePlatformEvents,PublishSnapshot}` | `EngineBase/Input/include/Epidemic/Input/input_system.h`; `EngineBase/Input/src/input_system.cpp` | pending event queue, keyboard/mouse state, snapshots | invalid event, allocation in queue/snapshot, transient clear ordering | Base unit/integration tests | `PASS_BASE_SUITE` |
| EngineBase/Memory | `MemoryTracker::{SetTrackingEnabled,SetBudget}` plus allocation accounting | `EngineBase/Memory/include/Epidemic/Memory/memory_tracker.h`; Memory sources | tracker maps, budgets, counters | overflow, disabled tracking, unknown tag | Base unit tests | `PASS_BASE_SUITE` |
| EngineBase/Platform/RHI | `CreateWindow`, `Close`, `PumpEvents`, `LoadDynamicLibrary`, `CreateCommandContext`, `CreateSwapChain`, `Clear`, `Present`, `Resize` | `EngineBase/Platform/include`, `EngineBase/RHI/include`, `EngineBase/RHI_D3D11/src/d3d11_rhi_device.cpp` | OS handles, window registry, device/swap-chain/context state | backend failure, invalid dimensions, owner-thread lifecycle, resize recovery | platform/RHI integration tests | `PASS_BASE_SUITE` |
| EngineRuntime/Animation | `RegisterSkeleton`, `RegisterClip`, `Freeze`, `CreateAnimatorHandle`, `DestroyAnimator`, `SetLod`, `Tick`, pose/event publish/clear | `EngineRuntime/Animation/include/Epidemic/Runtime/Animation/animation_runtime.h`; `src/animation_runtime_impl.cpp` | registries, animator map, pose buffers, event queue, revisions | registry freeze, stale handle, allocation, resource/source failure | `EngineRuntime/Animation/tests/animation_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Assets | `RegisterAsset`, `Seal` | `EngineRuntime/Assets/include/Epidemic/Runtime/Assets/asset_catalog_writer.h`; `src/in_memory_asset_catalog.cpp` | metadata catalog, type/location/dependency indexes, sealed flag | duplicate asset, invalid metadata, allocation, sealed registry | `EngineRuntime/Assets/tests/assets_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Audio | `RegisterSound`, backend `Initialize/Create/Destroy/Set/Update`, runtime `CreateEmitterHandle/DestroyEmitter/Tick/Shutdown`, listeners, mixer, one-shot queue | `EngineRuntime/Audio/include/Epidemic/Runtime/Audio/audio_runtime.h`; `src/audio_runtime_impl.cpp` | sound registry, emitters, backend voices/listeners, mixer groups, event queue | backend prepare/commit failure, rollback, stale handles, shutdown lifecycle | `EngineRuntime/Audio/tests/audio_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Environment | `SetWeather`, `SetSeason`, `SetClimateProfile`, `SetSurfaceState`, `ApplyUpdate`, `Update`, `SetUpdatePolicy`, `FreezeRegistration` | `EngineRuntime/Environment/include/Epidemic/Runtime/Environment/environment_runtime.h`; `src/environment_runtime_impl.cpp` | region/weather/season/climate/surface maps, projection state, policy pointer | invalid region, allocation, policy callback failure, freeze lifecycle | `EngineRuntime/Environment/tests/environment_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Navigation | `RegisterTile`, `MarkTileDirty`, `RequestPathHandle`, `CancelPath`, `ReleasePathResult`, `Tick` | `EngineRuntime/Navigation/include/Epidemic/Runtime/Navigation/navigation_runtime.h`; `src/navigation_runtime_impl.cpp` | tile registry, dirty set, query map, result cache, generators | stale query, allocation, invalid tile, budget boundary | `EngineRuntime/Navigation/tests/navigation_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Persistence | `OpenTransaction`, transaction `Upsert/Delete/Update/Remove/Commit/Rollback`, backend `Load/CommitSnapshot` | `EngineRuntime/Persistence/include/Epidemic/Runtime/Persistence/*.h`; `src/in_memory_persistence_support.cpp` | object store, tombstones, lazy rules, zone overrides, revision, durability backend | base revision mismatch, allocation, backend failure, commit boundary | `EngineRuntime/Persistence/tests/persistence_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Physics | shape/body registry `Register/Unregister/Create/Destroy/ApplyImpulse/Tick/Shutdown`, backend shape/body lifecycle, transform sink writes | `EngineRuntime/Physics/include/Epidemic/Runtime/Physics/physics_scene.h`; `src/physics_runtime_impl.cpp` | shape/body maps, backend handles, transform outputs, events | backend prepare/commit failure, stale handle, allocation, shutdown lifecycle | `EngineRuntime/Physics/tests/physics_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Renderer | proxy/resource/view registration, `SubmitProxy`, `ReleasePayloads`, `Render`, `Present`, `Shutdown` | `EngineRuntime/Renderer/include/Epidemic/Runtime/Renderer/*.h`; `src/renderer_runtime_impl.cpp` | render proxies, views, resource bridge handles, command queue, lifecycle | backend/resource failure, stale proxy, allocation, terminal shutdown | `EngineRuntime/Renderer/tests/renderer_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Resources | loader registry `Register/Freeze`, dependency graph/resource manager `Request/Load/Unload/Release/Cancel` | `EngineRuntime/Resources/include/Epidemic/Runtime/Resources/*.h`; `src/resource_manager.cpp` | loader registry, resource records, dependency graph, request queues, handles | loader failure, allocation, cycles, stale handles, freeze lifecycle | `EngineRuntime/Resources/tests/resources_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/RuntimeFoundation | checked ID allocators and numeric validation helpers | `EngineRuntime/RuntimeFoundation/include/Epidemic/Runtime/Foundation/*.h` | generator next/generation state | overflow, stale ID, invalid restore | `RuntimeFoundation/tests/runtime_foundation_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Scene | node/transform registry, spatial index updates, scene mutation and restore | `EngineRuntime/Scene/include/Epidemic/Runtime/Scene/*.h`; `src/scene_runtime_impl.cpp` | node map, transform registry, spatial indexes, revisions | invalid bounds, stale node, allocation, restore replacement | `EngineRuntime/Scene/tests/scene_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Serialization | serializer/migration registry `Register/Freeze`, archive writer `Write*`, migration execution | `EngineRuntime/Serialization/include/Epidemic/Runtime/Serialization/*.h`; `src/*.cpp` | registries, archive object tree, serialized document, migration chain | type mismatch, unsupported version, allocation, strong-guarantee rejection | `EngineRuntime/Serialization/tests/serialization_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Simulation | `SubmitJob`, `CancelJob`, `SetBudget`, `Tick`, `ProcessMainThreadCommits`, `SetAttention`, `Publish`, `CommitNext`, `Schedule`, `Cancel`, `ExecuteDueWithinBudget`, expiry | `EngineRuntime/Simulation/include/Epidemic/Runtime/Simulation/simulation_runtime.h`; `src/simulation_runtime_impl.cpp` | job map, queues, attention maps, proposal batches, task scheduler, event retention | executor failure, allocation, stale handles, budget and retention boundaries | `EngineRuntime/Simulation/tests/simulation_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Streaming | residency/priority/source registration, stream request scheduling, publish/commit, eviction/expiry | `EngineRuntime/Streaming/include/Epidemic/Runtime/Streaming/*.h`; `src/streaming_runtime_impl.cpp` | residency records, source maps, priority indexes, pending queue, budget state | source failure, allocation, stale region/object, budget boundary | `EngineRuntime/Streaming/tests/streaming_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/Time | calendar/time runtime mutations: tick/advance, scale updates, schedule/expiry/restore | `EngineRuntime/Time/include/Epidemic/Runtime/Time/time_runtime.h`; `src/time_runtime_impl.cpp` | time state, schedule queues, event buffers, revisions | overflow, invalid scale, allocation, stale schedule | `EngineRuntime/Time/tests/time_tests.cpp` | `PASS_RUNTIME_SUITE` |
| EngineRuntime/World | definitions `Register*`/`Freeze`, object `Create/Destroy/SetPlacement/SetResidency/Materialize/Demote`, chunk state, command `Apply` | `EngineRuntime/World/include/Epidemic/Runtime/World/world_services.h`; `src/world_runtime_impl.cpp` | region/chunk definitions, object records, placement/residency indexes, generators, command results | invalid enum/region, stale object, allocation, materialization demotion token | `EngineRuntime/World/tests/world_tests.cpp` | `PASS_RUNTIME_SUITE` |

#### Mutation API matrix, EngineFramework BaseInfrastructure

| Area | API family | Source / implementation anchor | State touched | Failure boundaries | Existing coverage | Result |
|---|---|---|---|---|---|---:|
| Foundation | ID/type registries, monotonic generators, restore helper | `EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/*.h`; `src/gameplay_foundation.cpp` | type maps, generator next/generation, registry frozen flags | collision, overflow, invalid snapshot, allocation while building error | `foundation_tests.cpp` | `PASS_FRAMEWORK_SUITE` |
| Facts | event/fact registration, codec/subscriber registration, `Publish`, `SubmitBatch`, `Commit`, `ExpireDueFacts`, snapshot/persistence restore | `EngineFramework/BaseInfrastructure/Facts/include/Epidemic/GameFramework/Facts/gameplay_facts.h`; `src/gameplay_facts.cpp` | event history, fact maps, type/codec/subscriber registries, revisions, recent history | registry freeze, callback exception, allocation, order overflow, restore validation | `facts_tests.cpp` | `PASS_FRAMEWORK_SUITE` |
| Queries | provider/snapshot provider registration, coordinator set, `Freeze`, snapshot acquire, query `Execute` | `EngineFramework/BaseInfrastructure/Queries/include/Epidemic/GameFramework/Queries/gameplay_queries.h`; `src/gameplay_queries.cpp` | provider maps, snapshot map, coordinator pointer, frozen flag | registry freeze, provider exception, type mismatch, allocation | `queries_tests.cpp` | `PASS_FRAMEWORK_SUITE` |
| SupportRandom | deterministic random stream state mutation | `EngineFramework/BaseInfrastructure/SupportRandom/include/Epidemic/GameFramework/SupportRandom/deterministic_random.h`; `src/deterministic_random.cpp` | RNG seed/state | invalid range, deterministic replay drift | `random_support_tests.cpp` | `PASS_FRAMEWORK_SUITE` |
| Time | `RegisterAction`, `Freeze`, `Schedule`, `Cancel`, `Acknowledge`, `AdvanceTo/By`, `ProcessDue`, `RestoreSnapshot` | `EngineFramework/BaseInfrastructure/Time/include/Epidemic/GameFramework/Time/gameplay_time.h`; `src/gameplay_time.cpp` | action registry, schedule map, due indexes, clock, journal/cursor, revisions | stale schedule, overflow, allocation, callback dispatch failure, restore validation | `time_tests.cpp` | `PASS_FRAMEWORK_SUITE` |

#### Mutation API matrix, EngineFramework gameplay majors

| Major | API family | Source / implementation anchor | State touched | Failure boundaries | Existing coverage | Result |
|---|---|---|---|---|---|---:|
| Abilities | definition/resource registration, activation/execution lifecycle, cancel/acknowledge, resource provider reserve/commit/release, restore | `EngineFramework/GameplayWorldStateOwners/Abilities/include/Epidemic/GameFramework/Abilities/abilities.h`; `src/abilities.cpp` | definitions, cooldowns, executions, resource reservations, journal/cursor, revisions | provider failure, allocation, stale handles, overflow, invalid lifecycle | `abilities_tests.cpp` | `UNPROVEN` |
| AI | agent/behavior registration, `RegisterAgent`, `SetNextThink`, `Think`, intent execution/acknowledge, restore | `EngineFramework/GameplayWorldStateOwners/AI/include/Epidemic/GameFramework/AI/ai.h`; `EngineFramework/GameplayWorldStateOwners/AI/src/ai.cpp` | agent map, schedule indexes, intent queues, generator, journal/cursor, revisions | callback/provider failure, allocation, stale agent, schedule overflow | `ai_tests.cpp` | `UNPROVEN` |
| Combat | combatant/stat/effect registration, attack/damage lifecycle, defeat/death journal, restore | `EngineFramework/GameplayWorldStateOwners/Combat/include/Epidemic/GameFramework/Combat/combat.h`; `EngineFramework/GameplayWorldStateOwners/Combat/src/combat.cpp` | combatant records, health/state indexes, pending damage, journal/cursor, revisions | invalid combatant, callback/effect failure, allocation, overflow | `combat_tests.cpp` | `UNPROVEN` |
| Conditions | condition definition registration, apply/stack/tick/expire/remove, restore | `EngineFramework/GameplayWorldStateOwners/Conditions/include/Epidemic/GameFramework/Conditions/conditions.h`; `EngineFramework/GameplayWorldStateOwners/Conditions/src/conditions.cpp` | active condition records, target/type indexes, expiry queues, journal/cursor, revisions | stale target, invalid stack, allocation, expiry overflow | `conditions_tests.cpp` | `UNPROVEN` |
| Construction | blueprint/site/work order registration, start/progress/cancel/complete, material reservation, restore | `EngineFramework/GameplayWorldStateOwners/Construction/include/Epidemic/GameFramework/Construction/construction.h`; `EngineFramework/GameplayWorldStateOwners/Construction/src/construction.cpp` | sites, work orders, reservations, spatial/owner indexes, journal/cursor, revisions | provider reservation failure, allocation, stale site/order, lifecycle | `construction_tests.cpp` | `UNPROVEN` |
| Crime | crime report/case/suspect lifecycle, evidence updates, authority response, restore | `EngineFramework/GameplayWorldStateOwners/Crime/include/Epidemic/GameFramework/Crime/crime.h`; `EngineFramework/GameplayWorldStateOwners/Crime/src/crime.cpp` | reports, cases, suspect/victim/location indexes, journal/cursor, revisions | invalid enum/state, allocation, callback failure, stale IDs | `crime_tests.cpp` | `UNPROVEN` |
| Dialogue | conversation/session/choice lifecycle, consequence execution, restore | `EngineFramework/GameplayWorldStateOwners/Dialogue/include/Epidemic/GameFramework/Dialogue/dialogue.h`; `EngineFramework/GameplayWorldStateOwners/Dialogue/src/dialogue.cpp` | conversation definitions, sessions, participant indexes, pending consequences, journal/cursor, revisions | consequence callback, invalid choice, allocation, stale session | `dialogue_tests.cpp` | `UNPROVEN` |
| Economy | account/market/order/trade lifecycle, reservation/settlement, restore | `EngineFramework/GameplayWorldStateOwners/Economy/include/Epidemic/GameFramework/Economy/economy.h`; `EngineFramework/GameplayWorldStateOwners/Economy/src/economy.cpp` | accounts, offers/orders, balances, indexes, journal/cursor, revisions | insufficient funds, overflow, allocation, settlement callback | `economy_tests.cpp` | `UNPROVEN` |
| Effects | effect registration, `Execute`, `Defer`, bind/cancel/acknowledge deferred effects, restore | `EngineFramework/GameplayWorldStateOwners/Effects/include/Epidemic/GameFramework/Effects/effects.h`; `EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp` | definitions, deferred records, target bindings, journal/cursor, revisions | handler prepare/commit/rollback, allocation, stale deferred ID | `effects_tests.cpp` | `UNPROVEN` |
| Encounters | spawn point/encounter registration, spawn/bind/activate/complete, respawn schedule/process, restore | `EngineFramework/GameplayWorldStateOwners/Encounters/include/Epidemic/GameFramework/Encounters/encounters.h`; `EngineFramework/GameplayWorldStateOwners/Encounters/src/encounters.cpp` | spawn points, encounters, bindings, respawn queues, journal/cursor, revisions | spawn callback, allocation, stale binding, schedule overflow | `encounters_tests.cpp` | `UNPROVEN` |
| Entities | entity definition/create, destroy request/commit, activate/deactivate, materialize/remove, restore | `EngineFramework/GameplayWorldStateOwners/Entities/include/Epidemic/GameFramework/Entities/entities.h`; `EngineFramework/GameplayWorldStateOwners/Entities/src/entities.cpp` | entity slots, generation/free-list, active/materialized indexes, journal/cursor, revisions | stale generation, allocation, invalid lifecycle, runtime callback | `entities_tests.cpp` | `UNPROVEN` |
| Environment | layer/profile registration, layer create/update/remove/expiry, blend callbacks, restore | `EngineFramework/GameplayWorldStateOwners/Environment/include/Epidemic/GameFramework/Environment/environment.h`; `EngineFramework/GameplayWorldStateOwners/Environment/src/environment.cpp` | layers, region/type indexes, expiry queues, projections, journal/cursor, revisions | blend callback, allocation, invalid region/layer, overflow | `environment_gameplay_tests.cpp` | `UNPROVEN` |
| Equipment | profile/loadout/binding creation, equip/unequip, external item reserve/release, restore | `EngineFramework/GameplayWorldStateOwners/Equipment/include/Epidemic/GameFramework/Equipment/equipment.h`; `EngineFramework/GameplayWorldStateOwners/Equipment/src/equipment.cpp` | profiles, loadouts, slot bindings, item reservations, journal/cursor, revisions | item provider failure, allocation, stale slot/item, lifecycle | `equipment_tests.cpp` | `UNPROVEN` |
| Interaction | interaction definition, start/commit/schedule binding, complete/cancel, executor callback, restore | `EngineFramework/GameplayWorldStateOwners/Interaction/include/Epidemic/GameFramework/Interaction/interaction.h`; `EngineFramework/GameplayWorldStateOwners/Interaction/src/interaction.cpp` | sessions, schedules, participant indexes, journal/cursor, revisions | executor failure, allocation, stale session, time binding failure | `interaction_gameplay_tests.cpp` | `UNPROVEN` |
| ItemsInventory | container/item create, transfer/split/merge/remove, world bindings, restore | `EngineFramework/GameplayWorldStateOwners/ItemsInventory/include/Epidemic/GameFramework/ItemsInventory/items_inventory.h`; `EngineFramework/GameplayWorldStateOwners/ItemsInventory/src/items_inventory.cpp` | containers, item stacks, capacity indexes, bindings, journal/cursor, revisions | capacity/overflow, allocation, stale item/container, external binding | `items_inventory_tests.cpp` | `UNPROVEN` |
| Knowledge | profile/knowledge create/update/remove/decay, source indexes, restore | `EngineFramework/GameplayWorldStateOwners/Knowledge/include/Epidemic/GameFramework/Knowledge/knowledge.h`; `EngineFramework/GameplayWorldStateOwners/Knowledge/src/knowledge.cpp` | profiles, knowledge records, source/subject indexes, decay queue, journal/cursor, revisions | stale source/profile, allocation, decay overflow | `knowledge_memory_tests.cpp` | `UNPROVEN` |
| Loot | loot table/generation, pending delivery, schedule bind, claim/cancel, reward handler callbacks, restore | `EngineFramework/GameplayWorldStateOwners/Loot/include/Epidemic/GameFramework/Loot/loot.h`; `EngineFramework/GameplayWorldStateOwners/Loot/src/loot.cpp` | tables, pending rewards, delivery stages, schedule bindings, journal/cursor, revisions | reward prepare/commit/cancel, allocation, stale claim | `loot_tests.cpp` | `UNPROVEN` |
| Materials | material composition/state/reaction mutations and restore | `EngineFramework/GameplayWorldStateOwners/Materials/include/Epidemic/GameFramework/Materials/materials.h`; `EngineFramework/GameplayWorldStateOwners/Materials/src/materials.cpp` | material records, composition/reaction indexes, state maps, journal/cursor, revisions | invalid composition, reaction callback, allocation, overflow | `materials_tests.cpp` | `UNPROVEN` |
| Narrative | thread/objective/event/choice/storylet/journal/clue/rumor mutations, consequence callbacks, restore | `EngineFramework/GameplayWorldStateOwners/Narrative/include/Epidemic/GameFramework/Narrative/narrative.h`; `EngineFramework/GameplayWorldStateOwners/Narrative/src/narrative.cpp` | narrative records, active indexes, consequence outbox, journal/cursor, revisions | consequence failure, allocation, stale thread/objective, ordering overflow | `narrative_tests.cpp` | `UNPROVEN` |
| NavigationSemantics | profile/layer/link mutations, dynamic updates, restore | `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/include/Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h`; `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/src/navigation_semantics.cpp` | semantic layers, links, profile indexes, dirty regions, journal/cursor, revisions | invalid link, allocation, stale profile/layer, spatial bounds | `navigation_semantics_tests.cpp` | `UNPROVEN` |
| NeedsLife | need profiles/states/pressures/routines, simulation/expiry, restore | `EngineFramework/GameplayWorldStateOwners/NeedsLife/include/Epidemic/GameFramework/NeedsLife/needs_life.h`; `EngineFramework/GameplayWorldStateOwners/NeedsLife/src/needs_life.cpp` | need records, routine queues, pressure indexes, journal/cursor, revisions | invalid ranges, allocation, expiry overflow, stale actor | `needs_life_tests.cpp` | `UNPROVEN` |
| Ownership | ownership/permission/claim mutations, reverse indexes, restore | `EngineFramework/GameplayWorldStateOwners/Ownership/include/Epidemic/GameFramework/Ownership/ownership.h`; `EngineFramework/GameplayWorldStateOwners/Ownership/src/ownership.cpp` | ownership records, subject/object reverse indexes, claims, journal/cursor, revisions | stale ref, permission conflict, allocation, overflow | `ownership_tests.cpp` | `UNPROVEN` |
| Perception | perceiver/stimulus/contact lifecycle, delayed queues, observer callbacks, restore | `EngineFramework/GameplayWorldStateOwners/Perception/include/Epidemic/GameFramework/Perception/perception.h`; `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp` | perceivers, stimuli, contacts, delayed queues, observer state, journal/cursor, revisions | observer callback, allocation, stale contact, retention overflow | `perception_tests.cpp` | `UNPROVEN` |
| Population | groups/units/membership/materialization/migration, restore | `EngineFramework/GameplayWorldStateOwners/Population/include/Epidemic/GameFramework/Population/population.h`; `EngineFramework/GameplayWorldStateOwners/Population/src/population.cpp` | group/unit records, membership indexes, migration queues, journal/cursor, revisions | stale membership, allocation, materialization callback, overflow | `population_tests.cpp` | `UNPROVEN` |
| Processes | station/instance/reservation lifecycle, provider prepare/commit/cancel, restore | `EngineFramework/GameplayWorldStateOwners/Processes/include/Epidemic/GameFramework/Processes/processes.h`; `EngineFramework/GameplayWorldStateOwners/Processes/src/processes.cpp` | stations, process instances, reservations, queues, journal/cursor, revisions | provider prepare/commit/cancel, allocation, stale instance, lifecycle | `processes_tests.cpp` | `UNPROVEN` |
| Progression | profile/track/progress/reservation/milestone lifecycle, restore | `EngineFramework/GameplayWorldStateOwners/Progression/include/Epidemic/GameFramework/Progression/progression.h`; `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp` | profiles, tracks, progress records, milestone indexes, journal/cursor, revisions | reward/reservation failure, allocation, overflow, stale profile | `progression_tests.cpp` | `UNPROVEN` |
| ResourcesProduction | nodes/stockpiles/flows/reservations/production cycles, provider callbacks, restore | `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/include/Epidemic/GameFramework/ResourcesProduction/resources_production.h`; `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp` | resource nodes, stockpiles, reservations, flow indexes, journal/cursor, revisions | capacity/underflow, provider failure, allocation, stale reservation | `resources_production_tests.cpp` | `UNPROVEN` |
| RolesJobs | workplaces/assignments/duties/schedules and activation/expiry, restore | `EngineFramework/GameplayWorldStateOwners/RolesJobs/include/Epidemic/GameFramework/RolesJobs/roles_jobs.h`; `EngineFramework/GameplayWorldStateOwners/RolesJobs/src/roles_jobs.cpp` | workplaces, roles, assignments, duty schedules, journal/cursor, revisions | stale worker/job, allocation, schedule overflow, lifecycle | `roles_jobs_tests.cpp` | `UNPROVEN` |
| SaveGame | order resolution, capture, migration, validation, stage, commit boundary and barrier release | `EngineFramework/GameplayWorldStateOwners/SaveGame/include/Epidemic/GameFramework/SaveGame/save_game.h`; `EngineFramework/GameplayWorldStateOwners/SaveGame/src/save_game.cpp` | participant list, staged snapshots, restore stages, barrier, journal/cursor via participants | capture/stage/commit failure, participant exception, allocation, migration failure | `save_game_tests.cpp` | `UNPROVEN` |
| Simulation | region/layer/interval/task/summary lifecycle, executor callbacks, retention, restore | `EngineFramework/GameplayWorldStateOwners/Simulation/include/Epidemic/GameFramework/Simulation/simulation.h`; `EngineFramework/GameplayWorldStateOwners/Simulation/src/simulation.cpp` | layers, tasks, summaries, queues, retention records, journal/cursor, revisions | executor failure, allocation, stale task, budget/retention overflow | `simulation_tests.cpp` | `UNPROVEN` |
| Society | memberships, relationships, reputation and reverse indexes, restore | `EngineFramework/GameplayWorldStateOwners/Society/include/Epidemic/GameFramework/Society/society.h`; `EngineFramework/GameplayWorldStateOwners/Society/src/society.cpp` | memberships, relationships, reputation records, reverse indexes, journal/cursor, revisions | stale member, conflict, allocation, overflow | `society_tests.cpp` | `UNPROVEN` |
| Traversal | state/grant/route/session/carrier lifecycle and restore | `EngineFramework/GameplayWorldStateOwners/Traversal/include/Epidemic/GameFramework/Traversal/traversal.h`; `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp` | traversal states, grants, routes, sessions, carrier indexes, journal/cursor, revisions | stale route/session, allocation, grant conflict, lifecycle | `traversal_tests.cpp` | `UNPROVEN` |
| World | region/area/location/type definitions, static/dynamic features, placements, alteration transactions, compaction, restore | `EngineFramework/GameplayWorldStateOwners/World/include/Epidemic/GameFramework/World/world.h`; `src/world.cpp` | definitions, features, placements, alteration maps, spatial indexes, staged transaction state, journal/cursor, generators, revisions | invalid enum, stale feature/placement, allocation, transaction commit/cancel, spatial small/large path | `world_tests.cpp` | `UNPROVEN` |

#### Mutation API matrix, EngineFramework integration and boundary APIs

| Area | API family | Source / implementation anchor | State touched | Failure boundaries | Existing coverage | Result |
|---|---|---|---|---|---|---:|
| RuntimeBridge | backend `Materialize/Dematerialize/Destroy/ApplyImpulse/Project*`, service `Process`, path `Request/Cancel/Release` | `EngineFramework/RuntimeBoundary/RuntimeBridge/include/Epidemic/GameFramework/RuntimeBridge/runtime_bridge.h`; `src/runtime_bridge.cpp` | projection queue, runtime bindings, reconciliation records, backend handles | backend failure/exception, allocation, stale generation, retry/reconciliation | `runtime_bridge_tests.cpp` | `UNPROVEN` |
| Integration/Core | query provider registration, scheduled trigger dispatcher, time facts publishing, save participant commit | `EngineFramework/IntegrationLayer/Integration/include/Epidemic/GameFramework/Integration/core_adapters.h`; `src/core_adapters.cpp` | provider registry, trigger handler maps, cursors/checkpoints, facts events | handler exception, registry freeze, allocation, restore commit boundary | `core_integration_tests.cpp` | `UNPROVEN` |
| GameplayIntegration | combat/effects/progression/ability/time/death reward adapters and checkpoints | `EngineFramework/IntegrationLayer/GameplayIntegration/include/Epidemic/GameFramework/GameplayIntegration/gameplay_adapters.h`; `src/gameplay_adapters.cpp` | mappings, cursors, checkpoints, reservations, reward stages | provider reserve/commit/release, allocation, stale cursor, rollback | `gameplay_integration_tests.cpp` | `UNPROVEN` |
| ExtendedGameplayIntegration | equipment/items, process/item IO, dialogue/knowledge, trade coordinator | `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/include/Epidemic/GameFramework/ExtendedGameplayIntegration/extended_gameplay_adapters.h`; `src/extended_gameplay_adapters.cpp` | reservations, trade executions, checkpoints, cross-major cursors | second-prepare failure, commit/cancel failure, allocation, restore validation | `extended_gameplay_integration_tests.cpp` | `UNPROVEN` |
| InteractionEffects/Time | interaction effect executor and time adapter registration/checkpoints | `InteractionEffectsIntegration/*.h`, `InteractionTimeIntegration/*.h`; matching `src/*.cpp` | mappings, scheduled bindings, checkpoints, cursors | executor failure, allocation, dispatcher registration failure | dedicated integration tests | `UNPROVEN` |
| Narrative/State/World/Social/Population/Process/Traversal integration | contract/mapping registration, pending change processors, checkpoint restore, save participants | `EngineFramework/IntegrationLayer/*/include/**/*.h`; matching `src/*.cpp` | mapping registries, checkpoints, cursors, pending queues, save participant state | registry freeze, callback/provider failure, allocation, stale cursor, restore validation | dedicated integration tests | `UNPROVEN` |

**Результат пункта 1, freeze 2026-09-13:** `FREEZE_READY / MATRIX_SNAPSHOT_LOCKED`. Публичная mutation surface зафиксирована в воспроизводимом per-method snapshot: 824 rows, включая abstract contract rows, inline header rows, concrete implementation anchors, state contract, failure boundaries, postconditions и test anchors. Snapshot намеренно оставляет evidence как `UNPROVEN`, пока конкретная строка не покрыта bounded allocation sweep + full pre-state comparator. Повторная проверка: 0 concrete implementation lookups, 0 multiple implementation candidates, 19 missing test anchors. `MISSING_TEST_ANCHOR` является зафиксированным evidence debt для пунктов 4-7, а не скрытым долгом пунктов 1-3.

### 2. Усилить общий fault-injection harness

Доработать `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h`:

1. Добавить bounded sweep helper, принимающий имя API, максимальный fault index и callback одного запуска.
2. Возвращать диагностический результат: API, fault index, был ли вызван метод, тип отказа и достигнутый postcondition.
3. Различать `std::bad_alloc`, контролируемый `Result` с allocation code, callback failure и успешный вызов.
4. Гарантировать отключение fault injection через RAII перед capture/compare postconditions.
5. Сохранить исключение для внутренней 16-байтовой bookkeeping allocation MSVC checked iterators.
6. Ограничить каждый sweep числом реально наблюдаемых allocations плюс небольшой запас; отсутствие успешного завершающего прохода считать ошибкой теста.
7. Не использовать большие load fixtures для fault sweeps. Для каждого API создавать минимальное валидное состояние.

**Выполнено 2026-09-13, общий harness:** `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h` усилен без нарушения обратной совместимости со старым `FailAfter`:

- добавлены `FailureKind`, `InvocationResult`, `SweepIteration`, `SweepRunContext` и `SweepReport`;
- добавлен bounded helper `RunBoundedSweep(api, max_fault_index, invoke, verify)`;
- каждый iteration фиксирует API, fault index, был ли включен fault injection, был ли реально отмечен вызов метода, тип отказа, detail и результат postcondition;
- `verify` запускается после выхода из `FailAfter`, поэтому capture/compare postconditions не попадают под искусственную allocation failure;
- различаются `std::bad_alloc`, controlled allocation `Result` по error code с `alloc`/`memory`/`bad_alloc`, generic controlled failure, callback failure и unexpected exception;
- сохранено MSVC-исключение для 16-байтовой checked-iterator bookkeeping allocation;
- отсутствие успешного прохода после bounded fault range отмечается через `exhausted_without_success`, а `SweepReport::Passed()` требует успешный проход и все postconditions.
- добавлен `ObserveAllocations` и `CountObservedAllocations(make_invoke)`;
- добавлен `RunObservedBoundedSweep(api, observed_allocation_spare, make_invoke, verify)`, который сам делает dry-run, измеряет реально наблюдаемые allocations и запускает bounded sweep до `observed_allocations + spare`;
- `SweepReport` теперь хранит `observed_allocations`, `observed_allocation_spare` и `max_fault_index`;
- classification allocation result codes стала case-insensitive (`alloc`, `memory`, `bad_alloc`, включая mixed-case формы).

Self-test добавлен в `EngineFramework/DevelopmentInfrastructure/Tests/foundation_tests.cpp`: проверяются реальный `std::bad_alloc`, controlled allocation `Result`, callback failure и exhausted sweep без success. Проверка выполнена через отдельную Ninja build-папку:

```powershell
cmake -S . -B build/codex-harness-ninja -G Ninja -DBUILD_TESTING=ON -DEPIDEMIC_BUILD_RUNTIME=ON -DEPIDEMIC_BUILD_FRAMEWORK=ON -DEPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/codex-harness-ninja --target EpidemicGameFrameworkFoundationTests --parallel 4
ctest --test-dir build/codex-harness-ninja -R "^EpidemicGameFrameworkFoundationTests$" --output-on-failure --timeout 60
```

Результат пункта 2, freeze 2026-09-13: `FREEZE_READY / HARNESS_LOCKED`. Общий harness закрывает требования пункта: bounded sweep, structured diagnostics, failure classification, RAII-disable перед postconditions, MSVC checked-iterator exception, automatic observed allocation ceiling через `RunObservedBoundedSweep`, failure on exhausted sweep, и минимальное-state factory model для новых per-API tests. Старый `RunBoundedSweep` сохранён как ручной низкоуровневый API.

### 3. Стандартизировать проверку полного pre-state

Для каждого major добавить локальный `StateFingerprint` или явный comparator, который после отказа проверяет:

- primary records и их payload;
- secondary/reverse/spatial indexes через публичные queries;
- ID generator scopes и `next`;
- global и record revisions;
- journal epoch, sequence, retained records и latest cursor;
- diagnostics, budgets и pending queues, если они authoritative;
- состояние внешнего provider/handler и число вызовов callbacks.

Одной проверки `revision` недостаточно. Для snapshot owners сравнивать полный логический snapshot и отдельно результаты index-based queries. Для owners без snapshot API перечислять и проверять все доступные публичные read-модели.

**Выполнено 2026-09-13, стандарт проверки:** добавлен общий тестовый helper `EngineFramework/DevelopmentInfrastructure/Tests/pre_state_verification.h`.

- `StateFacet` фиксирует обязательные части authoritative pre-state: primary records, record payloads, secondary indexes, ID generators, revisions, journal/epoch/sequence/retained records/latest cursor, diagnostics, budgets, pending queues, external callbacks и public read models.
- `StateComparator` требует явных checks и возвращает `ComparisonReport`.
- `ComparisonReport::Passed()` запрещает пустой comparator как ложный success.
- `ComparisonReport::PassedAndCovers(required_facets)` не даёт принять проверку, которая сравнила только `revision` и пропустила обязательные facets.
- добавлены freeze-gate наборы `RequiredMutationFacets`, `RequiredJournaledMutationFacets` и `RequiredExternalMutationFacets`.
- `CompareSnapshotState` и `CompareCapturedState` добавлены для owners, где удобно сравнивать snapshot или captured state.
- Self-test добавлен в `EngineFramework/DevelopmentInfrastructure/Tests/foundation_tests.cpp`: проверяет полный comparator, journaled gate, failure для revision-only comparator и mismatch secondary index.

Результат пункта 3, freeze 2026-09-13: `FREEZE_READY / PRE_STATE_GATE_LOCKED`. Общий pre-state standard больше не является устным правилом: он представлен типами, обязательными facet-наборами и self-test. Per-major local fingerprints/comparators будут добавляться при закрытии строк матрицы в пунктах 4-7, но стандарт, по которому они принимаются, теперь заморожен.

### Проверка полноты пунктов 1-3, 2026-09-13

1. Пункт 1: `FREEZE_READY / MATRIX_SNAPSHOT_LOCKED`. Есть generated per-method matrix snapshot и script для повторной проверки drift.
2. Пункт 2: `FREEZE_READY / HARNESS_LOCKED`. Есть manual bounded API и observed-allocation bounded API; оба покрыты foundation self-test.
3. Пункт 3: `FREEZE_READY / PRE_STATE_GATE_LOCKED`. Есть typed facets, required facet sets и self-test, который отбрасывает revision-only comparator.

Вывод аудита: пункты 1-3 теперь можно заморозить как базовый контракт и тестовую инфраструктуру. Дальнейшая работа по пунктам 4-7 должна брать строки из `docs/milestone2_mutation_api_matrix.md`, писать per-API observed bounded sweeps через `RunObservedBoundedSweep`, проверять state через `RequiredMutationFacets` / `RequiredJournaledMutationFacets` / `RequiredExternalMutationFacets`, и только после этого переводить конкретные evidence rows из `UNPROVEN` в `PASS`.

### Freeze guardrails для следующих проходов

1. Пункт 1, что проверять: перед началом каждого major перегенерировать `docs/milestone2_mutation_api_matrix.md`, смотреть diff, брать API rows из snapshot, проверять header anchor, implementation/inline/abstract anchor, state contract, failure boundaries, postcondition и test anchor.
2. Пункт 1, что было забыто: нельзя считать grouped overview полной матрицей; нельзя оставлять wildcard/source-family anchors как "точные"; нельзя доверять regex-generator без sanity checks на `IMPLEMENTATION_LOOKUP_REQUIRED`, `MULTIPLE_IMPL_CANDIDATES`, private `*Impl`, constructors, field initializers и const getters.
3. Пункт 2, что проверять: новый fault test должен использовать `RunObservedBoundedSweep` с factory свежего минимального состояния, проверять `report.Passed()`, `saw_failure`, `saw_success`, `postconditions_met`, `!exhausted_without_success`, `observed_allocations >= 0`, ожидаемый `max_fault_index`, тип failure и то, что postcondition выполняется после RAII-disable.
4. Пункт 2, что было забыто: одного ручного `max_fault_index` недостаточно для freeze; нельзя запускать postcondition capture под активным `FailAfter`; нельзя смешивать callback failure, controlled allocation failure и `std::bad_alloc`; нельзя использовать большие load fixtures как замену минимального валидного состояния.
5. Пункт 3, что проверять: postcondition после отказа должен покрывать полный required facet set, а не только `revision`; для journaled owners использовать `RequiredJournaledMutationFacets`, для external/provider mutations - `RequiredExternalMutationFacets`, для non-journaled authoritative state - `RequiredMutationFacets`.
6. Пункт 3, что было забыто: проверка только revision создаёт ложный PASS; snapshot equality без index-based public queries не доказывает secondary/reverse/spatial indexes; external provider/handler callback counts и staged reservations нужно сравнивать отдельно; пустой comparator или неполный facet coverage должен считаться ошибкой теста.

### 4. Закрыть Part 2 по majors

Обрабатывать последовательно, не смешивая исправления разных majors:

1. `Effects`: `Execute`, `Defer`, bind/cancel/acknowledge deferred effects, restore; проверить transactional handler commit и rollback.
2. `Encounters`: spawn point, encounter spawn/bind/activate/complete, respawn scheduling/processing, restore.
3. `Entities`: create, destroy request/commit, activate/deactivate, materialization, remove, restore; проверять slot/free-list/generation indexes.
4. `Environment`: layer creation/update/removal/expiry, blend callbacks, restore.
5. `Equipment`: profile/loadout/binding creation, equip/unequip, external item reservation/release, restore.
6. `Interaction`: commit/start, schedule binding, complete/cancel, executor callback, restore.
7. `ItemsInventory`: container/item create, transfer/split/merge/remove, world bindings, capacity indexes, restore.
8. `Knowledge`: profile/knowledge create/update/remove/decay, source indexes, restore.
9. `Loot`: generation, pending delivery, schedule bind, claim/cancel, external delivery callbacks, restore.
10. `Materials`: composition/state/reaction mutations and restore.
11. `Narrative`: thread/objective/event/choice/storylet/journal/clue/rumor mutations, consequence callbacks, restore.
12. `NavigationSemantics`: profile/layer/link mutations, dynamic updates, restore.
13. `NeedsLife`: profiles, need states, pressures, routines, simulation/expiry, restore.

Для каждого major порядок одинаковый: заполнить матрицу, добавить тесты, запустить только его test target с process timeout, исправить подтверждённые дефекты, повторить target, затем пометить строки матрицы.

**Выполнено 2026-09-14, Part 2 restore/fault freeze-pass:** закрыт общий `RestoreSnapshot`
контракт для всех 13 Part 2 majors из этого пункта.

- Добавлен `EngineFramework/DevelopmentInfrastructure/Tests/restore_fault_sweep.h`: restore-specific observed sweep теперь готовит target snapshot до включения `FailAfter`, делает observed dry-run, снимает post-probe live baseline и проверяет pre-state только для failed iterations. Это исправляет старую ошибку тестов, где fault injection часто бил по копированию snapshot внутри теста, а не по самому `RestoreSnapshot`.
- `EngineFramework/DevelopmentInfrastructure/Tests/pre_state_verification.h` расширен набором `RequiredJournaledMutationFacetsWithoutIdGenerator` для journaled owners без authoritative ID generator, а failure diagnostics печатают scope и failed facets.
- В 13 suites заменены старые ручные restore loops на `RunObservedRestoreSweep` + `StateComparator`: `Effects`, `Encounters`, `Entities`, `Environment`, `Equipment`, `Interaction`, `ItemsInventory`, `Knowledge`, `Loot`, `Materials`, `Narrative`, `NavigationSemantics`, `NeedsLife`.
- `Materials` restore sweep перенесён на минимальный валидный fixture до sparse-state stress, чтобы fault test не нарушал performance budget.
- В `docs/milestone2_mutation_api_matrix.md` помечены `PASS` ровно 13 строк `GameplayWorldStateOwners/<Major>::RestoreSnapshot`; `docs/milestone2_mutation_api_matrix.py` обновлён так, чтобы эти Part 2 restore evidence marks воспроизводились генератором.

Проверка:

```powershell
cmake --build build/codex-harness-ninja --target EpidemicGameFrameworkEffectsTests EpidemicGameFrameworkEncountersTests EpidemicGameFrameworkEntitiesTests EpidemicGameFrameworkEnvironmentTests EpidemicGameFrameworkEquipmentTests EpidemicGameFrameworkInteractionTests EpidemicGameFrameworkItemsInventoryTests EpidemicGameFrameworkKnowledgeTests EpidemicGameFrameworkLootTests EpidemicGameFrameworkMaterialsTests EpidemicGameFrameworkNarrativeTests EpidemicGameFrameworkNavigationSemanticsTests EpidemicGameFrameworkNeedsLifeTests --parallel 4
ctest --test-dir build/codex-harness-ninja -R "^EpidemicGameFramework(Effects|Encounters|Entities|Environment|Equipment|Interaction|ItemsInventory|Knowledge|Loot|Materials|Narrative|NavigationSemantics|NeedsLife)Tests$" --output-on-failure --timeout 120
```

Результат: `13/13 PASS`, total test time `35.85 s`.

**Перепроверено 2026-09-14, Effects freeze-pass:** пункт 4 нельзя честно закрывать только restore sweep-ами. Добавлен общий `mutation_fault_sweep.h`, который прогоняет все observed fault indices на fresh fixture, отключает RAII fault injection перед capture/compare и отличает реально сработавший injected allocation failure от обычного success path. На этом harness закрыт `Effects` major полностью: `RegisterHandler`, `RegisterDefinition`, `Freeze`, `SetTargetStateProvider`, `Execute`, `Defer`, `BindDeferredSchedule`, `ClearDeferredSchedule`, `AcknowledgeDeferredBySchedule`, `TakeDeferredBySchedule`, `CancelDeferred`, `CancelDeferredTargeting`, `RestoreSnapshot` и внешний `IEffectHandler::Commit` contract через `ExternalCallbacks` facet.

Найден и исправлен дефект в `EffectService::Execute`: первая wave могла публиковать `execution_ids_`/journal до allocation failure в `PrepareWave`. Теперь первая wave staging выполняется до live publish, а `PrepareWave` возвращает controlled storage failure при невозможности выделить prepared-operation storage. Regression evidence: `EpidemicGameFrameworkEffectsTests` и полный Part 2 CTest suite проходят.

Проверка:

```powershell
cmake --build build/codex-harness-ninja --target EpidemicGameFrameworkEffectsTests EpidemicGameFrameworkEncountersTests EpidemicGameFrameworkEntitiesTests EpidemicGameFrameworkEnvironmentTests EpidemicGameFrameworkEquipmentTests EpidemicGameFrameworkInteractionTests EpidemicGameFrameworkItemsInventoryTests EpidemicGameFrameworkKnowledgeTests EpidemicGameFrameworkLootTests EpidemicGameFrameworkMaterialsTests EpidemicGameFrameworkNarrativeTests EpidemicGameFrameworkNavigationSemanticsTests EpidemicGameFrameworkNeedsLifeTests --parallel 4
ctest --test-dir build/codex-harness-ninja -R "^EpidemicGameFramework(Effects|Encounters|Entities|Environment|Equipment|Interaction|ItemsInventory|Knowledge|Loot|Materials|Narrative|NavigationSemantics|NeedsLife)Tests$" --output-on-failure --timeout 120
```

Результат: `13/13 PASS`, total test time `17.99 s`.

Статус строгого пункта 4: `PARTIAL / EFFECTS_FULL_PASS_RESTORE_ROWS_PASS`. Текущая Part 2 matrix: `168` rows, `23 PASS`, `145 UNPROVEN`. `Effects` можно морозить; restore rows оставшихся 12 majors можно морозить; весь пункт 4 ещё нельзя переводить в полный `PASS`, пока не добавлены такие же per-API bounded allocation/pre-state sweeps для `Encounters`, `Entities`, `Environment`, `Equipment`, `Interaction`, `ItemsInventory`, `Knowledge`, `Loot`, `Materials`, `Narrative`, `NavigationSemantics` и `NeedsLife`.

### 5. Закрыть Part 3 по majors

Порядок проверки:

1. `Ownership`: ownership/permission/claim mutations, reverse indexes, restore.
2. `Perception`: perceiver/stimulus/contact lifecycle, delayed queues, observer callbacks, restore.
3. `Population`: groups/units/membership/materialization/migration and restore.
4. `Processes`: station/instance/reservation lifecycle, provider prepare/commit/cancel, restore.
5. `Progression`: profile/track/progress/reservation/milestone lifecycle, restore.
6. `ResourcesProduction`: nodes/stockpiles/flows/reservations/production cycles, provider callbacks, restore.
7. `RolesJobs`: workplaces/assignments/duties/schedules and activation/expiry, restore.
8. `SaveGame`: order resolution, capture, migration, validation, stage, commit boundary and barrier release.
9. `Simulation`: region/layer/interval/task/summary lifecycle, executor callbacks, retention, restore.
10. `Society`: memberships, relationships, reputation and all reverse indexes, restore.
11. `Traversal`: state/grant/route/session/carrier lifecycle and restore.
12. `World`: dynamic features, placements, alteration transactions, compaction, spatial indexes and restore.

`World` должен иметь sweeps для create/update/remove transaction с минимум двумя alterations и для small/large spatial-index paths. `SaveGame` должен доказывать, что ни один `CommitRestore` не вызывается после любого staging failure.

### 6. Проверить external commit contracts

Для API с внешними handlers/providers создать управляемые doubles со сценариями:

- `Prepare` возвращает failure;
- `Prepare` бросает исключение;
- второй `Prepare` падает после успешного первого;
- `Commit` возвращает failure, если контракт это допускает;
- `Commit` бросает, если интерфейс не `noexcept`;
- rollback/cancel возвращает failure либо бросает;
- локальная allocation failure происходит до и после подготовки внешнего состояния.

Безопасный контракт должен быть одним из двух:

- внешний `Commit` гарантированно `noexcept` и вызывается только после завершения всей fallible локальной подготовки;
- сервис хранит rollback token и компенсирует уже выполненные commits.

Если компенсация может отказать, API обязан вернуть durable reconciliation record, который сохраняется в snapshot и имеет отдельные retry/idempotency тесты.

### 7. Исправлять только воспроизведённые дефекты

Для каждого красного fault index:

1. Зафиксировать API, index, exception/result code и нарушенный postcondition.
2. Убедиться, что отказ относится к движку, а не к MSVC iterator bookkeeping или самому test double.
3. Перенести все fallible операции до commit boundary либо добавить rollback guard/node handle.
4. Не публиковать live revision, generator, state или journal до завершения последней allocation/callback, способной сорвать операцию.
5. После исправления оставить минимальный regression на найденный index и bounded sweep вокруг всего API.
6. Не выполнять попутные refactors и форматирование несвязанных участков.

### 8. Контроль производительности тестов

- Каждый local suite должен укладываться в 60 секунд Debug-конфигурации.
- Один API sweep должен использовать минимальный fixture и обычно укладываться в 1 секунду.
- Fault tests не должны копировать состояния из load tests на 100k объектов.
- `EpidemicEngineLoadTruthTests` должен оставаться быстрее 240 секунд; текущая контрольная точка: 7.79 секунды.
- При существенном росте времени вывести длительность по секциям Time, Entities, Perception и AI и проверить сложность на `N`/`2N`.

### 9. Порядок сборки и тестирования

Во время разработки запускать только изменённый target:

```powershell
cmake --build build/framework-check --config Debug --target <TargetTests> --parallel 4
ctest --test-dir build/framework-check -C Debug -R "^<TestName>$" --output-on-failure --timeout 60
```

После закрытия всех строк матрицы выполнить один финальный цикл:

```powershell
cmake --build build/framework-check --config Debug --parallel 4
ctest --test-dir build/framework-check -C Debug --output-on-failure --timeout 300 -j 4
```

Дополнительно повторить Base+Runtime tests отдельным диапазоном и отдельно запустить load/smoke truth, чтобы отчёт содержал их собственные exit code и duration.

### 10. Критерии подтверждения Milestone 2

Milestone 2 можно перевести из `UNPROVEN` в `PASS` только если одновременно выполнено следующее:

1. В матрице нет `MISSING`, `FAIL` и `UNPROVEN`.
2. Каждый public multi-container/journal mutation API имеет bounded allocation sweep и проверку полного pre-state после отказа.
3. Все callback/external commit сценарии имеют проверенные rollback либо durable reconciliation semantics.
4. Все snapshot restore paths проверены на validation и allocation failure без разрушения live state.
5. Все revision/generation/journal overflow boundaries возвращают контролируемую ошибку без mutation.
6. Полная Debug-сборка успешна.
7. Полный CTest-прогон проходит без timeout и process termination.
8. Base+Runtime, load и smoke результаты зафиксированы отдельно.
9. `git diff --check` не содержит ошибок; временные probes и диагностические prints удалены.
10. В этом документе для каждого major стоит `PASS` со ссылками на тесты, а milestone-файлы обновлены только после финального прогона.
