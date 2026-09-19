# Epidemic Engine: план полной проверки и окончательного freeze

Дата исходного дерева: 2026-09-14.

Этот документ не является новой версией только Milestone 2. Он заменяет старую последовательность milestones единым планом доведения трех слоев движка, `EngineBase`, `EngineRuntime` и `EngineFramework`, от текущего состояния до окончательного `FROZEN`.

Главная единица первого круга: production-модуль целиком.

Главная единица второго круга: причинная цепочка или смысловой кластер.

Главная единица финального допуска: весь движок как единая система.

## 0. Текущее состояние, которое принимается за исходную точку

### 0.1. Размер дерева

В текущем архиве:

- 78 production-модулей;
- 232 public headers;
- EngineBase: 9 production-модулей;
- EngineRuntime: 17 production-модулей;
- EngineFramework: 52 production-модуля;
- Framework разбит на 5 BaseInfrastructure, 1 RuntimeBoundary, 33 GameplayWorldStateOwners и 13 IntegrationLayer;
- CMake регистрирует 89 test executables: 9 Base, 20 Runtime, 55 Framework и 5 Truth.

`EngineBase/Apps`, общие `Tests` и `EngineFramework/DevelopmentInfrastructure` являются проверочной или composition-инфраструктурой и не считаются отдельными production freeze units.

### 0.2. Что уже выглядит сильным

- Архитектурное разделение Base -> Runtime -> Framework выражено в CMake и документации.
- Большинство Runtime majors имеют собственный isolated test target.
- Все Framework production-модули имеют отдельные test targets или входят в отдельную integration suite.
- Framework уже содержит allocation fault-injection и pre-state verification helpers.
- Snapshot/restore широко используется и покрывается локальными тестами.
- Есть Truth suites для public contracts, одной integration chain, high-cardinality load и нескольких smoke paths.
- Есть deterministic random infrastructure.
- Есть крупный load test на 100 000 entities, 20 000 schedules, 5 000 perceivers, 10 000 AI agents и 1 000 runtime frames.

### 0.3. Что нельзя считать замороженным

#### Архитектурный gate сейчас не воспроизводится из архива

Top-level `CMakeLists.txt` включает:

`cmake/ArchitectureFreeze.cmake`

но этого файла в текущем архиве нет.

Документация и `Milestones_review.md` также ссылаются на:

`.github/workflows/architecture-freeze.yml`

но workflow в текущем архиве отсутствует.

До восстановления этих файлов нельзя считать architecture freeze технически подтвержденным на текущем дереве, даже если сама архитектура по коду в основном следует задуманным границам.

#### Статусы Milestone 2 противоречат друг другу

`Milestones.md` помечает local correctness выполненной.

Более новый `Milestone2_Roadmap.md` одновременно фиксирует строгий Framework verification как незавершенный.

В текущей mutation matrix около 824 evidence rows. Только 23 строки имеют `PASS`, остальные в основном `UNPROVEN`. Это не означает, что остальные API не тестируются. Это означает, что строгий fault/pre-state evidence не закрыт.

Кроме того, текущий matrix scanner уже показал ложные классификации и потерю части mutation surface. Поэтому matrix нельзя использовать как единственный критерий freeze.

#### Persistence готова локально, но не системно

Много локальных snapshot/restore tests уже есть. Текущий Truth smoke проверяет только узкое продолжение игровой сессии после восстановления нескольких Framework owners.

Нет доказанного whole-engine ordered restore, включающего Base composition state, Runtime state, Framework participants, RuntimeBridge reconciliation и продолжение полного causal scenario.

#### Determinism готов частично

Есть deterministic random, deterministic ordering contracts и точечные Truth tests.

Нет общего scenario runner:

`initial snapshot + seed + input stream + N ticks -> semantic state hash`

и нет повторяемого whole-engine replay после save/load.

#### Memory/lifetime qualification отсутствует как отдельный gate

Есть Memory module, load tests и множество cleanup tests.

В текущем CMake/Presets не найден sanitizer configuration. Нет отдельного длительного logical-retention suite, который доказывает bounded journals, tombstones, pending queues, caches и runtime bindings.

#### Concurrency qualification неполна

Есть scheduler tests, wrong-thread rejection и отдельные threading comments.

Нет единой per-service threading matrix и общего stress/sanitizer gate для официально поддерживаемых concurrency scenarios.

#### Integration проверяется по частям

Есть 13 Framework integration modules и dedicated tests. Есть один Truth path `Perception -> Knowledge -> AI`.

Нет полного набора cluster tests, который проверяет все критические causal chains между Base, Runtime и Framework.

#### Load qualification уже начата, но недостаточна для freeze

Есть high-cardinality Truth suite. Она проверяет несколько важных областей.

Не квалифицированы большие inventories, knowledge graphs, economy/process simulation, streaming churn, RuntimeBridge queues, repeated materialization/dematerialization, long journal retention и degradation/backpressure contracts.

#### Whole-engine smoke пока слишком узкий

Текущий Truth smoke покрывает:

1. Base headless lifecycle.
2. Runtime clock -> gameplay clock.
3. Entities + Progression save/restore continuation.
4. Runtime composition ticks/shutdown.

Этого недостаточно для окончательного freeze всего движка.

## 0.4. Статусы, используемые новым планом

`NOT_AUDITED`: модуль еще не проверен на текущем baseline.

`IN_AUDIT`: модуль находится в первом круге.

`BLOCKED`: есть воспроизведенный defect или недоказанный контракт, блокирующий локальную готовность.

`LOCAL_READY`: модуль полностью проверен сам по себе и может участвовать в системных цепочках.

`SYSTEM_READY`: модуль прошел все обязательные cross-module и cross-layer chains.

`FREEZE_READY`: локальные, системные, persistence, determinism, lifetime, concurrency, fault, load и regression gates для модуля выполнены.

`FROZEN`: публичный contract snapshot зафиксирован после финальной whole-engine qualification.

Ни один старый статус `ВЫПОЛНЕНО` не переносится автоматически. Текущий код должен пройти новый gate.

---

# Цель 1. Проверяемый baseline и единый freeze contract

Статус: `HARD_FROZEN` (2026-09-16). Подробная история остаётся в Git и CI; ниже — только действующий результат.

## 1.1. Build и architecture baseline

[x] Architecture gates запрещают Base -> Runtime/Framework и Runtime -> Framework, проверяют утверждённые link/include allowlists, production sources и generated sources fail-closed.
[x] Каждый из 232 public headers собирается отдельным consumer target с реальными `PUBLIC/INTERFACE` requirements; Base/Runtime/Full self-containment воспроизводим.
[x] Локальная и CI-матрица содержит Base, Runtime и Full в Debug/Release, `/W4 /WX`, точные CTest manifests, timeout policy и отдельный ClangCL public-surface профиль.
[x] Semantic CI validator проверяет YAML, обязательные steps, matrix, shell, failure policy и pinned runner/tool actions; negative fixtures обязательны.

## 1.2. Freeze evidence contract

[x] Для 78 production-модулей зафиксированы responsibility/ownership/dependencies, public contracts, state, lifecycle, failure atomicity, persistence applicability и test evidence.
[x] `LOCAL_READY` вычисляется из 37 единых критериев и полного per-item ledger, а не из свободного текста или наличия test executable.
[x] Exact API inventory использует стабильные signature IDs, запрещает `UNCLASSIFIED` и хранит contract/test anchors; non-callable public surface покрывает structs, fields, enums, aliases, constants, inheritance и templates.
[x] Production discovery, evidence anchors, configured CTest JSON, error/exception policy и adversarial self-tests входят в обязательный gate.
[x] Обещается source/API compatibility в пределах frozen contracts; стабильный C++ binary ABI не обещается.

## 1.3. Публикация и выход

[x] Локальная qualification 2026-09-16 прошла шесть strict-профилей и все freeze validators/self-tests.
[x] Verified code SHA: `2abe6d0d27dcf847dfa2fcb9d080fce743f4fa8a`; evidence commits: `2e34d2f`, `049f5a7`.
[x] GitHub Actions run `#9` (`35148315116`) прошёл `freeze-contract`, `clang-public-surface` и 6/6 build/test jobs; documentation attestation `83c8b3917e07826771be2a43d3a5e620859d089d` подтверждён run `#10` (`35150150448`).
[x] Зафиксированы Windows Server 2025 / `windows-2025-vs2026`, MSVC `19.51.36256.0`, ClangCL `22.1.3`, CMake `4.4.3`, Python `3.13.7`; отрицательные runs `#7`–`#8` сохранены как evidence найденных и устранённых portability defects.

Критерий выхода выполнен: baseline воспроизводим локально и в remote CI; дальнейшие цели используют этот contract без повторного определения правил.

---
# Цель 2. Полный локальный freeze-аудит EngineBase

Статус: `LOCAL_READY`, 9/9 модулей (2026-09-19). Системный freeze остаётся за целями 5–9.

## 2.1. Закрытые module contracts

[x] Foundation: стабильные errors/IDs/handles, `Result`, hash/equality, checked arithmetic и path normalization; без I/O, threads и верхних dependencies.
[x] Memory: size/alignment, tracking, budgets/tags, reset lifetime, saturation/OOM и failure atomicity.
[x] Diagnostics: disabled behavior, counters, RAII profiling, owning thread names, no-throw sinks/failure logging; не authoritative storage.
[x] Core: полная lifecycle state machine, sealed services, dependency/order/unwind/shutdown, EventBus reentrancy, scheduler/dispatcher, frame phases и stop semantics.
[x] Platform: Win32 window/event lifecycle, size/focus/exit/clock, DLL/symbol lifetime, neutral public surface и retryable/surfaced callback failures.
[x] Input: event-before-snapshot publication, immutable frame state, complete key/mouse transitions, focus/reset/no-event/invalid-input semantics и atomic retry.
[x] RHI: atomic creation, frame/clear/present/resize lifecycle, minimized surfaces, Null parity и destruction state.
[x] RHI_D3D11: COM/device/context/swap-chain/RTV lifetime, resize/minimize/device-error/debug paths и отсутствие upper-layer downcasts.
[x] Support: atomic composition bundles, duplicate handling, Null/D3D11 service parity, unique window/swap-chain ownership, frame order и application scoping.

## 2.2. Qualification и выход

[x] Windows 11 / MSVC 19.50 strict builds прошли: Base Debug/Release `14/14`, Runtime Debug/Release `34/34`, Full Debug/Release `94/94`; всего `284/284` CTest.
[x] Четыре unattended smoke applications зарегистрированы в CTest; D3D11 hidden-window smoke проходит отдельно.
[x] Все шесть `ctest_manifest --check-profile`, 37/37 LOCAL_READY criteria, 78/78 ledger records, exact API inventory (4309 callables), 232 public headers, freeze validators и negative self-tests зелёные.
[x] `git diff --check` чист; известного локального долга, переносимого в Goal 3, нет. Remote CI подтверждается отдельно перед публикацией.

Критерий выхода выполнен: EngineBase локально готов, Goal 3 может начинаться с зафиксированного admission baseline `14/34/94`.

---
# Цель 3. Полный локальный freeze-аудит EngineRuntime

Статус: `READY_TO_START` (2026-09-19). Goal 2 закрыт. Подробный рабочий план: `Epidemic_Goal3_Runtime_Freeze_Plan_2026-09-18.md`.

В этом `work-plan` остаются только задачи, подтверждённые проблемы и exit gates. Причины, конкретные failure paths, proposed fixes и regression requirements находятся в подробном Goal 3 плане.

## 3.0. Admission и общие обязательства

[x] EngineBase `9/9 LOCAL_READY`; admission matrix Base `14/14`, Runtime `34/34`, Full `94/94` в Debug/Release, суммарно `284/284`.
[x] Exact CTest manifests, 37/37 LOCAL_READY criteria, 78/78 ledger records, architecture/dossier/coverage/API/public-surface/CI validators и negative/self-tests зелёные.
[ ] `G3-DOC-001`: убрать преждевременные `frozen` status claims из Runtime module docs и синхронизировать их с ledger.
[ ] Для всех 17 modules пройти единый LOCAL_READY audit: contracts, authoritative/derived state, identity exhaustion, no-op semantics, failure atomicity, external boundaries, deterministic observable order, persistence applicability и cleanup ownership.
[ ] Каждый найденный defect получает failing regression до fix и постоянный evidence anchor после fix.

## 3.1. RuntimeFoundation

[ ] `G3-RF-001`: устранить противоречие TRS contract, зафиксировав deterministic approximate TRS semantics.
[ ] RuntimeFoundation audit complete; `EpidemicRuntimeFoundationTests` Debug/Release green; module = `LOCAL_READY`.

## 3.2. Time

[ ] `G3-TIME-001`: добавить persistence-complete clock checkpoint с fractional remainder и atomic restore.
[ ] Time audit complete; deterministic accumulation/persistent continuation tests green; module = `LOCAL_READY`.

## 3.3. Serialization

[ ] Serialization local contract/failure-atomicity/migration audit complete; `EpidemicRuntimeSerializationTests` Debug/Release green; module = `LOCAL_READY`.

## 3.4. Resources

[ ] `G3-RES-001`: сделать replacement dependency set атомарным.
[ ] `G3-RES-002`: исключить потерю WaitingForDependencies retry ownership.
[ ] `G3-RES-003`: отделить loader callback exception boundary от Runtime-local artifact finalization.
[ ] `G3-RES-004`: не удалять queue ownership до публикации следующего durable owner.
[ ] Resources ownership/load/dependency/budget/eviction/cancellation audit complete; `EpidemicRuntimeResourcesTests` Debug/Release green; module = `LOCAL_READY`.

## 3.5. Assets

[ ] Assets registration/path/dependency/seal audit complete; `EpidemicRuntimeAssetsTests` Debug/Release green; module = `LOCAL_READY`.

## 3.6. Streaming

[ ] `G3-STR-001`: local allocation failure не превращать в semantic streaming failure после partial mutation.
[ ] `G3-STR-002`: сохранять successful ExecuteStep result durably до local cursor/accounting commit.
[ ] Streaming demand/request/state-machine/external-failure/budget/shutdown audit complete; `EpidemicRuntimeStreamingTests` Debug/Release green; module = `LOCAL_READY`.

## 3.7. Scene

[ ] Scene identity/hierarchy/TRS/bounds/query/snapshot audit complete; `EpidemicRuntimeSceneTests` Debug/Release green; module = `LOCAL_READY`.

## 3.8. World

[ ] World topology/object identity/placement/persistence-tier/token/query audit complete; `EpidemicRuntimeWorldTests` Debug/Release green; module = `LOCAL_READY`.

## 3.9. Simulation

[ ] `G3-SIM-001`: не повторять successful job ExecuteStep после local staging failure.
[ ] `G3-SIM-002`: убрать обязательные Runtime allocations из post-commit housekeeping/result bookkeeping.
[ ] Simulation jobs/schedules/memory/attention/facts/proposal-commit/shutdown audit complete; `EpidemicRuntimeSimulationTests` Debug/Release green; module = `LOCAL_READY`.

## 3.10. Physics

[ ] `G3-PHYS-001`: зафиксировать acceptance/retry semantics Tick(delta) после partial fixed-step progress.
[ ] Physics shape/body ownership/fixed-step/backend-sync/projection/contact/query/shutdown audit complete; `EpidemicRuntimePhysicsTests` Debug/Release green; module = `LOCAL_READY`.

## 3.11. Navigation

[ ] `G3-NAV-001`: byte-budget считать по фактически подготавливаемому path shape.
[ ] `G3-NAV-002`: не превращать local allocation failure после backend success в semantic path failure/re-execution.
[ ] `G3-NAV-003`: не выполнять purge до последующей fallible work-list preparation.
[ ] Navigation tile/revision/query/provider/backend/budget audit complete; `EpidemicRuntimeNavigationTests` Debug/Release green; module = `LOCAL_READY`.

## 3.12. Animation

[ ] `G3-ANIM-001`: убрать pose publication после authoritative animator commit.
[ ] `G3-ANIM-002`: исключить silent loss semantic animation events при allocation failure.
[ ] Animation registry/animator/evaluation/pose/event audit complete; `EpidemicRuntimeAnimationTests` Debug/Release green; module = `LOCAL_READY`.

## 3.13. Audio

[ ] `G3-AUDIO-001`: зафиксировать backend retry/commit semantics без добавления transaction API.
[ ] `G3-AUDIO-002`: preflight cleanup ownership до CreateVoice; failed rollback не теряет backend voice.
[ ] Audio sound/emitter/listener/backend/fade/mixer/event/resource/shutdown audit complete; `EpidemicRuntimeAudioTests` Debug/Release green; module = `LOCAL_READY`.

## 3.14. Environment

[ ] Environment region/surface/update/snapshot/projection audit complete; `EpidemicRuntimeEnvironmentTests` Debug/Release green; module = `LOCAL_READY`.

## 3.15. Renderer

[ ] `G3-REN-001`: failed AbortFrame сохраняет recovery ownership и блокирует новый BeginFrame до reconciliation.
[ ] Renderer proxy/view/resource/scene/pose/frame/abort/shutdown audit complete; `EpidemicRuntimeRendererTests` Debug/Release green; module = `LOCAL_READY`.

## 3.16. Persistence

[ ] `G3-PERS-001`: сделать InMemoryPersistenceBackend::CommitSnapshot strong-commit.
[ ] Persistence transaction/candidate/backend `CommitSnapshot(durability)`/query/snapshot audit complete без добавления несуществующих `Save()`/`Flush()` APIs; `EpidemicRuntimePersistenceTests` Debug/Release green; module = `LOCAL_READY`.

## 3.17. Support

[ ] `G3-SUP-001`: Scene projection retry не повторяет уже successful prefix.
[ ] `G3-SUP-002`: coordinator сохраняет accepted frame prefix при late local allocation failure.
[ ] `G3-SUP-003`: зафиксировать atomic failure semantics IRuntimeEventSink и reference sink.
[ ] `G3-SUP-004`: закрыть post-acquire ResourceLease publication gaps Animation/Audio adapters.
[ ] `G3-SUP-005`: сделать audio wrapper lease cleanup действительно no-throw и durably retryable.
[ ] `G3-SUP-006`: reference audio backend не потребляет voice ID до publication commit.
[ ] `G3-SUP-007`: сделать dual-index animation pose publication атомарной.
[ ] Runtime composition prepare/commit, profiles и `registered_majors` audit complete.
[ ] Все standard cross-major adapters закрыты isolated fake-based tests и ownership/retry contracts.
[ ] Coordinator Tick order, phase failure semantics, event publication и result atomicity закрыты.
[ ] Coordinator Shutdown order, best-effort cleanup и retry completion закрыты.
[ ] `EpidemicRuntimeSupportTests` Debug/Release green; Support = `LOCAL_READY`.

## 3.18. Runtime-wide gate

[ ] `17/17 EngineRuntime production modules = LOCAL_READY`.
[ ] Runtime Architecture, Integration, Regression и Support reference/preflight smoke проходят.
[ ] Runtime не зависит от Framework; majors не получили новые peer dependencies вместо Support adapters.
[ ] Все найденные defects имеют permanent regressions; disabled/skipped tests без explicit freeze exception отсутствуют.

## 3.19. Финальный выход Goal 3

[ ] LOCAL_READY ledger содержит PASS/N/A evidence по всем применимым 37 criteria для всех 17 Runtime modules.
[ ] Runtime API inventory/public surface/dossiers/evidence anchors актуальны; `UNCLASSIFIED = 0`.
[ ] Base Debug/Release, Runtime Debug/Release и Full Debug/Release проходят после всех Runtime fixes; exact CTest manifests перегенерированы под фактическое число tests.
[ ] Public-header consumers, architecture/freeze validators и все validator negative/self-tests зелёные.
[ ] `git diff --check`, warnings-as-errors и remote CI на recorded SHA зелёные.

Критерий выхода: `EngineBase = 9/9 LOCAL_READY`, `EngineRuntime = 17/17 LOCAL_READY`, Goal 3 = `COMPLETE`. `SYSTEM_READY/FROZEN` не присваивается до Goals 5–9.

---
# Цель 4. Полный локальный freeze-аудит EngineFramework

Framework проверяется после EngineBase и EngineRuntime, потому что он опирается на уже подтвержденные lower-layer contracts.

Порядок Framework внутри первого круга фиксирован:

1. BaseInfrastructure.
2. RuntimeBoundary.
3. GameplayWorldStateOwners.
4. IntegrationLayer.

`LOCAL_READY` IntegrationLayer доказывается на fakes/ports соседних owners. Реальные causal chains проверяются позже, в цели 7.

## 4.A. Framework BaseInfrastructure и RuntimeBoundary, 6 модулей

### 4.A.1. Foundation

[ ] Все gameplay IDs, refs, tags и handles имеют однозначную invalid/stale semantics.

[ ] IdGenerator snapshot/restore и exhaustion.

[ ] ChangeCursor ordering и stale cursor behavior.

[ ] TypeRegistry duplicate registration.

[ ] TypeRegistry freeze.

[ ] GameplayContext не создаёт hidden ownership.

[ ] Gameplay time/value types boundary arithmetic.

### 4.A.2. SupportRandom

[ ] Один seed и одна sequence дают одинаковые значения.

[ ] Snapshot/restore random sequence продолжает тот же stream.

[ ] Разные streams не делят hidden mutable state.

[ ] Boundary ranges не имеют modulo bias, если contract обещает uniform result.

[ ] Invalid range отклоняется.

[ ] Randomness не зависит от wall clock после создания stream.

### 4.A.3. Time

[ ] Clock registration/state.

[ ] Schedule create/update/remove.

[ ] Recurrence daily/calendar/custom boundaries.

[ ] Catch-up policies.

[ ] Large time jump.

[ ] Duplicate schedule identity.

[ ] Cancel terminal semantics.

[ ] Trigger order deterministic при одинаковом timestamp.

[ ] Scheduler budget exhaustion deferred, без потери triggers.

[ ] Journal/cursor consistency.

[ ] Snapshot/restore clocks, schedules, generators, cursor.

### 4.A.4. Queries

[ ] Provider registration, duplicate и freeze.

[ ] Query type mismatch.

[ ] Consistency/coverage/accuracy requirements.

[ ] Budget exhaustion.

[ ] Snapshot coordinator epoch.

[ ] Provider failure не оставляет partial response state.

[ ] Deterministic provider order.

[ ] Detached query response lifetime.

[ ] Diagnostics не является authoritative state.

### 4.A.5. Facts

[ ] Fact assert/update/remove lifecycle.

[ ] Event publication.

[ ] History policy.

[ ] Transaction commit/rollback.

[ ] Duplicate fact/event identities.

[ ] Type registration и freeze.

[ ] Subscriber reentrancy.

[ ] Subscriber failure policy.

[ ] Journal/history cursor.

[ ] Compaction не удаляет необходимый retained history.

[ ] Snapshot/restore persistent facts, events, generator and cursor.

[ ] Failed transaction или allocation не публикует half-state.

### 4.A.6. RuntimeBridge

[ ] Framework semantic object создаёт ровно одну runtime materialization.

[ ] Runtime handle/generation map consistency.

[ ] Create/update/remove commands.

[ ] Retry после runtime backend failure.

[ ] Stale runtime observation не применяется к новому framework generation.

[ ] Duplicate command/event delivery idempotent или rejected по контракту.

[ ] Dematerialization cleanup.

[ ] Queue ordering.

[ ] Backpressure/budget.

[ ] Snapshot/reconciliation state, если bridge хранит persistent checkpoint.

### 4.A.7. Exit criteria

[ ] 6 из 6 модулей имеют `LOCAL_READY`.

[ ] Ни один gameplay owner не используется для доказательства локальной корректности этих шести модулей, кроме test fake/port.

---

## 4.B. Framework GameplayWorldStateOwners, 33 модуля

Для каждого state owner особенно обязательны: полный inventory authoritative state, все indexes, generators, revisions, journals, snapshot/restore, lifecycle, failure atomicity и mutation API coverage.

### 4.B.1. Entities

[ ] Entity create/remove/deactivate/convert lifecycle.

[ ] Archetype and part references.

[ ] Tags and parts indexes.

[ ] Stale entity/part references.

[ ] Convert сохраняет только разрешённые fields.

[ ] Pruning history не повреждает latest cursor.

[ ] Snapshot/restore records, indexes, generators, revisions, journal.

### 4.B.2. Materials

[ ] Material/substance/type registration.

[ ] Composition validation и normalization.

[ ] Slot assignment.

[ ] Stimulus/exposure mutation.

[ ] Cross-reference validity.

[ ] Derived material response соответствует primary composition.

[ ] Journal prune boundary.

[ ] Snapshot/restore.

### 4.B.3. Conditions

[ ] Definition registration и freeze.

[ ] Apply/remove.

[ ] Stacking policies.

[ ] Persistence/materialization policies.

[ ] Periodic catch-up.

[ ] Expiration.

[ ] Subject provider failure.

[ ] Duplicate condition handling.

[ ] Snapshot/restore instances, generators, revisions, journal.

### 4.B.4. Effects

[ ] Definition/handler registration и freeze.

[ ] Immediate execution.

[ ] Deferred execution.

[ ] Cancel deferred.

[ ] Cancel deferred targeting.

[ ] Take deferred by schedule.

[ ] Multi-operation batch prepare/commit.

[ ] Handler prepare failure.

[ ] Handler commit failure.

[ ] Target state provider failure.

[ ] Execution budget.

[ ] Journal pruning.

[ ] Snapshot/restore deferred queue, IDs, revisions and journal.

### 4.B.5. Interaction

[ ] Interaction definition/session/request lifecycle.

[ ] Start/advance/complete/cancel.

[ ] Preconditions and state provider.

[ ] Executor success/failure.

[ ] Timeout/sweep.

[ ] Duplicate execution prevention.

[ ] Reservation/pending state rollback.

[ ] Snapshot/restore active interactions and pending execution state.

### 4.B.6. ItemsInventory

[ ] Item definition and container registration.

[ ] Item create/remove.

[ ] Stack split/merge rules.

[ ] Capacity and slot limits.

[ ] Transfer within/between containers.

[ ] Reservation create/consume/release.

[ ] Prepare/commit/cancel transfer.

[ ] Exchange reservations.

[ ] Durability and charges boundaries.

[ ] World/container bindings.

[ ] Secondary container/item indexes.

[ ] Failed transfer leaves source and target unchanged.

[ ] Snapshot/restore items, containers, reservations, bindings, generators, journal.

### 4.B.7. Equipment

[ ] Equipment slot definition.

[ ] Equip/unequip.

[ ] Prepare equip.

[ ] Item provider reservation.

[ ] Conflicting slots.

[ ] Requirements and compatibility.

[ ] Reconcile restored bindings.

[ ] Exchange/reconcile reservations.

[ ] Provider failure before and after prepare.

[ ] Snapshot/restore equipment and external binding metadata.

### 4.B.8. Ownership

[ ] Owner/property identity.

[ ] Assign/transfer/remove ownership.

[ ] Duplicate ownership prevention.

[ ] Shared/fractional semantics, если предусмотрены.

[ ] Stale owner/property references.

[ ] Transfer atomicity.

[ ] Journal/revision.

[ ] Snapshot/restore.

### 4.B.9. Loot

[ ] Loot table/definition registration.

[ ] Generate deterministic output from explicit random source.

[ ] Generated, pending, delivered, cancelled lifecycle.

[ ] Reward handler prepare/commit/cancel.

[ ] Discard generated.

[ ] Duplicate delivery protection.

[ ] External reward failure and reconciliation.

[ ] Snapshot/restore pending deliveries, generators, journal.

### 4.B.10. Economy

[ ] Currency/account registration.

[ ] Funds reservation lifecycle.

[ ] Monetary transfer.

[ ] Offer lifecycle.

[ ] Trade transaction lifecycle.

[ ] Debt and contract lifecycle.

[ ] Price provider failure.

[ ] No negative/overflow balance unless explicitly allowed.

[ ] Reservation prevents double spending.

[ ] Transfer failure leaves both accounts unchanged.

[ ] Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.

### 4.B.11. Processes

[ ] Process definition/execution lifecycle.

[ ] Input reservations.

[ ] Output prepare/commit.

[ ] Cancellation.

[ ] Provider failure.

[ ] Partial input/output failure rollback.

[ ] Process scheduling/state transitions.

[ ] Snapshot/restore executions, reservations, generators, journal.

### 4.B.12. ResourcesProduction

[ ] Resource definitions/stores/producers.

[ ] Production reservation.

[ ] Consume/produce atomicity.

[ ] Capacity and quantity boundaries.

[ ] Negative/overflow quantities rejected.

[ ] Producer lifecycle.

[ ] Process integration ports remain external.

[ ] Snapshot/restore.

### 4.B.13. RolesJobs

[ ] Role/job definitions.

[ ] Assignment/unassignment.

[ ] Capacity/eligibility rules.

[ ] Worker/job stale references.

[ ] Duplicate assignment.

[ ] Job lifecycle.

[ ] Failed reassignment leaves old assignment valid.

[ ] Snapshot/restore.

### 4.B.14. NeedsLife

[ ] Need definitions/state.

[ ] Satisfy/decay boundaries.

[ ] Life pressure lifecycle.

[ ] Expiration sweep.

[ ] Resolve pressure.

[ ] Simulation interval including large delta.

[ ] Terminal pressure pruning.

[ ] No negative/overflow need values unless contract allows.

[ ] Snapshot/restore.

### 4.B.15. Population

[ ] Population group/member lifecycle.

[ ] Counts and membership indexes.

[ ] Spawn/despawn semantic state.

[ ] Inactive/active population transitions.

[ ] Capacity and aggregate consistency.

[ ] Stale member references.

[ ] Snapshot/restore groups, members, generators, journal.

### 4.B.16. Society

[ ] Social groups/relations/reputation definitions.

[ ] Relationship mutation boundaries.

[ ] Membership lifecycle.

[ ] Symmetric/asymmetric relationship semantics.

[ ] Duplicate relation prevention.

[ ] Social change journal.

[ ] Snapshot/restore.

### 4.B.17. Crime

[ ] Law/jurisdiction/authority registration.

[ ] Crime candidate evaluation.

[ ] Crime record lifecycle.

[ ] Evidence and witness lifecycle.

[ ] Proof state transitions.

[ ] Bounty lifecycle.

[ ] Response request lifecycle.

[ ] Invalid jurisdiction/authority reference.

[ ] Duplicate evidence/witness behavior.

[ ] Snapshot/restore all legal records and journal.

### 4.B.18. Perception

[ ] Observer/source/target registration.

[ ] Observation lifecycle.

[ ] Visibility/sense result validation.

[ ] Duplicate observation coalescing policy.

[ ] Expiration/forgetting.

[ ] Provider failure.

[ ] Deterministic ordering where observations share timestamp.

[ ] Snapshot/restore retained perception state.

### 4.B.19. Knowledge

[ ] Knowledge learn/share/forget/contradict lifecycle.

[ ] Confidence/value boundaries.

[ ] Memory compaction.

[ ] Subject/source references.

[ ] Duplicate knowledge semantics.

[ ] Contradiction does not corrupt indexes.

[ ] Decay.

[ ] Graph/index consistency.

[ ] Snapshot/restore memories, relations, generators, journal.

### 4.B.20. AI

[ ] Profile/goal/intent/consideration registration.

[ ] Agent register/remove.

[ ] Blackboard key/value type validation.

[ ] Goal/intent lifecycle.

[ ] Think/replan.

[ ] Evaluator failure.

[ ] Access policy.

[ ] Target candidate handling.

[ ] SetNextThink scheduling.

[ ] Deterministic tie breaking.

[ ] Failed think does not half-update agent/index/journal.

[ ] Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

### 4.B.21. Abilities

[ ] Definition/instance grant/remove.

[ ] Availability checks.

[ ] Activation lifecycle.

[ ] Target policy.

[ ] Cost reservation.

[ ] Cooldown group.

[ ] Requirement provider failure.

[ ] Resource provider prepare/commit/release.

[ ] Materialization provider failure.

[ ] Execution cancel/complete.

[ ] Duplicate delivery prevention.

[ ] Snapshot/restore instances, executions, reservations, cooldowns, generators, journal.

### 4.B.22. Progression

[ ] Track/level definition.

[ ] Grant/reserve/commit/cancel progression.

[ ] XP/value overflow.

[ ] Level boundary.

[ ] Duplicate reward prevention.

[ ] Reservation lifecycle.

[ ] Snapshot/restore.

### 4.B.23. Combat

[ ] Combatant registration/removal.

[ ] Engagement lifecycle.

[ ] Damage plan/resolve.

[ ] Modifier ordering.

[ ] Resource reservation.

[ ] Resource depletion transitions alive/downed/dead/disabled.

[ ] Clamp/overflow behavior.

[ ] Modifier provider failure.

[ ] Failed resolution does not consume reservation or change resource.

[ ] Snapshot/restore combatants, reservations, generators, journal.

### 4.B.24. Encounters

[ ] Definition/spawn table registration.

[ ] Encounter start/active/terminal lifecycle.

[ ] Spawn request lifecycle.

[ ] Spawn point state.

[ ] Spawned entity record consistency.

[ ] Respawn rules.

[ ] Budget limits.

[ ] Fail encounter.

[ ] Despawn lifecycle.

[ ] Terminal pruning.

[ ] Deterministic spawn roll via explicit random source.

[ ] Snapshot/restore.

### 4.B.25. Narrative

[ ] Thread lifecycle.

[ ] Objective lifecycle.

[ ] Clue discovery.

[ ] Choice resolve/cancel.

[ ] Consequence prepare/commit/failure.

[ ] Suspend/resume/fail thread.

[ ] Duplicate consequence delivery prevention.

[ ] Terminal execution compaction.

[ ] Cursors/checkpoints.

[ ] Snapshot/restore threads, objectives, choices, deliveries, generators, journal.

### 4.B.26. Dialogue

[ ] Conversation definition/session lifecycle.

[ ] Participant validation.

[ ] Condition resolver success/failure.

[ ] Option repeat policy.

[ ] Consequence handler success/failure.

[ ] Node transition validity.

[ ] Conversation terminal state.

[ ] Duplicate consequence execution prevention.

[ ] Snapshot/restore sessions and journal.

### 4.B.27. World

[ ] Feature/area/placement create/update/remove.

[ ] Alteration lifecycle.

[ ] Spatial index consistency.

[ ] Transaction commit/cancel.

[ ] Alteration ID generator staging.

[ ] Invalid enum rejection.

[ ] Large alteration index.

[ ] Journal atomicity.

[ ] Snapshot restore builds all indexes off-state.

[ ] Failed restore leaves old world unchanged.

### 4.B.28. Environment

[ ] Gameplay environment definitions and state.

[ ] Region/area references.

[ ] Weather/environment semantic mutation.

[ ] Expiration/sweep.

[ ] Derived queries match authoritative state.

[ ] No duplicate semantic state against Runtime Environment.

[ ] Snapshot/restore.

### 4.B.29. Traversal

[ ] Traversal capability/route/action definitions.

[ ] Begin/progress/complete/cancel lifecycle.

[ ] Capability validation.

[ ] Cost/reservation semantics.

[ ] Stale world/entity references.

[ ] Failure leaves traversal state unchanged.

[ ] Snapshot/restore.

### 4.B.30. NavigationSemantics

[ ] Semantic area/layer/rule registration.

[ ] Registry freeze.

[ ] Capability provider failure.

[ ] Route/query semantic constraints.

[ ] Layer create/update/remove.

[ ] Invalid world references.

[ ] Deterministic rule resolution.

[ ] Snapshot/restore semantic graph/indexes.

### 4.B.31. Construction

[ ] Placement rules/recipes registration.

[ ] Placement validation.

[ ] Plan lifecycle.

[ ] Cost reservation.

[ ] Socket reservation.

[ ] Site lifecycle.

[ ] Commit/cancel placement.

[ ] Output creation.

[ ] Provider failure.

[ ] Collision/conflicting socket rejection.

[ ] Snapshot/restore plans, sites, reservations, generators, journal.

### 4.B.32. Simulation

[ ] Framework simulation definitions/state.

[ ] Job/task registration and lifecycle.

[ ] Budgeted update.

[ ] Proposal/application boundary to state owners.

[ ] No duplicate authoritative state with Runtime Simulation.

[ ] Large delta/catch-up.

[ ] Failure atomicity.

[ ] Snapshot/restore.

### 4.B.33. SaveGame

[ ] Participant registration.

[ ] Duplicate participant IDs.

[ ] Registration freeze.

[ ] Save staging.

[ ] Participant capture failure.

[ ] Serialization failure.

[ ] Store failure.

[ ] Load read failure.

[ ] Participant validate failure.

[ ] Restore ordering.

[ ] Failed participant restore does not leave previously restored participants committed without reconciliation policy.

[ ] Version/migration behavior.

[ ] Persistent/session separation.

[ ] Whole-framework snapshot metadata.

### 4.B.34. Exit criteria GameplayWorldStateOwners

[ ] 33 из 33 state owners имеют `LOCAL_READY`.

[ ] Для каждого owner существует authoritative state inventory.

[ ] Для каждого snapshot owner есть roundtrip, corruption и failed restore atomicity tests.

[ ] Для каждого multi-container/external transaction API есть failure atomicity test.

[ ] Для каждого найденного defect есть regression test.

---

## 4.C. Framework IntegrationLayer, 13 модулей

Главный принцип: IntegrationLayer не становится вторым owner state соседних majors. Он может хранить mapping, cursor, checkpoint, reservation и reconciliation state, необходимый именно для доставки между owners.

Для каждого integration module обязательно проверить:

[ ] Exactly-once или явно зафиксированную at-least-once semantics.

[ ] Duplicate input.

[ ] Stale generation.

[ ] Stale revision/cursor.

[ ] Source success и target failure.

[ ] Retry.

[ ] Cancel/rollback.

[ ] Restore checkpoint и resync.

[ ] Не возникает feedback loop.

[ ] Delivery order deterministic.

### 4.C.1. Integration

[ ] FactsQueryAdapter query mapping.

[ ] RuntimeTimeAdapter time projection.

[ ] ScheduledTriggerDispatcher registration/freeze.

[ ] Trigger handler failure.

[ ] Delivery modes.

[ ] Duplicate trigger prevention.

[ ] Dispatcher checkpoint/save participant.

[ ] TimeFactsAdapter exactly-once event projection.

### 4.C.2. GameplayIntegration

[ ] Combat -> Effects damage delivery.

[ ] Progression -> Combat modifier mapping.

[ ] Combat <-> Ability resource reservation.

[ ] Ability -> Effects delivery.

[ ] Conditions -> Progression.

[ ] Ability -> Time scheduled trigger.

[ ] Ability output coordinator.

[ ] Loot -> Progression rewards.

[ ] Death reward delivery.

[ ] Prepare/commit/release failures на каждом внешнем leg.

[ ] Checkpoint restore без duplicate rewards/effects.

### 4.C.3. ExtendedGameplayIntegration

[ ] Equipment <-> Items reservation/ownership.

[ ] Processes <-> Items input/output.

[ ] Dialogue -> Knowledge consequence.

[ ] Economy/Items/Ownership coordinated trade.

[ ] Second prepare failure rollback.

[ ] Money leg committed, goods leg failed reconciliation.

[ ] Goods leg committed, ownership leg failed reconciliation.

[ ] Cancel path.

[ ] Restore trade execution checkpoint.

[ ] Повторный retry не дублирует transfer.

### 4.C.4. InteractionEffectsIntegration

[ ] Interaction completion -> Effects exactly once.

[ ] Effect prepare failure.

[ ] Effect commit failure.

[ ] Reconciliation state.

[ ] Duplicate interaction completion.

[ ] Restore delivery records.

### 4.C.5. InteractionTimeIntegration

[ ] Interaction -> scheduled time binding.

[ ] Cancel interaction removes/invalidates schedule по контракту.

[ ] Trigger after interaction terminal state.

[ ] Duplicate schedule delivery.

[ ] Reconciliation after restore.

### 4.C.6. NarrativeIntegration

[ ] Narrative semantic event mapping.

[ ] Contract registry duplicate/freeze.

[ ] Narrative external consequence outbox.

[ ] Handler failure and retry.

[ ] Save participant roundtrip.

[ ] Knowledge -> Narrative reference.

[ ] Duplicate external consequence prevention.

### 4.C.7. PerceptionKnowledgeAIIntegration

[ ] Observation -> Knowledge mapping.

[ ] Duplicate observation.

[ ] Knowledge update -> AI inputs.

[ ] Missing knowledge.

[ ] AI execution availability.

[ ] Intent execution record exactly once.

[ ] Stale observation/agent generation.

### 4.C.8. PopulationSimulationIntegration

[ ] Population backed encounter planning.

[ ] Spawn success/failure.

[ ] Encounter termination updates population once.

[ ] Roles -> Needs mapping.

[ ] Population lifecycle reconciliation.

[ ] City life scheduled update.

[ ] Snapshot restore plans/checkpoints.

### 4.C.9. ProcessResourceSimulationIntegration

[ ] Processes <-> resource input reservation.

[ ] Output prepare/commit.

[ ] Provider failure.

[ ] Resource quantity rollback.

[ ] Simulation layer proposal/application.

[ ] Duplicate simulation execution.

[ ] Restore checkpoint/reconciliation.

### 4.C.10. SocialLegalIntegration

[ ] Ownership -> Crime theft resolution.

[ ] Crime -> Society relationship consequence.

[ ] Authority response mapping.

[ ] Duplicate crime input.

[ ] Victim policy.

[ ] Failed consequence delivery.

[ ] Checkpoint restore без duplicate penalty.

### 4.C.11. StateIntegration

[ ] Entity/material/condition queries.

[ ] State -> Facts projection.

[ ] Entity create/convert/tag effects.

[ ] Material stimulus/exposure effects.

[ ] Condition apply/remove effect delivery.

[ ] Entity target state provider.

[ ] Lifecycle adapter.

[ ] State <-> Time processing.

[ ] Deferred effect reconciliation.

[ ] Combined checkpoint and persistence.

[ ] Duplicate event/effect protection.

### 4.C.12. TraversalNavigationConstructionIntegration

[ ] Traversal capability -> NavigationSemantics.

[ ] Navigation layer changes from Construction.

[ ] Construction -> Traversal availability.

[ ] Construction rollback removes staged navigation/traversal consequences.

[ ] Duplicate construction completion.

[ ] Stale world/placement reference.

### 4.C.13. WorldIntegration

[ ] World query adapters.

[ ] World -> Facts projection.

[ ] Environment sample mapping.

[ ] Entity -> Interaction state provider.

[ ] Alteration create/update/remove projection.

[ ] Duplicate world change.

[ ] Stale cursor.

[ ] Restore checkpoint and resync.

### 4.C.14. Exit criteria IntegrationLayer

[ ] 13 из 13 integration modules имеют `LOCAL_READY`.

[ ] Каждый adapter доказан отдельно с fakes соседних owners.

[ ] Ни один integration test не скрывает defect одного owner с помощью другого real owner.

[ ] Все checkpoint/save participants имеют restore and duplicate-delivery regression tests.

---

# Цель 5. Доказать persistence и determinism всего движка

Цель 5 начинается после того, как соответствующие state owners имеют `LOCAL_READY`.

## 5.1. Полная карта persistent state

Для Base, Runtime и Framework составить одну таблицу:

[ ] Что сохраняется.

[ ] Кто authoritative owner.

[ ] Что является transient и должно быть пересоздано.

[ ] Snapshot type/version.

[ ] Dependencies при restore.

[ ] Migration policy.

[ ] Reconciliation после restore.

[ ] Deterministic state, влияющий на будущее поведение.

Ни один persistent owner не должен существовать только "по факту наличия CaptureSnapshot". Он должен присутствовать в карте.

## 5.2. Локальный persistence gate каждого state owner

Для каждого snapshot owner:

[ ] Non-empty realistic roundtrip.

[ ] Empty-state roundtrip.

[ ] Snapshot после нескольких create/update/remove cycles.

[ ] ID generator state сохраняется.

[ ] Generation сохраняется.

[ ] Revision сохраняется.

[ ] Journal epoch/sequence/cursor сохраняются по контракту.

[ ] Pending queues/reservations/reconciliation records сохраняются, если они persistent.

[ ] Transient runtime handles не сериализуются как reusable identity.

[ ] Corrupted enum rejected.

[ ] Corrupted reference rejected.

[ ] Duplicate ID rejected.

[ ] Missing required data rejected.

[ ] Optional missing data обрабатывается по version contract.

[ ] Old supported version мигрирует.

[ ] Unsupported version отклоняется.

[ ] Allocation failure during restore оставляет live state неизменным.

[ ] Provider/backend failure во время post-restore reconciliation имеет controlled outcome.

## 5.3. Ordered whole-engine restore

Создать отдельный whole-engine restore harness.

Он обязан проверять:

[ ] Создание state сразу в Framework, Runtime и boundary mappings.

[ ] Save всех persistent participants.

[ ] Полное уничтожение runtime/framework instances.

[ ] Boot нового Base/Runtime composition.

[ ] Restore в dependency order.

[ ] Rebuild derived indexes.

[ ] Rebuild RuntimeBridge mappings.

[ ] Re-materialization Runtime objects только после semantic state.

[ ] Resume Time/Simulation после завершения restore.

[ ] Продолжение исходного causal scenario.

[ ] Один participant намеренно падает на validation.

[ ] Один participant падает во время staging.

[ ] Один participant падает на post-restore reconciliation.

[ ] После каждого failure выполняется заранее определенная rollback/recovery policy.

Нельзя считать whole-engine restore проверенным через сохранение только Entities и Progression.

## 5.4. Deterministic scenario runner

Добавить test utility со входом:

`seed + initial snapshot + ordered input stream + tick count`

Выход:

`semantic final-state hash + ordered externally observable event trace`

Runner не должен hash-ить pointer addresses, allocator addresses или unordered serialization artifacts.

## 5.5. Determinism matrix

Проверить:

[ ] Unordered map/set iteration не определяет observable order.

[ ] Tie-breaking всегда explicit.

[ ] Stable IDs не зависят от pointer/address.

[ ] Randomness идет через explicit streams.

[ ] Random streams входят в snapshot, если влияют на future state.

[ ] Runtime/gameplay clock не использует wall clock для deterministic state.

[ ] Event queues имеют однозначный order.

[ ] Same timestamp schedules имеют stable order.

[ ] Task completion не определяет deterministic commit order там, где completion может меняться.

[ ] Serialization output имеет stable semantic ordering там, где это требуется для hash/replay.

[ ] Floating-point sensitive paths имеют documented tolerance либо deterministic representation.

## 5.6. Обязательные deterministic tests

[ ] Один и тот же scenario запускается 10 раз в одном executable/process model.

[ ] Hash и event trace идентичны.

[ ] Повторить с save/load в середине.

[ ] Повторить с stream-out/stream-in региона.

[ ] Повторить с deferred work/budget exhaustion.

[ ] Повторить минимум один combat/effects scenario.

[ ] Повторить минимум один AI/perception scenario.

[ ] Повторить минимум один economy/process scenario.

## 5.7. Exit criteria цели 5

[ ] 100% persistent owners присутствуют в persistence map.

[ ] 100% snapshot owners имеют roundtrip, corruption и failed-restore tests.

[ ] Whole-engine restore suite проходит.

[ ] Whole-engine deterministic runner проходит повторные runs.

[ ] Save/load не меняет deterministic continuation.

# Цель 6. Доказать memory/lifetime и concurrency/async safety

Эта цель закрывает классы дефектов, которые локальные functional tests обычно не обнаруживают.

## 6.1. Threading contract на каждый модуль

Для каждого из 78 dossiers выбрать ровно одну основную модель либо явную комбинацию:

- single-thread only;
- owner-thread only;
- multiple readers;
- single writer;
- externally synchronized;
- internally synchronized;
- asynchronous worker plus main-thread commit.

Нельзя оставлять threading contract неописанным.

## 6.2. Ownership и callback lifetime

Для каждого callback/subscription/task/provider:

[ ] Кто владеет registration token.

[ ] Кто может отменить callback.

[ ] Может ли callback прийти после shutdown owner.

[ ] Как гарантируется отсутствие use-after-free.

[ ] Reentrant callback разрешен или запрещен.

[ ] Callback exception policy определена.

[ ] Pending completion после shutdown не mutates destroyed state.

## 6.3. Concurrency tests

Особое внимание:

### EngineBase

[ ] TaskScheduler submit/wait/cancel/shutdown races.

[ ] Self-wait/self-shutdown.

[ ] MainThreadDispatcher producer/consumer load.

[ ] EventBus publication/subscription races только если официально разрешены.

[ ] MemoryTracker concurrent accounting.

### EngineRuntime

[ ] Async resource loading cancellation.

[ ] Streaming demand/release races.

[ ] Resource lease lifetime during unload.

[ ] Renderer preparation versus shutdown по поддерживаемому contract.

[ ] Physics/backend callback lifetime.

[ ] Save/persistence operations против teardown, если разрешено.

[ ] Simulation proposal production versus commit.

### EngineFramework

Если gameplay owners officially single-threaded:

[ ] Тестами доказать rejection/documentation, а не изображать thread safety.

Для разрешенных async boundaries:

[ ] Query provider completion.

[ ] Integration delivery queue.

[ ] RuntimeBridge observation/application.

[ ] Save orchestration.

[ ] Deferred callbacks.

## 6.4. Sanitizer configurations

Добавить поддерживаемые конфигурации.

На Windows 11 минимум:

[ ] MSVC AddressSanitizer либо поддерживаемый Clang-cl ASan profile.

[ ] Undefined behavior checks, доступные выбранному toolchain.

Для race detection, если Windows toolchain недостаточен:

[ ] Отдельный portable/headless test slice на toolchain, где доступен ThreadSanitizer, либо эквивалентная специализированная race qualification.

Если platform slice мешает sanitizer build, testable lower modules должны иметь headless configuration. Нельзя просто удалить sanitizer gate.

## 6.5. Logical-retention tests

Проверить длительный steady-state:

[ ] Journals после prune bounded.

[ ] Histories bounded согласно policy.

[ ] Tombstones bounded.

[ ] Terminal records bounded.

[ ] Pending operations после completion не накапливаются.

[ ] Reconciliation queues очищаются после success.

[ ] Resource caches имеют eviction/budget.

[ ] Streaming records уходят после release.

[ ] RuntimeBridge mappings удаляются после semantic removal.

[ ] Subscriptions освобождаются.

[ ] Deferred callbacks не удерживают уничтоженные owners.

[ ] Temporary buffers не превращаются в persistent capacity growth без причины.

## 6.6. Repetition/lifetime scenarios

[ ] 1000+ create/destroy cycles ключевых object types.

[ ] 1000+ load/unload resource cycles.

[ ] Repeated stream-in/stream-out.

[ ] Repeated scene materialization/dematerialization.

[ ] Repeated save/load.

[ ] Repeated application/runtime startup/shutdown в допустимом process model.

[ ] Failed shutdown + retry cycles для external backends.

После выхода на steady state измеряемые retained records и live allocations не должны линейно зависеть от числа завершенных циклов.

## 6.7. Exit criteria цели 6

[ ] 78/78 modules имеют threading contract.

[ ] Sanitizer suite проходит на поддерживаемом subset.

[ ] Нет известных use-after-free/double-free/invalid-access defects.

[ ] Нет data race/deadlock/lost update в официально поддерживаемых scenarios.

[ ] Long-running retention scenarios bounded.

# Цель 7. Второй круг: проверить смысловые кластеры и причинные цепочки

Второй круг начинается только после `LOCAL_READY` всех участников соответствующей цепочки.

Каждая цепочка проверяется как самостоятельный system contract.

## 7.1. Общий checklist любой causal chain

[ ] Normal path.

[ ] Source rejection.

[ ] Failure каждого intermediate participant.

[ ] Final target failure.

[ ] Retry.

[ ] Duplicate delivery.

[ ] Stale generation.

[ ] Stale revision/cursor.

[ ] Cancellation.

[ ] Budget/deferred work.

[ ] Save/load посередине цепочки.

[ ] Reconciliation после restore.

[ ] Deterministic order.

[ ] Ни один adapter не становится вторым authoritative owner.

[ ] Не возникает feedback loop.

[ ] Успешный retry не повторяет уже committed side effect.

## 7.2. Boot/lifecycle cluster

`EngineBase Core -> Platform/Input/RHI -> Runtime Support -> Runtime majors -> Framework composition`

Проверить:

[ ] Happy startup.

[ ] Failure Base module initialization.

[ ] Failure Runtime preparation.

[ ] Failure Framework composition.

[ ] Reverse-order cleanup.

[ ] Failed external cleanup.

[ ] Retry shutdown.

[ ] No work accepted after terminal shutdown.

## 7.3. Time/simulation cluster

`Base frame -> Runtime Time -> Runtime Simulation -> Framework Time -> Framework Simulation -> scheduled gameplay`

[ ] Pause/resume.

[ ] Time scale.

[ ] Large delta.

[ ] Catch-up.

[ ] Budget exhaustion.

[ ] Same-time deterministic ordering.

[ ] Save/load between ticks.

## 7.4. Semantic world/runtime materialization cluster

`Framework World/Entities -> RuntimeBridge -> Runtime World/Scene -> Renderer/Physics/Audio -> observations -> Framework`

[ ] Create/materialize.

[ ] Update.

[ ] Dematerialize.

[ ] Stream-out.

[ ] Stream-in.

[ ] Runtime object disappears unexpectedly.

[ ] Stale runtime generation.

[ ] Backend failure.

[ ] Retry/reconciliation.

[ ] Save/load while materialized.

## 7.5. Streaming/resources/persistence cluster

`Runtime World -> Streaming -> Resources -> Persistence -> resident Runtime state`

[ ] Multiple demands.

[ ] Dependency load failure.

[ ] Budget exhaustion.

[ ] Cancellation.

[ ] Unload failure.

[ ] Successor demand during unload.

[ ] Persistent override.

[ ] Shutdown with outstanding work.

## 7.6. Presentation/resource cluster

`Assets -> Resources -> Animation/Audio/Renderer -> RHI`

[ ] Asset dependency graph.

[ ] Shared leases.

[ ] Animation resource lifetime.

[ ] Audio resource lifetime.

[ ] Renderer resource lifetime.

[ ] RHI resize/recreate.

[ ] Backend shutdown failure.

## 7.7. Physical world/movement cluster

`Framework Traversal/Construction/NavigationSemantics -> Runtime Navigation/Physics/Scene -> Framework`

[ ] Construction changes nav/traversal state.

[ ] Invalid placement.

[ ] Navigation rebuild failure.

[ ] Traversal cancellation.

[ ] Construction rollback.

[ ] Stale world placement.

[ ] Runtime observation reconciliation.

## 7.8. State/effects cluster

`Interaction -> Effects -> Conditions -> Entities/Materials -> Facts`

[ ] Immediate effect.

[ ] Deferred effect.

[ ] Cancelled deferred effect.

[ ] Handler failure.

[ ] Condition stacking/expiry.

[ ] Entity conversion/removal.

[ ] Material response.

[ ] Facts publication exactly once.

## 7.9. Items/economy/production cluster

`Loot -> ItemsInventory -> Equipment -> Ownership -> Economy -> Processes -> ResourcesProduction`

[ ] Reward generation.

[ ] Pending delivery.

[ ] Inventory reservation.

[ ] Transfer.

[ ] Equip/unequip.

[ ] Ownership transfer.

[ ] Trade.

[ ] Production input reservation.

[ ] Output commit.

[ ] Failure на каждом leg.

[ ] Save/load mid-transaction.

## 7.10. Perception/cognition cluster

`World/Environment/Entities -> Perception -> Knowledge -> AI -> action intent`

[ ] Observation.

[ ] Duplicate observation.

[ ] Knowledge contradiction/decay.

[ ] AI think/replan.

[ ] Target disappears.

[ ] Action becomes unavailable.

[ ] Retry without duplicate intent execution.

## 7.11. Population/social/legal cluster

`Population -> RolesJobs -> NeedsLife -> Society -> Crime -> Encounters`

[ ] Population lifecycle.

[ ] Assignment.

[ ] Needs pressure.

[ ] Social relation update.

[ ] Crime evidence/response.

[ ] Encounter spawn/despawn.

[ ] Stream-out/coarse simulation.

[ ] Restore reconciliation.

## 7.12. Combat/progression cluster

`Abilities -> Combat -> Effects/Conditions -> Loot -> Progression`

[ ] Ability resource reservation.

[ ] Damage.

[ ] Terminal death state.

[ ] Effects/conditions.

[ ] Reward exactly once.

[ ] Progression grant.

[ ] Reward delivery failure and retry.

## 7.13. Narrative cluster

`Dialogue -> Narrative -> Knowledge/Facts -> external consequences -> SaveGame`

[ ] Choice.

[ ] Consequence prepare/commit.

[ ] Handler failure.

[ ] Outbox/reconciliation.

[ ] Duplicate consequence prevention.

[ ] Save/load mid-thread.

## 7.14. Whole-engine save/load cluster

Полный causal scenario должен:

[ ] Boot.

[ ] Load/create world.

[ ] Create population.

[ ] Materialize region.

[ ] Spawn entities.

[ ] Execute interaction.

[ ] Execute combat.

[ ] Change inventory/equipment.

[ ] Update perception/knowledge/AI.

[ ] Run economy/process simulation.

[ ] Stream region away.

[ ] Run coarse simulation.

[ ] Save.

[ ] Shutdown.

[ ] Boot new engine.

[ ] Restore.

[ ] Stream region back.

[ ] Продолжить тот же causal scenario.

## 7.15. Exit criteria цели 7

[ ] Все обязательные cluster suites проходят.

[ ] Все участвующие modules получают `SYSTEM_READY`.

[ ] Нет duplicate/lost delivery.

[ ] Нет accepted stale generation/revision/cursor.

[ ] Нет неограниченного feedback loop.

[ ] Save/load не разрывает causal continuation.

# Цель 8. Провести failure, regression, load, scale и degradation qualification

Эта цель превращает функционально правильный движок в устойчивый фундамент для дальнейшего Tools/Reflection/Scripting.

## 8.1. Risk categories для public mutations

Каждый mutation contract классифицировать:

`A`: локальная простая mutation одного owner record.

`B`: mutation нескольких локальных structures/indexes/journal.

`C`: external participant, transaction, restore, async, cross-boundary или сложный lifecycle.

Требования:

### A

[ ] Success.

[ ] Invalid input.

[ ] Identity/lifecycle boundaries.

[ ] Invariants.

### B

Все A плюс:

[ ] Full failure atomicity.

[ ] Primary/index consistency.

[ ] Journal/revision/generator boundary.

[ ] Allocation failure, если mutation реально аллоцирует после начала state transition.

### C

Все B плюс:

[ ] Prepare/commit/cancel/rollback.

[ ] Callback/provider/backend failures.

[ ] Allocation fault sweep по fallible local preparation.

[ ] Full pre-state comparison.

[ ] Durable reconciliation и idempotent retry, если rollback не гарантирован.

## 8.2. Fault-injection rules

[ ] Каждый fault iteration начинается с fresh fixture.

[ ] Dry run не изменяет fixture, используемый следующей итерацией.

[ ] Fault injection выключается до capture/compare.

[ ] Проверяем engine failure, а не allocation test harness.

[ ] Каждая реально найденная failure boundary получает минимальный regression.

[ ] Нельзя требовать exhaustive allocation sweep от очевидного no-allocation `SetX`, если это не дает дополнительной гарантии.

## 8.3. Deliberate failures

Обязательный набор:

[ ] Allocation failure.

[ ] Serialization corruption.

[ ] Backend failure.

[ ] Provider failure.

[ ] Provider exception.

[ ] Callback exception.

[ ] Queue/budget overflow.

[ ] Missing resource.

[ ] Stale handle.

[ ] Revision overflow.

[ ] Generation exhaustion.

[ ] Destroy during pending operation.

[ ] Runtime object disappears.

[ ] Save during complex pending state.

[ ] Load after partial runtime teardown.

[ ] Shutdown cleanup failure.

## 8.4. Regression policy

[ ] Каждый подтвержденный defect сначала получает reproducible test.

[ ] Fix не принимается без прохода regression.

[ ] Test остается после исправления.

[ ] Regression test не должен зависеть от случайного fault index без сохраненного seed/index.

[ ] P0/P1 regression помечается отдельным label.

## 8.5. Load domains

Текущий Truth load остается baseline, но расширяется.

Обязательные profiles:

[ ] 100 000+ semantic entities.

[ ] 50 000+ inactive/coarse NPC records.

[ ] 20 000+ scheduled events.

[ ] 10 000+ active AI contexts в budgeted scenario.

[ ] 5 000+ perceivers с bounded work.

[ ] Large inventories и containers.

[ ] Large knowledge graphs.

[ ] Economy accounts/offers/trades.

[ ] Process/resource-production batches.

[ ] Population/roles/needs simulation.

[ ] RuntimeBridge queue pressure.

[ ] Streaming churn.

[ ] Materialize/dematerialize cycles.

[ ] Large journals до prune threshold.

## 8.6. Degradation contracts

Для каждой bounded subsystem определить:

[ ] Hard limit или budget.

[ ] Что происходит при достижении budget.

[ ] Что происходит при превышении.

Разрешенные формы:

- defer;
- coalesce;
- backpressure;
- controlled `budget_exceeded`;
- eviction по явной policy.

Запрещенные формы:

- crash;
- silent loss committed work;
- infinite queue growth;
- unbounded memory growth;
- frame-long uncontrolled full scan там, где заявлен budget.

## 8.7. Метрики

Load test должен собирать как минимум:

[ ] Total duration.

[ ] Per-stage duration.

[ ] Peak/live allocations, где доступно.

[ ] Queue depth.

[ ] Retained record counts.

[ ] Journal sizes.

[ ] Deferred work count.

[ ] Budget-exceeded count.

[ ] Optional frame/tick percentile latency для систем, работающих в tick.

Не требуется оптимизировать всё до финальной производительности игры. Требуется доказать отсутствие архитектурного развала и знать границы.

## 8.8. Exit criteria цели 8

[ ] 100% public mutations классифицированы A/B/C.

[ ] Все B/C contracts имеют соответствующее failure evidence.

[ ] Все найденные defects имеют regression.

[ ] High-cardinality profiles завершаются контролируемо.

[ ] Overload behavior соответствует documented degradation policy.

[ ] Нет P0/P1 defects.

# Цель 9. Финальная test saturation, whole-engine qualification и окончательный freeze

Это единственная цель, после которой разрешено зафиксировать `FROZEN`.

## 9.1. Test taxonomy

CI и локальные presets должны разделять:

- architecture;
- unit;
- module;
- integration;
- persistence;
- determinism;
- regression;
- fault;
- sanitizer;
- load;
- smoke;
- whole-engine qualification.

Каждый test должен иметь однозначный label.

## 9.2. Coverage требования

Line/branch coverage используется как detector, а не как единственный acceptance criterion.

Обязательное behavioral coverage:

[ ] 100% public mutation contracts найдены.

[ ] 100% public mutations классифицированы A/B/C.

[ ] 100% public mutations имеют happy-path test либо документированное доказательство pure forwarding contract.

[ ] 100% lifecycle transitions имеют success/rejection evidence.

[ ] 100% stale-handle capable APIs имеют stale identity tests.

[ ] 100% snapshot owners имеют roundtrip/corruption/failed-restore tests.

[ ] 100% B/C multi-state mutations имеют failure atomicity evidence.

[ ] 100% external ports имеют failure path test.

[ ] 100% integration delivery paths имеют retry/duplicate evidence.

[ ] 100% подтвержденных defects имеют regression test.

## 9.3. Build matrix

На Windows 11:

[ ] Clean Base Debug.

[ ] Clean Runtime Debug.

[ ] Clean Full Debug.

[ ] Clean Base Release.

[ ] Clean Runtime Release.

[ ] Clean Full Release.

[ ] Warnings-as-errors configuration для engine-owned targets.

[ ] Architecture validator включен во всех freeze configurations.

[ ] Public-header self-containment.

[ ] D3D11 smoke.

[ ] Headless/null backend smoke.

## 9.4. Sanitizer and special builds

[ ] ASan supported suite.

[ ] UB checks supported suite.

[ ] Race/concurrency qualification согласно цели 6.

[ ] Fault-injection configuration.

[ ] Determinism configuration с фиксированными seeds.

## 9.5. Flakiness gate

[ ] Каждый module suite 5 последовательных проходов.

[ ] Каждый cluster suite 10 последовательных проходов.

[ ] Deterministic suite 10 идентичных runs.

[ ] Full Debug suite 3 последовательных прохода.

[ ] Full Release suite минимум 2 последовательных прохода.

[ ] Randomized tests печатают seed.

[ ] Нет disabled/skipped freeze tests без documented exception.

## 9.6. Финальные whole-engine scenarios

Минимум пять независимых scenarios.

### Scenario A: полный игровой causal path

`boot -> world -> population -> materialize -> interactions -> combat -> inventory -> AI -> economy/process -> stream out -> save -> shutdown -> restore -> stream in -> continue`

### Scenario B: long simulation

[ ] Большой virtual time interval.

[ ] Multiple catch-up cycles.

[ ] Population/economy/process/needs.

[ ] Periodic prune/compaction.

[ ] Bounded memory.

### Scenario C: streaming churn

[ ] Repeated region load/unload.

[ ] Resource dependencies.

[ ] Runtime materialization.

[ ] PlayerTouched/persistent objects.

[ ] Cancellation и successor demands.

### Scenario D: destruction/materialization heavy

[ ] Many creates/removes.

[ ] Physics/scene/renderer lifecycle.

[ ] RuntimeBridge mapping churn.

[ ] Stale observations ignored.

[ ] Journals and tombstones remain bounded.

### Scenario E: repeated save/load

[ ] Save/load несколько раз внутри одного causal scenario.

[ ] Save во время pending external transaction.

[ ] Save после deferred work.

[ ] Restore и continuation.

[ ] Final semantic state совпадает с control run без промежуточных reload, если contract предполагает эквивалентность.

## 9.7. Финальный freeze report

Для каждого из 78 production-модулей:

[ ] `LOCAL_READY`.

[ ] `SYSTEM_READY`.

[ ] Persistence obligations PASS либо `N/A` с причиной.

[ ] Determinism obligations PASS либо `N/A`.

[ ] Lifetime obligations PASS.

[ ] Concurrency obligations PASS.

[ ] Load obligations PASS либо `N/A`.

[ ] Все P0/P1 закрыты.

[ ] Known P2/P3 не нарушают frozen public contract и явно перечислены.

[ ] Public API inventory совпадает с headers.

[ ] Threading contract актуален.

[ ] Snapshot contract актуален.

[ ] No temporary compatibility API остается публичным.

## 9.8. Freeze snapshot

После успешного qualification:

[ ] Зафиксировать public header/API snapshot.

[ ] Зафиксировать architecture allowlists.

[ ] Зафиксировать snapshot schema versions.

[ ] Зафиксировать test counts и test labels.

[ ] Зафиксировать reference performance/load metrics.

[ ] Зафиксировать compiler/toolchain baseline.

[ ] Обновить `Milestones.md` и убрать противоречивые старые статусы.

[ ] Создать freeze tag/commit.

После этого любое несовместимое изменение public contracts, ownership или persistence schema должно проходить отдельный unfreeze/change process.

## 9.9. Финальный критерий

Полный freeze разрешен только при одновременном выполнении:

```text
architecture = PASS
78/78 local module audits = PASS
persistence = PASS
determinism = PASS
memory/lifetime = PASS
concurrency/async = PASS
semantic clusters = PASS
failure/regression = PASS
load/degradation = PASS
sanitizers = PASS
whole-engine scenarios = PASS
known P0/P1 = 0
```

Итоговый статус:

`EngineBase = FROZEN`

`EngineRuntime = FROZEN`

`EngineFramework = FROZEN`

Только после этого базовые три слоя считаются достаточно стабильными для активного наращивания Tools, Reflection, Scripting и game-specific systems без регулярного возврата к фундаментальным контрактам.

---

# Порядок выполнения без двойственности

1. Сначала цель 1. Не начинать массовый audit, пока текущий build baseline и architecture gates не воспроизводятся.
2. Затем цель 2 целиком. EngineBase должен получить 9/9 `LOCAL_READY`.
3. Затем цель 3 целиком. EngineRuntime должен получить 17/17 `LOCAL_READY`.
4. Затем цель 4 целиком. EngineFramework должен получить 52/52 `LOCAL_READY`.
5. После первого круга выполнить цель 5: persistence и determinism.
6. Затем цель 6: lifetime и concurrency.
7. Затем цель 7: реальные смысловые и cross-layer chains.
8. Затем цель 8: failure/regression/load/degradation.
9. В конце цель 9: repeated full suites, whole-engine scenarios и freeze snapshot.

# Правила кампании freeze

- Не добавлять gameplay features в ходе freeze campaign, кроме минимально необходимых test seams или contracts.
- Не смешивать unrelated refactor с исправлением подтвержденного defect.
- Сначала regression, затем fix.
- Нельзя закрыть module audit только общим full test run.
- Нельзя закрыть integration defect изменением ownership так, чтобы adapter стал вторым state owner.
- Нельзя закрыть restore defect сравнением только revision или count.
- Нельзя закрыть multi-state transaction только happy-path test.
- Нельзя считать API покрытым простым упоминанием его имени в test source.
- Нельзя переносить старый `PASS` на измененный contract без повторного запуска соответствующего gate.
- После `LOCAL_READY` public contract можно менять только если defect требует исправления. Такое изменение обнуляет affected local/system evidence.
- После `FREEZE_READY` любые contract changes требуют повторного прохождения всех затронутых goals.
