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

Статус: `COMPLETE` (2026-09-22). Recorded SHA: `bc968d5c7e9e4ed4f221e67bcdde3696e41e0aad`; GitHub Architecture Freeze #14: `PASS`, включая обязательный `clang-public-surface`. EngineBase = `9/9 LOCAL_READY`, EngineRuntime = `17/17 LOCAL_READY`. Canonical evidence: `docs/freeze/` и `_goal3_handoff/block_A-D/`.

## 3.1. Закрытые module contracts

[x] RuntimeFoundation, Time и Serialization: deterministic math/time, persistence-complete clock checkpoint, atomic restore, migration и failure-atomicity.
[x] Resources, Assets и Streaming: registration/dependencies, durable retry ownership, budgets, cancellation, queue/state-machine и external-failure semantics.
[x] Scene, World и Simulation: identity/topology/TRS, placement/persistence tiers, jobs/schedules, proposal/commit, snapshot и shutdown contracts.
[x] Physics, Navigation и Animation: fixed-step/backend sync, queries/path budgets, animator/pose/event publication и deterministic observable order.
[x] Audio, Environment и Renderer: backend/resource lifetime, retry/recovery, regions/surfaces, frame/abort/shutdown semantics.
[x] Persistence и Support: strong snapshot commit, composition/adapters, coordinator Tick/Shutdown, event publication и cleanup ownership.

## 3.2. Подтверждённые исправления

[x] `G3-DOC-001`: Runtime module docs синхронизированы с ledger и больше не содержат преждевременных `frozen` claims.
[x] `G3-RF-001`, `G3-TIME-001`: согласованы TRS semantics; clock checkpoint сохраняет fractional remainder и восстанавливается атомарно.
[x] `G3-RES-001`, `G3-RES-002`, `G3-RES-003`, `G3-RES-004`: атомарная замена dependencies, сохранение retry/queue ownership и отделение callback failure от local finalization.
[x] `G3-STR-001`, `G3-STR-002`, `G3-SIM-001`, `G3-SIM-002`: successful external/job work не повторяется и не теряется из-за позднего local allocation/bookkeeping failure.
[x] `G3-PHYS-001`, `G3-NAV-001`, `G3-NAV-002`, `G3-NAV-003`: определён partial fixed-step retry; path budget, backend result publication и purge order сделаны корректными.
[x] `G3-ANIM-001`, `G3-ANIM-002`, `G3-AUDIO-001`, `G3-AUDIO-002`: устранены post-commit publication gaps, silent event loss и потеря retryable backend voice ownership.
[x] `G3-REN-001`, `G3-PERS-001`: failed frame abort сохраняет recovery ownership; `CommitSnapshot` имеет strong-commit semantics.
[x] `G3-SUP-001`, `G3-SUP-002`, `G3-SUP-003`, `G3-SUP-004`, `G3-SUP-005`, `G3-SUP-006`, `G3-SUP-007`: закрыты prefix retry, accepted-frame staging, event-sink atomicity, resource-lease cleanup, voice ID и dual-index pose publication.

## 3.3. Qualification и выход

[x] Все 17 Runtime modules прошли единый 37-criteria audit; каждый defect имеет permanent regression и evidence anchor, `UNCLASSIFIED = 0`.
[x] Base/Runtime/Full Debug и Release: `14/14`, `34/34`, `94/94`; суммарно `284/284`, exact CTest manifests совпадают.
[x] `/W4 /WX`, public-header consumers, architecture/freeze validators, negative/self-tests и `git diff --check` зелёные; disabled/skipped без explicit exception отсутствуют.
[x] Final baseline и remote CI подтверждены на recorded SHA; Runtime не зависит от Framework и не получил новых peer dependencies вместо Support adapters.

Критерий выхода выполнен: Goal 3 = `COMPLETE`; `SYSTEM_READY/FROZEN` не присваивается до Goals 5–9.

---
# Цель 4. Полный локальный freeze-аудит EngineFramework

Статус: `EVIDENCE_REVIEW_REQUIRED` (baseline `Epidemic 22-09-2026-2.zip`). Все B01–B08 интегрированы, но канонический ledger после проверки evidence содержит 0/52 `LOCAL_READY` и 52/52 `BLOCKED`: merge больше не подменяет per-criterion proof первым API-якорем. Формально зелёные schema gates не доказывают семантическую полноту. Remote CI на итоговом SHA также ещё не записан.

Действующий подробный план: `Milestone 4.md`.

Goal 4 присваивает только `LOCAL_READY`. Whole-engine persistence/determinism, sanitizer/lifetime/concurrency, реальные causal chains, load/degradation и финальный `FROZEN` остаются Goals 5–9.

## 4.1. Общий Framework contract

[ ] Все 52 production modules пройти 37-criteria local audit и 15-field dossier review (dossier fields размечены; 47 module criteria требуют точного evidence).
[ ] Все 3054 admission public callables получить reviewed classification и contract/test anchors; любой surface delta отдельно объяснить (classification есть, 1729 test/audit anchors и 621 contract anchors требуют содержательной замены).
[ ] Все 3348 mutation obligations, 73 lifecycle, 770 stale-identity и 17 external-boundary candidates получить проверенное reviewed decision (формальные flags выставлены, но semantic evidence ещё требует ревизии).
[x] `G4-INFRA-001`: мигрировать Framework fault evidence с process-global allocator injection на narrow private/module-local seams; общий helper удалить только после merge восьми delta.
[ ] `G4-DOC-001`: создать корректный Framework docs root и contract docs для всех 52 модулей (52 файла и индекс существуют; статусы/контракты нужно сверить с canonical ledger).
[x] Зафиксировать post-preflight Debug/Release baseline и exact CTest manifest.

## 4.2. Восемь независимых delta-блоков

[x] B01 Core Infrastructure + Runtime Boundary: Foundation, SupportRandom, Queries, Facts, Time, RuntimeBridge. Закрыть `G4-RB-001` и Framework docs root.
[x] B02 Identity, Materials and Local Effects: Entities, Materials, Environment, Conditions, Effects, Interaction, Ownership.
[x] B03 Inventory, Economy and Production: ItemsInventory, Equipment, Economy, Processes, ResourcesProduction, Loot. Закрыть `G4-PROC-001`, `G4-RESPROD-001`, `G4-RESPROD-002`.
[x] B04 Population, Life and Society: RolesJobs, NeedsLife, Population, Encounters, Society, Crime. Закрыть `G4-SOC-001`, `G4-SOC-002`.
[x] B05 Perception, Knowledge, AI and Simulation: Perception, Knowledge, NavigationSemantics, AI, Simulation. Закрыть `G4-PER-001`, `G4-PER-002`, `G4-PER-003`, `G4-SIM-001`, `G4-SIM-002`.
[x] B06 Combat, Abilities, Progression and Traversal: Combat, Abilities, Progression, Construction, Traversal. Закрыть `G4-COMBAT-001`, `G4-PROG-001`, `G4-PROG-002`, `G4-PROG-003`, `G4-TRAV-001`, `G4-TRAV-002`.
[x] B07 World, Save/Restore and Narrative: World, SaveGame, Narrative, Dialogue, NarrativeIntegration, WorldIntegration. Закрыть `G4-NARRINT-001`, `G4-NARRINT-002`.
[x] B08 Integration Layer and Cross-owner Adapters: Integration, StateIntegration, InteractionTimeIntegration, InteractionEffectsIntegration, GameplayIntegration, ExtendedGameplayIntegration, PerceptionKnowledgeAIIntegration, PopulationSimulationIntegration, ProcessResourceSimulationIntegration, SocialLegalIntegration, TraversalNavigationConstructionIntegration. Закрыть `G4-EXTINT-001`, `G4-EXTINT-002`.

Каждый Bxx работает только в своих production directories, existing module-specific tests, unique module docs и `_goal4_handoff/Bxx/**`. Каждый блок возвращает delta ZIP с project-relative changed files. `docs/freeze/**`, общий `work-plan.md`, shared CMake, `.github/**`, `cmake/**` и shared allocation/sweep helpers read-only для workers.

Дополнительные обязательства блоков:

[x] SaveGame закрывается как in-memory capture/validate/migrate/stage/commit orchestrator; storage/file I/O остаётся Goal 5.
[x] Durable/checkpoint/outbox adapters имеют duplicate, retry, stale cursor/reference, partial-progress и restore/reconciliation regressions.
[x] TraversalNavigationConstructionIntegration проверяется по durable placement-output protocol, stable layer IDs, acknowledge-after-Navigation-commit и retry/idempotence.
[x] Реальные multi-owner causal chains не подменяются локальными fake/port tests и остаются Goal 7.

## 4.3. Serial convergence после восьми delta

[x] Применить B01–B08 delta, проверяя single-writer ownership и cross-block findings.
[x] Удалить/deprecate старый global allocation helper только после подтверждения 0 remaining Goal 4 users.
[ ] Слить восемь handoff в canonical dossiers, coverage reviews, exact API anchors, defect registry и ledger (B06 пропуски восстановлены; exact evidence review не завершён).
[x] Перегенерировать `module_dossiers`, `coverage_manifests`, `public_api_inventory`, `public_surface_manifest`, `local_ready_contract`.
[ ] EngineFramework = 52/52 `LOCAL_READY`; каждый модуль = 37/37 `PASS/N/A`, dossiers = 15/15 reviewed (сейчас 0/52; 52 `BLOCKED`).
[ ] Framework API/obligations/lifecycle/stale/external unresolved = 0; `UNCLASSIFIED = 0` (классификация полная, содержательное evidence ещё не закрыто).
[ ] Каждый confirmed defect имеет существующий вызываемый regression target/symbol (все G4 targets перечислены точно и зарегистрированы; 19 Framework/Goal 4 regression entries без точного line anchor).
[x] Шесть public-header deltas относительно admission baseline 3054 callables reviewed с rationale и regressions в `docs/freeze/goal4_public_surface_review.md`; public callable IDs/signatures не изменились.

## 4.4. Финальный выход Goal 4

[x] Base Debug/Release, Runtime Debug/Release и Full Debug/Release green; exact CTest manifests совпадают. Base/Runtime qualification перенесена без изменений из Goal 3, Full Debug/Release перепроверены после merge Goal 4.
[x] Disabled/skipped tests без explicit freeze exception = 0.
[x] MSVC `/W4 /WX`, public-header self-containment, architecture/freeze validators и validator self-tests green.
[x] Process-global allocator override как Goal 4 fault mechanism = 0.
[x] `git diff --check` clean.
[ ] Remote Architecture Freeze CI на recorded SHA green, включая ClangCL public-surface.
[ ] `EngineBase = 9/9 LOCAL_READY`, `EngineRuntime = 17/17 LOCAL_READY`, `EngineFramework = 52/52 LOCAL_READY` (Framework сейчас 0/52).

Критерий выхода: Goal 4 = `COMPLETE`, все 52 Framework production-модуля локально закрыты без module-local code/evidence debt. `SYSTEM_READY/FROZEN` не присваивается до Goals 5–9.

## 4.5. Остаток содержательного evidence-аудита (2026-09-27)

`python docs/freeze/goal4_evidence_quality.py --check` является обязательным дополнительным локальным gate. Schema validators и зелёный CTest не заменяют его. Текущая очередь: 1729 общих API test/audit anchors, 621 общих API contract anchors, 712 criterion test anchors, 70 шаблонных `N/A` rationales и 19 defect regressions без точной строки. Счётчики могут пересекаться. Перепроверять нужно сами исходные `_goal4_handoff/Bxx/**`, затем повторять `python _goal4_handoff/merge_goal4_evidence.py` и оба gate; merge теперь не повышает неполный handoff до `LOCAL_READY`.

Для каждого модуля: заменить `main()`/общий заголовок документа на точный callable/contract и проверяемый тестовый case/assertion; добавить регрессию, если доказательства в тестах нет; дать module-specific обоснование `N/A`; довести каждый confirmed defect до зарегистрированного target и точного regression anchor. Только после 52/52 `LOCAL_READY` и зелёного quality gate можно выполнять recorded-SHA remote Architecture Freeze CI и отмечать Goal 4 `COMPLETE`.

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
