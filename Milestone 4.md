# Milestone 4: EngineFramework Local Closure Plan, 8 independent delta blocks

Дата повторного admission-аудита: 2026-09-23.
Baseline: `Epidemic 22-09-2026-2.zip`, после полного закрытия Goal 3.
Этот файл заменяет предыдущую 20-block схему выполнения. Содержание аудита и module coverage сохранены, ownership перекроен на восемь независимых агентов, каждый из которых возвращает delta-архив.

Статус выполнения на 2026-09-27: `EVIDENCE_REVIEW_REQUIRED`. Все B01–B08 применены; синтаксические freeze gates и локальные Full Debug/Release зелёные, но содержательная проверка handoff выявила шаблонные доказательства. Канонический ledger теперь показывает 0/52 `LOCAL_READY` и 52/52 `BLOCKED`: merge больше не подменяет per-criterion proof первым API-якорем. Финальный remote Architecture Freeze CI на recorded SHA ещё не выполнен.

## 1. Что именно должен закрыть Goal 4

Goal 4 доводит все 52 production-модуля `EngineFramework` до `LOCAL_READY`. Это локальная заморозочная готовность каждого freeze unit, а не финальный whole-engine `FROZEN`.

- Framework production modules: **52**.
- Public Framework headers: **59**.
- Public Framework callables: **3054**, `UNCLASSIFIED = 0` уже на admission baseline.
- Mutation/API obligations: **3348**.
- Lifecycle candidates: **73**.
- Stale-identity candidates: **770**.
- External-boundary candidates: **17**.
- Все 52 Framework modules сейчас `IN_AUDIT`: 4 архитектурных критерия уже `PASS`, остальные 33/37 требуют evidence.
- Для каждого модуля необходимо 15/15 reviewed dossier fields, 37/37 `PASS/N/A`, complete API contract/test anchors и отсутствие unresolved audit candidates.

За пределами Goal 4 остаются whole-engine persistence/determinism (Goal 5), sanitizer/lifetime/concurrency (Goal 6), реальные cross-layer causal chains (Goal 7), broad load/degradation (Goal 8) и окончательный `FROZEN` (Goal 9).

## 2. Результат повторной перепроверки плана

Покрытие модулей подтверждено заново: 5 BaseInfrastructure + 1 RuntimeBoundary + 33 GameplayWorldStateOwners + 13 IntegrationLayer = 52. В новом разбиении каждый модуль принадлежит ровно одному B01-B08, дубликатов и пропусков нет.

Первоначальный список подтверждённых проблем был неполным. После повторного систематического прохода по numeric conversions, generator/revision boundaries, restore paths, allocator publication и accepted external work подтверждено **23 problem IDs**: один распределённый test-infrastructure blocker, один documentation blocker и 21 production/integration defect.

`allocation_fault_injection.h` напрямую подключён в **31 test .cpp** и ещё в **2 shared sweep headers**. Поэтому `G4-INFRA-001` не отдаётся одному агенту на массовую правку: каждый Bxx мигрирует только owned tests, а окончательное удаление общего helper делает serial integrator после merge.

Два новых подтверждённых пункта после этой перепроверки: `G4-SIM-001` и `G4-SIM-002`. Они описывают accepted external commit до доказанной локальной terminal publication и fallible interval/summary/journal publication после revision advance.

## 3. Правила независимости восьми агентов

Все восемь агентов стартуют от одного и того же baseline. Им запрещено синхронизироваться через изменения общего дерева.

Каждый агент может изменять только:

- production directories перечисленных ему модулей;
- существующие module-specific test `.cpp` этих модулей;
- уникальные per-module docs `docs/EngineFramework/<Module>.md`;
- свой `_goal4_handoff/Bxx/**`;
- новые module-private/internal test seams внутри owned module implementation.

Общие файлы read-only для всех восьми агентов:

- `docs/freeze/**`;
- `work-plan.md` и этот Milestone 4 plan;
- root/Framework/shared `CMakeLists.txt`;
- `.github/**`, `cmake/**`;
- `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h`, `mutation_fault_sweep.h`, `restore_fault_sweep.h`;
- любые production directories другого Bxx.

`EngineFramework/README.md` и `docs/EngineFramework/README.md` принадлежат только B01. Остальные агенты создают только свои уникальные module docs.

Regression tests по возможности добавляются в существующий owned test source. Если без нового test file нельзя, агент кладёт файл в delta и записывает требуемое изменение shared CMake в handoff, но сам shared CMake не меняет.

Если найден дефект в чужом модуле, агент не исправляет его. Он добавляет в `_goal4_handoff/Bxx/cross_block_findings.md` точный reproduction, expected/actual и owner block.

## 4. Обязательный формат delta-архива

Каждый агент возвращает ZIP, например `Goal4_B05_Perception_AI_Simulation_delta.zip`. В архиве лежат только изменённые/новые файлы с project-relative paths.

Обязательно включить:

- `_goal4_handoff/Bxx/manifest.md`: baseline, owned modules, changed files, tests executed, remaining blockers;
- `_goal4_handoff/Bxx/defects.json`: подтверждённые defect IDs, root cause, fix, regression target/symbol;
- `_goal4_handoff/Bxx/dossier_reviews.json`;
- `_goal4_handoff/Bxx/coverage_reviews.json`;
- `_goal4_handoff/Bxx/public_api_anchors.json`;
- `_goal4_handoff/Bxx/cross_block_findings.md`, даже если он пуст;
- module-local source/tests/docs, которые реально изменены.

Не включать build directories, `.git`, полный проект, canonical generated `docs/freeze/*`, общий work-plan или файлы чужих блоков.

Delta считается готовой, если owned module tests проходят Debug/Release с warnings-as-errors и каждый изменённый defect path имеет regression. Агент не имеет права объявлять весь Framework `LOCAL_READY`.

## 5. Восемь независимых блоков

| Block | Область | Modules | API | Obligations | Life | Stale | External | Confirmed IDs |
|---|---|---:|---:|---:|---:|---:|---:|---|
| B01 | Core Infrastructure + Runtime Boundary | 6 | 326 | 344 | 5 | 66 | 4 | G4-DOC-001, G4-RB-001 |
| B02 | Identity, Materials and Local Effects | 7 | 403 | 372 | 8 | 85 | 0 | none at admission |
| B03 | Inventory, Economy and Production | 6 | 492 | 508 | 9 | 124 | 7 | G4-PROC-001, G4-RESPROD-001, G4-RESPROD-002 |
| B04 | Population, Life and Society | 6 | 445 | 436 | 12 | 103 | 0 | G4-SOC-001, G4-SOC-002 |
| B05 | Perception, Knowledge, AI and Simulation | 5 | 316 | 308 | 9 | 74 | 1 | G4-PER-001, G4-PER-002, G4-PER-003, G4-SIM-001, G4-SIM-002 |
| B06 | Combat, Abilities, Progression and Traversal | 5 | 362 | 492 | 11 | 115 | 0 | G4-COMBAT-001, G4-PROG-001, G4-PROG-002, G4-PROG-003, G4-TRAV-001, G4-TRAV-002 |
| B07 | World, Save/Restore and Narrative | 6 | 405 | 396 | 11 | 92 | 1 | G4-NARRINT-001, G4-NARRINT-002 |
| B08 | Integration Layer and Cross-owner Adapters | 11 | 305 | 492 | 8 | 111 | 4 | G4-EXTINT-001, G4-EXTINT-002 |

`G4-INFRA-001` является распределённым обязательством всех восьми блоков и поэтому не дублируется в колонке Confirmed IDs.

## 6.1. B01: Core Infrastructure + Runtime Boundary

Общие gameplay primitives и единственная Framework↔Runtime граница. B01 также единственный владелец Framework README и docs root index.

Workload: 6 modules, 13 public headers, 326 public API, 344 mutation obligations, 5 lifecycle, 66 stale-identity, 4 external-boundary candidates.

Known admission defects: `G4-DOC-001`, `G4-RB-001`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

Дополнительный exclusive ownership B01:

- `EngineFramework/README.md`;
- новый `docs/EngineFramework/README.md`;
- documentation convention/index для 52 module docs;
- B01 не удаляет shared allocator helper. Его удалит integrator только после merge всех восьми delta.

### Foundation

Freeze unit: `EngineFramework/BaseInfrastructure/Foundation`. Admission: 104 API, 32 obligations, 2 lifecycle, 7 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/BaseInfrastructure/Foundation/include`, `EngineFramework/BaseInfrastructure/Foundation/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/foundation_tests.cpp`


Главные риски: registry freeze, deterministic queries/order, invalid input/no-op semantics, snapshot or persistent state where exposed.

Функциональная поверхность, которую нужно доказать/доработать:

- Все gameplay IDs, refs, tags и handles имеют однозначную invalid/stale semantics.
- IdGenerator snapshot/restore и exhaustion.
- ChangeCursor ordering и stale cursor behavior.
- TypeRegistry duplicate registration.
- TypeRegistry freeze.
- GameplayContext не создаёт hidden ownership.
- Gameplay time/value types boundary arithmetic.

Минимальный regression pack: registry duplicate/freeze; invalid/no-op; deterministic output; boundary/exhaustion where applicable.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Все gameplay IDs, refs, tags и handles имеют однозначную invalid/stale semantics.
- [ ] IdGenerator snapshot/restore и exhaustion.
- [ ] ChangeCursor ordering и stale cursor behavior.
- [ ] TypeRegistry duplicate registration.
- [ ] TypeRegistry freeze.
- [ ] GameplayContext не создаёт hidden ownership.
- [ ] Gameplay time/value types boundary arithmetic.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### SupportRandom

Freeze unit: `EngineFramework/BaseInfrastructure/SupportRandom`. Admission: 28 API, 64 obligations, 0 lifecycle, 2 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/BaseInfrastructure/SupportRandom/include`, `EngineFramework/BaseInfrastructure/SupportRandom/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/random_support_tests.cpp`


Главные риски: deterministic sequence, seed derivation, exhaustion, snapshot/replay equivalence.

Функциональная поверхность, которую нужно доказать/доработать:

- Один seed и одна sequence дают одинаковые значения.
- Snapshot/restore random sequence продолжает тот же stream.
- Разные streams не делят hidden mutable state.
- Boundary ranges не имеют modulo bias, если contract обещает uniform result.
- Invalid range отклоняется.
- Randomness не зависит от wall clock после создания stream.

Минимальный regression pack: known-vector determinism; exhaustion; snapshot/replay; invalid bound/range.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Один seed и одна sequence дают одинаковые значения.
- [ ] Snapshot/restore random sequence продолжает тот же stream.
- [ ] Разные streams не делят hidden mutable state.
- [ ] Boundary ranges не имеют modulo bias, если contract обещает uniform result.
- [ ] Invalid range отклоняется.
- [ ] Randomness не зависит от wall clock после создания stream.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Queries

Freeze unit: `EngineFramework/BaseInfrastructure/Queries`. Admission: 43 API, 24 obligations, 1 lifecycle, 4 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/BaseInfrastructure/Queries/include`, `EngineFramework/BaseInfrastructure/Queries/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/queries_tests.cpp`


Главные риски: registry freeze, deterministic queries/order, invalid input/no-op semantics, snapshot or persistent state where exposed.

Функциональная поверхность, которую нужно доказать/доработать:

- Provider registration, duplicate и freeze.
- Query type mismatch.
- Consistency/coverage/accuracy requirements.
- Budget exhaustion.
- Snapshot coordinator epoch.
- Provider failure не оставляет partial response state.
- Deterministic provider order.
- Detached query response lifetime.
- Diagnostics не является authoritative state.

Минимальный regression pack: registry duplicate/freeze; invalid/no-op; deterministic output; boundary/exhaustion where applicable.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Provider registration, duplicate и freeze.
- [ ] Query type mismatch.
- [ ] Consistency/coverage/accuracy requirements.
- [ ] Budget exhaustion.
- [ ] Snapshot coordinator epoch.
- [ ] Provider failure не оставляет partial response state.
- [ ] Deterministic provider order.
- [ ] Detached query response lifetime.
- [ ] Diagnostics не является authoritative state.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Facts

Freeze unit: `EngineFramework/BaseInfrastructure/Facts`. Admission: 49 API, 76 obligations, 1 lifecycle, 17 stale, 1 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/BaseInfrastructure/Facts/include`, `EngineFramework/BaseInfrastructure/Facts/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/facts_tests.cpp`


Главные риски: transaction/event ordering, local/global sequences, subscriber isolation, persistence snapshot, payload codec boundaries.

Функциональная поверхность, которую нужно доказать/доработать:

- Fact assert/update/remove lifecycle.
- Event publication.
- History policy.
- Transaction commit/rollback.
- Duplicate fact/event identities.
- Type registration и freeze.
- Subscriber reentrancy.
- Subscriber failure policy.
- Journal/history cursor.
- Compaction не удаляет необходимый retained history.
- Snapshot/restore persistent facts, events, generator and cursor.
- Failed transaction или allocation не публикует half-state.

Минимальный regression pack: batch/event sequence exhaustion; failed commit atomicity; subscriber throw isolation; snapshot/persistence roundtrip; payload type/codec mismatch.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Fact assert/update/remove lifecycle.
- [ ] Event publication.
- [ ] History policy.
- [ ] Transaction commit/rollback.
- [ ] Duplicate fact/event identities.
- [ ] Type registration и freeze.
- [ ] Subscriber reentrancy.
- [ ] Subscriber failure policy.
- [ ] Journal/history cursor.
- [ ] Compaction не удаляет необходимый retained history.
- [ ] Snapshot/restore persistent facts, events, generator and cursor.
- [ ] Failed transaction или allocation не публикует half-state.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Time

Freeze unit: `EngineFramework/BaseInfrastructure/Time`. Admission: 31 API, 60 obligations, 1 lifecycle, 15 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/BaseInfrastructure/Time/include`, `EngineFramework/BaseInfrastructure/Time/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/time_tests.cpp`


Главные риски: timer/schedule identity, catch-up, duration arithmetic, cancellation, snapshot continuity.

Функциональная поверхность, которую нужно доказать/доработать:

- Clock registration/state.
- Schedule create/update/remove.
- Recurrence daily/calendar/custom boundaries.
- Catch-up policies.
- Large time jump.
- Duplicate schedule identity.
- Cancel terminal semantics.
- Trigger order deterministic при одинаковом timestamp.
- Scheduler budget exhaustion deferred, без потери triggers.
- Journal/cursor consistency.
- Snapshot/restore clocks, schedules, generators, cursor.

Минимальный regression pack: timer exhaustion; cancellation/no-op; large catch-up; partitioned vs combined time; snapshot roundtrip.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Clock registration/state.
- [ ] Schedule create/update/remove.
- [ ] Recurrence daily/calendar/custom boundaries.
- [ ] Catch-up policies.
- [ ] Large time jump.
- [ ] Duplicate schedule identity.
- [ ] Cancel terminal semantics.
- [ ] Trigger order deterministic при одинаковом timestamp.
- [ ] Scheduler budget exhaustion deferred, без потери triggers.
- [ ] Journal/cursor consistency.
- [ ] Snapshot/restore clocks, schedules, generators, cursor.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### RuntimeBridge

Freeze unit: `EngineFramework/RuntimeBoundary/RuntimeBridge`. Admission: 71 API, 88 obligations, 0 lifecycle, 21 stale, 3 external candidates.

Confirmed defects: `G4-RB-001`

Код: `EngineFramework/RuntimeBoundary/RuntimeBridge/include`, `EngineFramework/RuntimeBoundary/RuntimeBridge/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/runtime_bridge_tests.cpp`


Главные риски: runtime↔semantic conversion, stale generation/handle, accepted Runtime work, numeric representability, retry/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Framework semantic object creates exactly one runtime materialization per binding generation.
- Runtime handle/generation forward and reverse maps remain mutually consistent.
- Create/update/remove projection commands and dematerialization cleanup are retry-safe.
- Runtime backend failure must not falsely publish semantic success.
- Stale runtime observation cannot apply to a newer Framework generation.
- Duplicate command/event delivery is idempotent or rejected by the documented contract.
- Queue ordering, coalescing, backpressure and processing budgets preserve accepted work.
- World/Physics/Environment/Navigation observations validate finite numeric data and semantic ranges.
- Visibility distance/direction math remains valid for every finite runtime position representable by the Runtime contract.
- Checkpoint/reconciliation state, if present, restores mappings without orphaned Runtime representations.

Минимальный regression pack: create/update/remove retry; stale generation; backend failure; checkpoint/reconciliation; extreme finite numeric conversions; duplicate command/event.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Framework semantic object creates exactly one runtime materialization per binding generation.
- [ ] Runtime handle/generation forward and reverse maps remain mutually consistent.
- [ ] Create/update/remove projection commands and dematerialization cleanup are retry-safe.
- [ ] Runtime backend failure must not falsely publish semantic success.
- [ ] Stale runtime observation cannot apply to a newer Framework generation.
- [ ] Duplicate command/event delivery is idempotent or rejected by the documented contract.
- [ ] Queue ordering, coalescing, backpressure and processing budgets preserve accepted work.
- [ ] World/Physics/Environment/Navigation observations validate finite numeric data and semantic ranges.
- [ ] Visibility distance/direction math remains valid for every finite runtime position representable by the Runtime contract.
- [ ] Checkpoint/reconciliation state, if present, restores mappings without orphaned Runtime representations.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.2. B02: Identity, Materials and Local Effects

Базовые state owners с плотными identity/index/lifecycle связями, но без внешних durable transaction coordinators.

Workload: 7 modules, 7 public headers, 403 public API, 372 mutation obligations, 8 lifecycle, 85 stale-identity, 0 external-boundary candidates.

Known admission defects: нет подтверждённых production defects на baseline; это не отменяет полный аудит.

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### Entities

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Entities`. Admission: 75 API, 52 obligations, 3 lifecycle, 11 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Entities/include`, `EngineFramework/GameplayWorldStateOwners/Entities/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/entities_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Entity create/remove/deactivate/convert lifecycle.
- Archetype and part references.
- Tags and parts indexes.
- Stale entity/part references.
- Convert сохраняет только разрешённые fields.
- Pruning history не повреждает latest cursor.
- Snapshot/restore records, indexes, generators, revisions, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Entity create/remove/deactivate/convert lifecycle.
- [ ] Archetype and part references.
- [ ] Tags and parts indexes.
- [ ] Stale entity/part references.
- [ ] Convert сохраняет только разрешённые fields.
- [ ] Pruning history не повреждает latest cursor.
- [ ] Snapshot/restore records, indexes, generators, revisions, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Materials

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Materials`. Admission: 67 API, 60 obligations, 1 lifecycle, 14 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Materials/include`, `EngineFramework/GameplayWorldStateOwners/Materials/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/materials_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Material/substance/type registration.
- Composition validation и normalization.
- Slot assignment.
- Stimulus/exposure mutation.
- Cross-reference validity.
- Derived material response соответствует primary composition.
- Journal prune boundary.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Material/substance/type registration.
- [ ] Composition validation и normalization.
- [ ] Slot assignment.
- [ ] Stimulus/exposure mutation.
- [ ] Cross-reference validity.
- [ ] Derived material response соответствует primary composition.
- [ ] Journal prune boundary.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Environment

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Environment`. Admission: 44 API, 40 obligations, 1 lifecycle, 10 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Environment/include`, `EngineFramework/GameplayWorldStateOwners/Environment/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/environment_gameplay_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Gameplay environment definitions and state.
- Region/area references.
- Weather/environment semantic mutation.
- Expiration/sweep.
- Derived queries match authoritative state.
- No duplicate semantic state against Runtime Environment.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Gameplay environment definitions and state.
- [ ] Region/area references.
- [ ] Weather/environment semantic mutation.
- [ ] Expiration/sweep.
- [ ] Derived queries match authoritative state.
- [ ] No duplicate semantic state against Runtime Environment.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Conditions

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Conditions`. Admission: 50 API, 60 obligations, 1 lifecycle, 14 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Conditions/include`, `EngineFramework/GameplayWorldStateOwners/Conditions/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/conditions_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition registration и freeze.
- Apply/remove.
- Stacking policies.
- Persistence/materialization policies.
- Periodic catch-up.
- Expiration.
- Subject provider failure.
- Duplicate condition handling.
- Snapshot/restore instances, generators, revisions, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Definition registration и freeze.
- [ ] Apply/remove.
- [ ] Stacking policies.
- [ ] Persistence/materialization policies.
- [ ] Periodic catch-up.
- [ ] Expiration.
- [ ] Subject provider failure.
- [ ] Duplicate condition handling.
- [ ] Snapshot/restore instances, generators, revisions, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Effects

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Effects`. Admission: 57 API, 60 obligations, 1 lifecycle, 13 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Effects/include`, `EngineFramework/GameplayWorldStateOwners/Effects/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/handler registration и freeze.
- Immediate execution.
- Deferred execution.
- Cancel deferred.
- Cancel deferred targeting.
- Take deferred by schedule.
- Multi-operation batch prepare/commit.
- Handler prepare failure.
- Handler commit failure.
- Target state provider failure.
- Execution budget.
- Journal pruning.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Definition/handler registration и freeze.
- [ ] Immediate execution.
- [ ] Deferred execution.
- [ ] Cancel deferred.
- [ ] Cancel deferred targeting.
- [ ] Take deferred by schedule.
- [ ] Multi-operation batch prepare/commit.
- [ ] Handler prepare failure.
- [ ] Handler commit failure.
- [ ] Target state provider failure.
- [ ] Execution budget.
- [ ] Journal pruning.
- [ ] Snapshot/restore deferred queue, IDs, revisions and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Interaction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Interaction`. Admission: 47 API, 48 obligations, 1 lifecycle, 11 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Interaction/include`, `EngineFramework/GameplayWorldStateOwners/Interaction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_gameplay_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction definition/session/request lifecycle.
- Start/advance/complete/cancel.
- Preconditions and state provider.
- Executor success/failure.
- Timeout/sweep.
- Duplicate execution prevention.
- Reservation/pending state rollback.
- Snapshot/restore active interactions and pending execution state.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Interaction definition/session/request lifecycle.
- [ ] Start/advance/complete/cancel.
- [ ] Preconditions and state provider.
- [ ] Executor success/failure.
- [ ] Timeout/sweep.
- [ ] Duplicate execution prevention.
- [ ] Reservation/pending state rollback.
- [ ] Snapshot/restore active interactions and pending execution state.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Ownership

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Ownership`. Admission: 63 API, 52 obligations, 0 lifecycle, 12 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Ownership/include`, `EngineFramework/GameplayWorldStateOwners/Ownership/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/ownership_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Owner/property identity.
- Assign/transfer/remove ownership.
- Duplicate ownership prevention.
- Shared/fractional semantics, если предусмотрены.
- Stale owner/property references.
- Transfer atomicity.
- Journal/revision.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Owner/property identity.
- [ ] Assign/transfer/remove ownership.
- [ ] Duplicate ownership prevention.
- [ ] Shared/fractional semantics, если предусмотрены.
- [ ] Stale owner/property references.
- [ ] Transfer atomicity.
- [ ] Journal/revision.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.3. B03: Inventory, Economy and Production

Ресурсные и экономические транзакции, reserve/commit/release, accepted work и наиболее тяжёлые mutation/publication paths.

Workload: 6 modules, 6 public headers, 492 public API, 508 mutation obligations, 9 lifecycle, 124 stale-identity, 7 external-boundary candidates.

Known admission defects: `G4-PROC-001`, `G4-RESPROD-001`, `G4-RESPROD-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### ItemsInventory

Freeze unit: `EngineFramework/GameplayWorldStateOwners/ItemsInventory`. Admission: 82 API, 92 obligations, 1 lifecycle, 23 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/ItemsInventory/include`, `EngineFramework/GameplayWorldStateOwners/ItemsInventory/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/items_inventory_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Item definition and container registration.
- Item create/remove.
- Stack split/merge rules.
- Capacity and slot limits.
- Transfer within/between containers.
- Reservation create/consume/release.
- Prepare/commit/cancel transfer.
- Exchange reservations.
- Durability and charges boundaries.
- World/container bindings.
- Secondary container/item indexes.
- Failed transfer leaves source and target unchanged.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Item definition and container registration.
- [ ] Item create/remove.
- [ ] Stack split/merge rules.
- [ ] Capacity and slot limits.
- [ ] Transfer within/between containers.
- [ ] Reservation create/consume/release.
- [ ] Prepare/commit/cancel transfer.
- [ ] Exchange reservations.
- [ ] Durability and charges boundaries.
- [ ] World/container bindings.
- [ ] Secondary container/item indexes.
- [ ] Failed transfer leaves source and target unchanged.
- [ ] Snapshot/restore items, containers, reservations, bindings, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Equipment

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Equipment`. Admission: 72 API, 72 obligations, 1 lifecycle, 18 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Equipment/include`, `EngineFramework/GameplayWorldStateOwners/Equipment/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/equipment_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Equipment slot definition.
- Equip/unequip.
- Prepare equip.
- Item provider reservation.
- Conflicting slots.
- Requirements and compatibility.
- Reconcile restored bindings.
- Exchange/reconcile reservations.
- Provider failure before and after prepare.
- Snapshot/restore equipment and external binding metadata.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Equipment slot definition.
- [ ] Equip/unequip.
- [ ] Prepare equip.
- [ ] Item provider reservation.
- [ ] Conflicting slots.
- [ ] Requirements and compatibility.
- [ ] Reconcile restored bindings.
- [ ] Exchange/reconcile reservations.
- [ ] Provider failure before and after prepare.
- [ ] Snapshot/restore equipment and external binding metadata.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Economy

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Economy`. Admission: 105 API, 108 obligations, 1 lifecycle, 27 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Economy/include`, `EngineFramework/GameplayWorldStateOwners/Economy/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/economy_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Currency/account registration.
- Funds reservation lifecycle.
- Monetary transfer.
- Offer lifecycle.
- Trade transaction lifecycle.
- Debt and contract lifecycle.
- Price provider failure.
- No negative/overflow balance unless explicitly allowed.
- Reservation prevents double spending.
- Transfer failure leaves both accounts unchanged.
- Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Currency/account registration.
- [ ] Funds reservation lifecycle.
- [ ] Monetary transfer.
- [ ] Offer lifecycle.
- [ ] Trade transaction lifecycle.
- [ ] Debt and contract lifecycle.
- [ ] Price provider failure.
- [ ] No negative/overflow balance unless explicitly allowed.
- [ ] Reservation prevents double spending.
- [ ] Transfer failure leaves both accounts unchanged.
- [ ] Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Processes

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Processes`. Admission: 93 API, 88 obligations, 4 lifecycle, 20 stale, 4 external candidates.

Confirmed defects: `G4-PROC-001`

Код: `EngineFramework/GameplayWorldStateOwners/Processes/include`, `EngineFramework/GameplayWorldStateOwners/Processes/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/processes_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Process definition/execution lifecycle.
- Input reservations.
- Output prepare/commit.
- Cancellation.
- Provider failure.
- Partial input/output failure rollback.
- Process scheduling/state transitions.
- Snapshot/restore executions, reservations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Process definition/execution lifecycle.
- [ ] Input reservations.
- [ ] Output prepare/commit.
- [ ] Cancellation.
- [ ] Provider failure.
- [ ] Partial input/output failure rollback.
- [ ] Process scheduling/state transitions.
- [ ] Snapshot/restore executions, reservations, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### ResourcesProduction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction`. Admission: 88 API, 84 obligations, 1 lifecycle, 21 stale, 3 external candidates.

Confirmed defects: `G4-RESPROD-001`, `G4-RESPROD-002`

Код: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/include`, `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_tests.cpp`, `EngineFramework/DevelopmentInfrastructure/Tests/resources_production_separation_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Resource definitions/stores/producers.
- Production reservation.
- Consume/produce atomicity.
- Capacity and quantity boundaries.
- Negative/overflow quantities rejected.
- Producer lifecycle.
- Process integration ports remain external.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Resource definitions/stores/producers.
- [ ] Production reservation.
- [ ] Consume/produce atomicity.
- [ ] Capacity and quantity boundaries.
- [ ] Negative/overflow quantities rejected.
- [ ] Producer lifecycle.
- [ ] Process integration ports remain external.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Loot

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Loot`. Admission: 52 API, 64 obligations, 1 lifecycle, 15 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Loot/include`, `EngineFramework/GameplayWorldStateOwners/Loot/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/loot_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Loot table/definition registration.
- Generate deterministic output from explicit random source.
- Generated, pending, delivered, cancelled lifecycle.
- Reward handler prepare/commit/cancel.
- Discard generated.
- Duplicate delivery protection.
- External reward failure and reconciliation.
- Snapshot/restore pending deliveries, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Loot table/definition registration.
- [ ] Generate deterministic output from explicit random source.
- [ ] Generated, pending, delivered, cancelled lifecycle.
- [ ] Reward handler prepare/commit/cancel.
- [ ] Discard generated.
- [ ] Duplicate delivery protection.
- [ ] External reward failure and reconciliation.
- [ ] Snapshot/restore pending deliveries, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.4. B04: Population, Life and Society

Долгоживущие социальные state owners с большим количеством lifecycle, stale identity и derived indexes.

Workload: 6 modules, 6 public headers, 445 public API, 436 mutation obligations, 12 lifecycle, 103 stale-identity, 0 external-boundary candidates.

Known admission defects: `G4-SOC-001`, `G4-SOC-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### RolesJobs

Freeze unit: `EngineFramework/GameplayWorldStateOwners/RolesJobs`. Admission: 73 API, 80 obligations, 5 lifecycle, 20 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/RolesJobs/include`, `EngineFramework/GameplayWorldStateOwners/RolesJobs/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/roles_jobs_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Role/job definitions.
- Assignment/unassignment.
- Capacity/eligibility rules.
- Worker/job stale references.
- Duplicate assignment.
- Job lifecycle.
- Failed reassignment leaves old assignment valid.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Role/job definitions.
- [ ] Assignment/unassignment.
- [ ] Capacity/eligibility rules.
- [ ] Worker/job stale references.
- [ ] Duplicate assignment.
- [ ] Job lifecycle.
- [ ] Failed reassignment leaves old assignment valid.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### NeedsLife

Freeze unit: `EngineFramework/GameplayWorldStateOwners/NeedsLife`. Admission: 65 API, 72 obligations, 1 lifecycle, 17 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/NeedsLife/include`, `EngineFramework/GameplayWorldStateOwners/NeedsLife/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/needs_life_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Need definitions/state.
- Satisfy/decay boundaries.
- Life pressure lifecycle.
- Expiration sweep.
- Resolve pressure.
- Simulation interval including large delta.
- Terminal pressure pruning.
- No negative/overflow need values unless contract allows.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Need definitions/state.
- [ ] Satisfy/decay boundaries.
- [ ] Life pressure lifecycle.
- [ ] Expiration sweep.
- [ ] Resolve pressure.
- [ ] Simulation interval including large delta.
- [ ] Terminal pressure pruning.
- [ ] No negative/overflow need values unless contract allows.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Population

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Population`. Admission: 75 API, 80 obligations, 2 lifecycle, 19 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Population/include`, `EngineFramework/GameplayWorldStateOwners/Population/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/population_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Template registry and definition freeze.
- Group and unit creation; group counters and all unit indexes remain consistent.
- Unit semantic lifecycle: virtual/materialized, dematerialize, dead and retired states.
- Entity binding and stale entity/reference handling.
- Residence assignment and area/residence indexes.
- Migration lifecycle: start, complete, cancel and fail; no half-updated group/area state.
- Allocation lifecycle: reserve batch, commit, release and terminal pruning; active-allocation uniqueness.
- Capacity, aggregate counts, revision/change-sequence exhaustion and deterministic query ordering.
- Snapshot/restore of templates, groups, units, residences, migrations, allocations, generators, indexes and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Template registry and definition freeze.
- [ ] Group and unit creation; group counters and all unit indexes remain consistent.
- [ ] Unit semantic lifecycle: virtual/materialized, dematerialize, dead and retired states.
- [ ] Entity binding and stale entity/reference handling.
- [ ] Residence assignment and area/residence indexes.
- [ ] Migration lifecycle: start, complete, cancel and fail; no half-updated group/area state.
- [ ] Allocation lifecycle: reserve batch, commit, release and terminal pruning; active-allocation uniqueness.
- [ ] Capacity, aggregate counts, revision/change-sequence exhaustion and deterministic query ordering.
- [ ] Snapshot/restore of templates, groups, units, residences, migrations, allocations, generators, indexes and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Encounters

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Encounters`. Admission: 70 API, 68 obligations, 2 lifecycle, 14 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Encounters/include`, `EngineFramework/GameplayWorldStateOwners/Encounters/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/encounters_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/spawn table registration.
- Encounter start/active/terminal lifecycle.
- Spawn request lifecycle.
- Spawn point state.
- Spawned entity record consistency.
- Respawn rules.
- Budget limits.
- Fail encounter.
- Despawn lifecycle.
- Terminal pruning.
- Deterministic spawn roll via explicit random source.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Definition/spawn table registration.
- [ ] Encounter start/active/terminal lifecycle.
- [ ] Spawn request lifecycle.
- [ ] Spawn point state.
- [ ] Spawned entity record consistency.
- [ ] Respawn rules.
- [ ] Budget limits.
- [ ] Fail encounter.
- [ ] Despawn lifecycle.
- [ ] Terminal pruning.
- [ ] Deterministic spawn roll via explicit random source.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Society

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Society`. Admission: 62 API, 56 obligations, 1 lifecycle, 14 stale, 0 external candidates.

Confirmed defects: `G4-SOC-001`, `G4-SOC-002`

Код: `EngineFramework/GameplayWorldStateOwners/Society/include`, `EngineFramework/GameplayWorldStateOwners/Society/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/society_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Social groups/relations/reputation definitions.
- Relationship mutation boundaries.
- Membership lifecycle.
- Symmetric/asymmetric relationship semantics.
- Duplicate relation prevention.
- Social change journal.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Social groups/relations/reputation definitions.
- [ ] Relationship mutation boundaries.
- [ ] Membership lifecycle.
- [ ] Symmetric/asymmetric relationship semantics.
- [ ] Duplicate relation prevention.
- [ ] Social change journal.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Crime

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Crime`. Admission: 100 API, 80 obligations, 1 lifecycle, 19 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Crime/include`, `EngineFramework/GameplayWorldStateOwners/Crime/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/crime_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Law/jurisdiction/authority registration.
- Crime candidate evaluation.
- Crime record lifecycle.
- Evidence and witness lifecycle.
- Proof state transitions.
- Bounty lifecycle.
- Response request lifecycle.
- Invalid jurisdiction/authority reference.
- Duplicate evidence/witness behavior.
- Snapshot/restore all legal records and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Law/jurisdiction/authority registration.
- [ ] Crime candidate evaluation.
- [ ] Crime record lifecycle.
- [ ] Evidence and witness lifecycle.
- [ ] Proof state transitions.
- [ ] Bounty lifecycle.
- [ ] Response request lifecycle.
- [ ] Invalid jurisdiction/authority reference.
- [ ] Duplicate evidence/witness behavior.
- [ ] Snapshot/restore all legal records and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.5. B05: Perception, Knowledge, AI and Simulation

Детерминированное принятие решений, spatial/numeric boundaries, providers/executors и resumable simulation.

Workload: 5 modules, 5 public headers, 316 public API, 308 mutation obligations, 9 lifecycle, 74 stale-identity, 1 external-boundary candidates.

Known admission defects: `G4-PER-001`, `G4-PER-002`, `G4-PER-003`, `G4-SIM-001`, `G4-SIM-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### Perception

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Perception`. Admission: 72 API, 56 obligations, 1 lifecycle, 13 stale, 0 external candidates.

Confirmed defects: `G4-PER-001`, `G4-PER-002`, `G4-PER-003`

Код: `EngineFramework/GameplayWorldStateOwners/Perception/include`, `EngineFramework/GameplayWorldStateOwners/Perception/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/perception_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Sense, perceiver-profile and evaluator definition registration/freeze.
- Stimulus lifecycle and perceiver sampling/provider contracts.
- Observation and awareness lifecycle, duplicate/coalescing semantics and deterministic ordering.
- Built-in spatial evaluation: range, attenuation and FOV with exact handling of large int64 millimetre coordinates.
- Provider/evaluator failure leaves observations, indexes, revision and journal unchanged.
- Expiration, forgetting, suspicion/awareness decay and large catch-up intervals.
- Materialized/runtime-projection flags and stale subject/stimulus references.
- Revision/change-sequence exhaustion and bounded journal semantics.
- Snapshot/restore of definitions where applicable, stimuli, observations, awareness state, generators, indexes and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Sense, perceiver-profile and evaluator definition registration/freeze.
- [ ] Stimulus lifecycle and perceiver sampling/provider contracts.
- [ ] Observation and awareness lifecycle, duplicate/coalescing semantics and deterministic ordering.
- [ ] Built-in spatial evaluation: range, attenuation and FOV with exact handling of large int64 millimetre coordinates.
- [ ] Provider/evaluator failure leaves observations, indexes, revision and journal unchanged.
- [ ] Expiration, forgetting, suspicion/awareness decay and large catch-up intervals.
- [ ] Materialized/runtime-projection flags and stale subject/stimulus references.
- [ ] Revision/change-sequence exhaustion and bounded journal semantics.
- [ ] Snapshot/restore of definitions where applicable, stimuli, observations, awareness state, generators, indexes and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Knowledge

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Knowledge`. Admission: 40 API, 48 obligations, 1 lifecycle, 12 stale, 1 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Knowledge/include`, `EngineFramework/GameplayWorldStateOwners/Knowledge/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/knowledge_memory_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Knowledge learn/share/forget/contradict lifecycle.
- Confidence/value boundaries.
- Memory compaction.
- Subject/source references.
- Duplicate knowledge semantics.
- Contradiction does not corrupt indexes.
- Decay.
- Graph/index consistency.
- Snapshot/restore memories, relations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Knowledge learn/share/forget/contradict lifecycle.
- [ ] Confidence/value boundaries.
- [ ] Memory compaction.
- [ ] Subject/source references.
- [ ] Duplicate knowledge semantics.
- [ ] Contradiction does not corrupt indexes.
- [ ] Decay.
- [ ] Graph/index consistency.
- [ ] Snapshot/restore memories, relations, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### NavigationSemantics

Freeze unit: `EngineFramework/GameplayWorldStateOwners/NavigationSemantics`. Admission: 69 API, 60 obligations, 1 lifecycle, 14 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/include`, `EngineFramework/GameplayWorldStateOwners/NavigationSemantics/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/navigation_semantics_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Semantic area/layer/rule registration.
- Registry freeze.
- Capability provider failure.
- Route/query semantic constraints.
- Layer create/update/remove.
- Invalid world references.
- Deterministic rule resolution.
- Snapshot/restore semantic graph/indexes.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Semantic area/layer/rule registration.
- [ ] Registry freeze.
- [ ] Capability provider failure.
- [ ] Route/query semantic constraints.
- [ ] Layer create/update/remove.
- [ ] Invalid world references.
- [ ] Deterministic rule resolution.
- [ ] Snapshot/restore semantic graph/indexes.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### AI

Freeze unit: `EngineFramework/GameplayWorldStateOwners/AI`. Admission: 90 API, 104 obligations, 4 lifecycle, 26 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/AI/include`, `EngineFramework/GameplayWorldStateOwners/AI/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/ai_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Profile/goal/intent/consideration registration.
- Agent register/remove.
- Blackboard key/value type validation.
- Goal/intent lifecycle.
- Think/replan.
- Evaluator failure.
- Access policy.
- Target candidate handling.
- SetNextThink scheduling.
- Deterministic tie breaking.
- Failed think does not half-update agent/index/journal.
- Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Profile/goal/intent/consideration registration.
- [ ] Agent register/remove.
- [ ] Blackboard key/value type validation.
- [ ] Goal/intent lifecycle.
- [ ] Think/replan.
- [ ] Evaluator failure.
- [ ] Access policy.
- [ ] Target candidate handling.
- [ ] SetNextThink scheduling.
- [ ] Deterministic tie breaking.
- [ ] Failed think does not half-update agent/index/journal.
- [ ] Snapshot/restore agents, blackboards, intents, generators, scheduler state, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Simulation

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Simulation`. Admission: 45 API, 40 obligations, 2 lifecycle, 9 stale, 0 external candidates.

Confirmed defects: `G4-SIM-001`, `G4-SIM-002`

Код: `EngineFramework/GameplayWorldStateOwners/Simulation/include`, `EngineFramework/GameplayWorldStateOwners/Simulation/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/simulation_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Framework simulation definitions/state.
- Job/task registration and lifecycle.
- Budgeted update.
- Proposal/application boundary to state owners.
- No duplicate authoritative state with Runtime Simulation.
- Large delta/catch-up.
- Failure atomicity.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Framework simulation definitions/state.
- [ ] Job/task registration and lifecycle.
- [ ] Budgeted update.
- [ ] Proposal/application boundary to state owners.
- [ ] No duplicate authoritative state with Runtime Simulation.
- [ ] Large delta/catch-up.
- [ ] Failure atomicity.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.6. B06: Combat, Abilities, Progression and Traversal

Сложные state machines и multi-container mutations с наиболее важными revision/generator/lifecycle boundary cases.

Workload: 5 modules, 5 public headers, 362 public API, 492 mutation obligations, 11 lifecycle, 115 stale-identity, 0 external-boundary candidates.

Known admission defects: `G4-COMBAT-001`, `G4-PROG-001`, `G4-PROG-002`, `G4-PROG-003`, `G4-TRAV-001`, `G4-TRAV-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### Combat

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Combat`. Admission: 52 API, 80 obligations, 1 lifecycle, 17 stale, 0 external candidates.

Confirmed defects: `G4-COMBAT-001`

Код: `EngineFramework/GameplayWorldStateOwners/Combat/include`, `EngineFramework/GameplayWorldStateOwners/Combat/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/combat_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Combatant registration/removal.
- Engagement lifecycle.
- Damage plan/resolve.
- Modifier ordering.
- Resource reservation.
- Resource depletion transitions alive/downed/dead/disabled.
- Clamp/overflow behavior.
- Modifier provider failure.
- Failed resolution does not consume reservation or change resource.
- Snapshot/restore combatants, reservations, generators, journal.
- Preserve-ratio SetResourceMaximum must use portable exact/safe integer ratio arithmetic near int64 boundaries; MSVC double precision may not decide the result.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Combatant registration/removal.
- [ ] Engagement lifecycle.
- [ ] Damage plan/resolve.
- [ ] Modifier ordering.
- [ ] Resource reservation.
- [ ] Resource depletion transitions alive/downed/dead/disabled.
- [ ] Clamp/overflow behavior.
- [ ] Modifier provider failure.
- [ ] Failed resolution does not consume reservation or change resource.
- [ ] Snapshot/restore combatants, reservations, generators, journal.
- [ ] Preserve-ratio SetResourceMaximum must use portable exact/safe integer ratio arithmetic near int64 boundaries; MSVC double precision may not decide the result.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Abilities

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Abilities`. Admission: 57 API, 92 obligations, 1 lifecycle, 23 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Abilities/include`, `EngineFramework/GameplayWorldStateOwners/Abilities/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/abilities_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Definition/instance grant/remove.
- Availability checks.
- Activation lifecycle.
- Target policy.
- Cost reservation.
- Cooldown group.
- Requirement provider failure.
- Resource provider prepare/commit/release.
- Materialization provider failure.
- Execution cancel/complete.
- Duplicate delivery prevention.
- Snapshot/restore instances, executions, reservations, cooldowns, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Definition/instance grant/remove.
- [ ] Availability checks.
- [ ] Activation lifecycle.
- [ ] Target policy.
- [ ] Cost reservation.
- [ ] Cooldown group.
- [ ] Requirement provider failure.
- [ ] Resource provider prepare/commit/release.
- [ ] Materialization provider failure.
- [ ] Execution cancel/complete.
- [ ] Duplicate delivery prevention.
- [ ] Snapshot/restore instances, executions, reservations, cooldowns, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Progression

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Progression`. Admission: 63 API, 100 obligations, 1 lifecycle, 25 stale, 0 external candidates.

Confirmed defects: `G4-PROG-001`, `G4-PROG-002`, `G4-PROG-003`

Код: `EngineFramework/GameplayWorldStateOwners/Progression/include`, `EngineFramework/GameplayWorldStateOwners/Progression/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/progression_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Attribute, track, perk, unlock and milestone definition registration plus freeze validation.
- Profile create/remove and base-attribute mutation.
- Modifier add/remove/replace-by-source and service-wide modifier identity.
- GrantProgress/SetProgress rank transitions and arithmetic boundaries.
- ReserveProgressGrant/CommitProgressGrant/ReleaseProgressGrant exact reservation semantics.
- Perk and unlock grant/revoke semantics plus milestone evaluation and reward deduplication.
- Revision and journal exhaustion must reject before mutation; no successful mutation without revision advance.
- Snapshot/restore of profiles, modifiers, tracks, perks, unlocks, milestones, generators and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Attribute, track, perk, unlock and milestone definition registration plus freeze validation.
- [ ] Profile create/remove and base-attribute mutation.
- [ ] Modifier add/remove/replace-by-source and service-wide modifier identity.
- [ ] GrantProgress/SetProgress rank transitions and arithmetic boundaries.
- [ ] ReserveProgressGrant/CommitProgressGrant/ReleaseProgressGrant exact reservation semantics.
- [ ] Perk and unlock grant/revoke semantics plus milestone evaluation and reward deduplication.
- [ ] Revision and journal exhaustion must reject before mutation; no successful mutation without revision advance.
- [ ] Snapshot/restore of profiles, modifiers, tracks, perks, unlocks, milestones, generators and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Construction

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Construction`. Admission: 108 API, 128 obligations, 5 lifecycle, 29 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Construction/include`, `EngineFramework/GameplayWorldStateOwners/Construction/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/construction_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Placement rules/recipes registration.
- Placement validation.
- Plan lifecycle.
- Cost reservation.
- Socket reservation.
- Site lifecycle.
- Commit/cancel placement.
- Output creation.
- Provider failure.
- Collision/conflicting socket rejection.
- Snapshot/restore plans, sites, reservations, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Placement rules/recipes registration.
- [ ] Placement validation.
- [ ] Plan lifecycle.
- [ ] Cost reservation.
- [ ] Socket reservation.
- [ ] Site lifecycle.
- [ ] Commit/cancel placement.
- [ ] Output creation.
- [ ] Provider failure.
- [ ] Collision/conflicting socket rejection.
- [ ] Snapshot/restore plans, sites, reservations, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Traversal

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Traversal`. Admission: 82 API, 92 obligations, 3 lifecycle, 21 stale, 0 external candidates.

Confirmed defects: `G4-TRAV-001`, `G4-TRAV-002`

Код: `EngineFramework/GameplayWorldStateOwners/Traversal/include`, `EngineFramework/GameplayWorldStateOwners/Traversal/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/traversal_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Traversal capability/route/action definitions.
- Begin/progress/complete/cancel lifecycle.
- Capability validation.
- Cost/reservation semantics.
- Stale world/entity references.
- Failure leaves traversal state unchanged.
- Snapshot/restore.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Traversal capability/route/action definitions.
- [ ] Begin/progress/complete/cancel lifecycle.
- [ ] Capability validation.
- [ ] Cost/reservation semantics.
- [ ] Stale world/entity references.
- [ ] Failure leaves traversal state unchanged.
- [ ] Snapshot/restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.7. B07: World, Save/Restore and Narrative

Локальная persistence orchestration, world state, narrative state и durable narrative consequence outbox.

Workload: 6 modules, 6 public headers, 405 public API, 396 mutation obligations, 11 lifecycle, 92 stale-identity, 1 external-boundary candidates.

Known admission defects: `G4-NARRINT-001`, `G4-NARRINT-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### World

Freeze unit: `EngineFramework/GameplayWorldStateOwners/World`. Admission: 77 API, 92 obligations, 1 lifecycle, 22 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/World/include`, `EngineFramework/GameplayWorldStateOwners/World/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/world_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Feature/area/placement create/update/remove.
- Alteration lifecycle.
- Spatial index consistency.
- Transaction commit/cancel.
- Alteration ID generator staging.
- Invalid enum rejection.
- Large alteration index.
- Journal atomicity.
- Snapshot restore builds all indexes off-state.
- Failed restore leaves old world unchanged.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Feature/area/placement create/update/remove.
- [ ] Alteration lifecycle.
- [ ] Spatial index consistency.
- [ ] Transaction commit/cancel.
- [ ] Alteration ID generator staging.
- [ ] Invalid enum rejection.
- [ ] Large alteration index.
- [ ] Journal atomicity.
- [ ] Snapshot restore builds all indexes off-state.
- [ ] Failed restore leaves old world unchanged.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### SaveGame

Freeze unit: `EngineFramework/GameplayWorldStateOwners/SaveGame`. Admission: 35 API, 40 obligations, 1 lifecycle, 6 stale, 1 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/SaveGame/include`, `EngineFramework/GameplayWorldStateOwners/SaveGame/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/save_game_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Barrier binding and capture/restore lease acquisition/release semantics.
- Participant registration, duplicate IDs, required/optional participants and registry freeze.
- Migration registration and deterministic dependency/order resolution.
- Capture produces an in-memory SaveGameImage with structural metadata and per-section hashes.
- Participant capture failure is surfaced without partial image publication.
- Image structure, format/version compatibility and section hash validation.
- Migration chain validation and migration failure before restore publication.
- Restore stages all participants first; every fallible operation must complete before noexcept CommitRestore begins.
- Participant validation/staging failure leaves live state unchanged; commit ordering follows resolved dependencies.
- Persistent/session separation and whole-framework image metadata.
- Do not add storage/file I/O or serializer responsibilities to this module in Goal 4; those are whole-engine Goal 5 concerns.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Barrier binding and capture/restore lease acquisition/release semantics.
- [ ] Participant registration, duplicate IDs, required/optional participants and registry freeze.
- [ ] Migration registration and deterministic dependency/order resolution.
- [ ] Capture produces an in-memory SaveGameImage with structural metadata and per-section hashes.
- [ ] Participant capture failure is surfaced without partial image publication.
- [ ] Image structure, format/version compatibility and section hash validation.
- [ ] Migration chain validation and migration failure before restore publication.
- [ ] Restore stages all participants first; every fallible operation must complete before noexcept CommitRestore begins.
- [ ] Participant validation/staging failure leaves live state unchanged; commit ordering follows resolved dependencies.
- [ ] Persistent/session separation and whole-framework image metadata.
- [ ] Do not add storage/file I/O or serializer responsibilities to this module in Goal 4; those are whole-engine Goal 5 concerns.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Narrative

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Narrative`. Admission: 169 API, 140 obligations, 4 lifecycle, 34 stale, 0 external candidates.

Confirmed defects: `G4-NARRINT-001`, `G4-NARRINT-002`

Код: `EngineFramework/GameplayWorldStateOwners/Narrative/include`, `EngineFramework/GameplayWorldStateOwners/Narrative/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/narrative_tests.cpp`, `EngineFramework/DevelopmentInfrastructure/Tests/narrative_choices_storylets_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Thread lifecycle.
- Objective lifecycle.
- Clue discovery.
- Choice resolve/cancel.
- Consequence prepare/commit/failure.
- Suspend/resume/fail thread.
- Duplicate consequence delivery prevention.
- Terminal execution compaction.
- Cursors/checkpoints.
- Snapshot/restore threads, objectives, choices, deliveries, generators, journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Thread lifecycle.
- [ ] Objective lifecycle.
- [ ] Clue discovery.
- [ ] Choice resolve/cancel.
- [ ] Consequence prepare/commit/failure.
- [ ] Suspend/resume/fail thread.
- [ ] Duplicate consequence delivery prevention.
- [ ] Terminal execution compaction.
- [ ] Cursors/checkpoints.
- [ ] Snapshot/restore threads, objectives, choices, deliveries, generators, journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### Dialogue

Freeze unit: `EngineFramework/GameplayWorldStateOwners/Dialogue`. Admission: 65 API, 60 obligations, 4 lifecycle, 15 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/GameplayWorldStateOwners/Dialogue/include`, `EngineFramework/GameplayWorldStateOwners/Dialogue/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/dialogue_tests.cpp`


Главные риски: multi-container state, generators/revisions/journal, lifecycle transitions, snapshot/restore, deterministic indices, allocation failure atomicity.

Функциональная поверхность, которую нужно доказать/доработать:

- Conversation definition/session lifecycle.
- Participant validation.
- Condition resolver success/failure.
- Option repeat policy.
- Consequence handler success/failure.
- Node transition validity.
- Conversation terminal state.
- Duplicate consequence execution prevention.
- Snapshot/restore sessions and journal.

Минимальный regression pack: non-empty snapshot roundtrip; invalid snapshot; max revision/generator; failed mutation atomicity; multi-index consistency; journal boundary; deterministic query ordering.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Conversation definition/session lifecycle.
- [ ] Participant validation.
- [ ] Condition resolver success/failure.
- [ ] Option repeat policy.
- [ ] Consequence handler success/failure.
- [ ] Node transition validity.
- [ ] Conversation terminal state.
- [ ] Duplicate consequence execution prevention.
- [ ] Snapshot/restore sessions and journal.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### NarrativeIntegration

Freeze unit: `EngineFramework/IntegrationLayer/NarrativeIntegration`. Admission: 43 API, 44 obligations, 1 lifecycle, 11 stale, 0 external candidates.

Confirmed defects: `G4-NARRINT-001`, `G4-NARRINT-002`

Код: `EngineFramework/IntegrationLayer/NarrativeIntegration/include`, `EngineFramework/IntegrationLayer/NarrativeIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/narrative_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Narrative semantic event mapping.
- Contract registry duplicate/freeze.
- Narrative external consequence outbox.
- Handler failure and retry.
- Save participant roundtrip.
- Knowledge -> Narrative reference.
- Duplicate external consequence prevention.
- External-consequence outbox revision exhaustion is a hard terminal boundary: no wrap to zero after restored max revision.
- Outbox Execute must not advance revision before a fallible delivery allocation/publication.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Narrative semantic event mapping.
- [ ] Contract registry duplicate/freeze.
- [ ] Narrative external consequence outbox.
- [ ] Handler failure and retry.
- [ ] Save participant roundtrip.
- [ ] Knowledge -> Narrative reference.
- [ ] Duplicate external consequence prevention.
- [ ] External-consequence outbox revision exhaustion is a hard terminal boundary: no wrap to zero after restored max revision.
- [ ] Outbox Execute must not advance revision before a fallible delivery allocation/publication.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### WorldIntegration

Freeze unit: `EngineFramework/IntegrationLayer/WorldIntegration`. Admission: 16 API, 20 obligations, 0 lifecycle, 4 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/WorldIntegration/include`, `EngineFramework/IntegrationLayer/WorldIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/world_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- World query adapters.
- World -> Facts projection.
- Environment sample mapping.
- Entity -> Interaction state provider.
- Alteration create/update/remove projection.
- Duplicate world change.
- Stale cursor.
- Restore checkpoint and resync.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] World query adapters.
- [ ] World -> Facts projection.
- [ ] Environment sample mapping.
- [ ] Entity -> Interaction state provider.
- [ ] Alteration create/update/remove projection.
- [ ] Duplicate world change.
- [ ] Stale cursor.
- [ ] Restore checkpoint and resync.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 6.8. B08: Integration Layer and Cross-owner Adapters

Оставшийся IntegrationLayer. Production ownership не пересекается с B01-B07; реальные end-to-end causal chains всё ещё остаются Goal 7.

Workload: 11 modules, 11 public headers, 305 public API, 492 mutation obligations, 8 lifecycle, 111 stale-identity, 4 external-boundary candidates.

Known admission defects: `G4-EXTINT-001`, `G4-EXTINT-002`

Общий fix/audit порядок блока:

1. Закрыть перечисленные confirmed defects минимальными ownership-local изменениями.
2. Для каждого owned module пройти все 37 LOCAL_READY criteria и 15 dossier fields.
3. Для каждого mutator проверить success/no-op/invalid/failure, generator/revision/journal и allocation publication.
4. Для snapshot owners проверить non-empty roundtrip, invalid restore, generator/cursor continuity, max boundaries и failed-restore atomicity.
5. Для providers/backends/adapters проверить accepted-work, duplicate, retry, stale identity и restore/reconciliation.
6. Мигрировать owned tests с process-global allocator fault injection на private/module-local seams там, где allocation failure используется как evidence.
7. Подготовить Bxx handoff и delta ZIP, не трогая canonical freeze files.

### Integration

Freeze unit: `EngineFramework/IntegrationLayer/Integration`. Admission: 34 API, 68 obligations, 1 lifecycle, 14 stale, 2 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/Integration/include`, `EngineFramework/IntegrationLayer/Integration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/core_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- FactsQueryAdapter query mapping.
- RuntimeTimeAdapter time projection.
- ScheduledTriggerDispatcher registration/freeze.
- Trigger handler failure.
- Delivery modes.
- Duplicate trigger prevention.
- Dispatcher checkpoint/save participant.
- TimeFactsAdapter exactly-once event projection.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] FactsQueryAdapter query mapping.
- [ ] RuntimeTimeAdapter time projection.
- [ ] ScheduledTriggerDispatcher registration/freeze.
- [ ] Trigger handler failure.
- [ ] Delivery modes.
- [ ] Duplicate trigger prevention.
- [ ] Dispatcher checkpoint/save participant.
- [ ] TimeFactsAdapter exactly-once event projection.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### StateIntegration

Freeze unit: `EngineFramework/IntegrationLayer/StateIntegration`. Admission: 52 API, 72 obligations, 0 lifecycle, 13 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/StateIntegration/include`, `EngineFramework/IntegrationLayer/StateIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/state_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Entity/material/condition queries.
- State -> Facts projection.
- Entity create/convert/tag effects.
- Material stimulus/exposure effects.
- Condition apply/remove effect delivery.
- Entity target state provider.
- Lifecycle adapter.
- State <-> Time processing.
- Deferred effect reconciliation.
- Combined checkpoint and persistence.
- Duplicate event/effect protection.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Entity/material/condition queries.
- [ ] State -> Facts projection.
- [ ] Entity create/convert/tag effects.
- [ ] Material stimulus/exposure effects.
- [ ] Condition apply/remove effect delivery.
- [ ] Entity target state provider.
- [ ] Lifecycle adapter.
- [ ] State <-> Time processing.
- [ ] Deferred effect reconciliation.
- [ ] Combined checkpoint and persistence.
- [ ] Duplicate event/effect protection.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### InteractionTimeIntegration

Freeze unit: `EngineFramework/IntegrationLayer/InteractionTimeIntegration`. Admission: 8 API, 16 obligations, 0 lifecycle, 4 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/InteractionTimeIntegration/include`, `EngineFramework/IntegrationLayer/InteractionTimeIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_time_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction -> scheduled time binding.
- Cancel interaction removes/invalidates schedule по контракту.
- Trigger after interaction terminal state.
- Duplicate schedule delivery.
- Reconciliation after restore.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Interaction -> scheduled time binding.
- [ ] Cancel interaction removes/invalidates schedule по контракту.
- [ ] Trigger after interaction terminal state.
- [ ] Duplicate schedule delivery.
- [ ] Reconciliation after restore.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### InteractionEffectsIntegration

Freeze unit: `EngineFramework/IntegrationLayer/InteractionEffectsIntegration`. Admission: 11 API, 24 obligations, 1 lifecycle, 5 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/InteractionEffectsIntegration/include`, `EngineFramework/IntegrationLayer/InteractionEffectsIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/interaction_effects_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Interaction completion -> Effects exactly once.
- Effect prepare failure.
- Effect commit failure.
- Reconciliation state.
- Duplicate interaction completion.
- Restore delivery records.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Interaction completion -> Effects exactly once.
- [ ] Effect prepare failure.
- [ ] Effect commit failure.
- [ ] Reconciliation state.
- [ ] Duplicate interaction completion.
- [ ] Restore delivery records.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### GameplayIntegration

Freeze unit: `EngineFramework/IntegrationLayer/GameplayIntegration`. Admission: 72 API, 132 obligations, 5 lifecycle, 31 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/GameplayIntegration/include`, `EngineFramework/IntegrationLayer/GameplayIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/gameplay_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Combat -> Effects damage delivery.
- Progression -> Combat modifier mapping.
- Combat <-> Ability resource reservation.
- Ability -> Effects delivery.
- Conditions -> Progression.
- Ability -> Time scheduled trigger.
- Ability output coordinator.
- Loot -> Progression rewards.
- Death reward delivery.
- Prepare/commit/release failures на каждом внешнем leg.
- Checkpoint restore без duplicate rewards/effects.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Combat -> Effects damage delivery.
- [ ] Progression -> Combat modifier mapping.
- [ ] Combat <-> Ability resource reservation.
- [ ] Ability -> Effects delivery.
- [ ] Conditions -> Progression.
- [ ] Ability -> Time scheduled trigger.
- [ ] Ability output coordinator.
- [ ] Loot -> Progression rewards.
- [ ] Death reward delivery.
- [ ] Prepare/commit/release failures на каждом внешнем leg.
- [ ] Checkpoint restore без duplicate rewards/effects.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### ExtendedGameplayIntegration

Freeze unit: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration`. Admission: 42 API, 68 obligations, 0 lifecycle, 17 stale, 1 external candidates.

Confirmed defects: `G4-EXTINT-001`, `G4-EXTINT-002`

Код: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/include`, `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/extended_gameplay_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Equipment <-> Items reservation/ownership.
- Processes <-> Items input/output.
- Dialogue -> Knowledge consequence.
- Economy/Items/Ownership coordinated trade.
- Second prepare failure rollback.
- Money leg committed, goods leg failed reconciliation.
- Goods leg committed, ownership leg failed reconciliation.
- Cancel path.
- Restore trade execution checkpoint.
- Повторный retry не дублирует transfer.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Equipment <-> Items reservation/ownership.
- [ ] Processes <-> Items input/output.
- [ ] Dialogue -> Knowledge consequence.
- [ ] Economy/Items/Ownership coordinated trade.
- [ ] Second prepare failure rollback.
- [ ] Money leg committed, goods leg failed reconciliation.
- [ ] Goods leg committed, ownership leg failed reconciliation.
- [ ] Cancel path.
- [ ] Restore trade execution checkpoint.
- [ ] Повторный retry не дублирует transfer.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### PerceptionKnowledgeAIIntegration

Freeze unit: `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration`. Admission: 14 API, 12 obligations, 0 lifecycle, 2 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration/include`, `EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/perception_knowledge_ai_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Observation -> Knowledge mapping.
- Duplicate observation.
- Knowledge update -> AI inputs.
- Missing knowledge.
- AI execution availability.
- Intent execution record exactly once.
- Stale observation/agent generation.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Observation -> Knowledge mapping.
- [ ] Duplicate observation.
- [ ] Knowledge update -> AI inputs.
- [ ] Missing knowledge.
- [ ] AI execution availability.
- [ ] Intent execution record exactly once.
- [ ] Stale observation/agent generation.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### PopulationSimulationIntegration

Freeze unit: `EngineFramework/IntegrationLayer/PopulationSimulationIntegration`. Admission: 14 API, 28 obligations, 0 lifecycle, 7 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/PopulationSimulationIntegration/include`, `EngineFramework/IntegrationLayer/PopulationSimulationIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/population_simulation_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Population backed encounter planning.
- Spawn success/failure.
- Encounter termination updates population once.
- Roles -> Needs mapping.
- Population lifecycle reconciliation.
- City life scheduled update.
- Snapshot restore plans/checkpoints.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Population backed encounter planning.
- [ ] Spawn success/failure.
- [ ] Encounter termination updates population once.
- [ ] Roles -> Needs mapping.
- [ ] Population lifecycle reconciliation.
- [ ] City life scheduled update.
- [ ] Snapshot restore plans/checkpoints.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### ProcessResourceSimulationIntegration

Freeze unit: `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration`. Admission: 22 API, 32 obligations, 0 lifecycle, 8 stale, 1 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration/include`, `EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/process_resource_simulation_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Processes <-> resource input reservation.
- Output prepare/commit.
- Provider failure.
- Resource quantity rollback.
- Simulation layer proposal/application.
- Duplicate simulation execution.
- Restore checkpoint/reconciliation.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Processes <-> resource input reservation.
- [ ] Output prepare/commit.
- [ ] Provider failure.
- [ ] Resource quantity rollback.
- [ ] Simulation layer proposal/application.
- [ ] Duplicate simulation execution.
- [ ] Restore checkpoint/reconciliation.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### SocialLegalIntegration

Freeze unit: `EngineFramework/IntegrationLayer/SocialLegalIntegration`. Admission: 28 API, 40 obligations, 1 lifecycle, 10 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/SocialLegalIntegration/include`, `EngineFramework/IntegrationLayer/SocialLegalIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/social_legal_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- Ownership -> Crime theft resolution.
- Crime -> Society relationship consequence.
- Authority response mapping.
- Duplicate crime input.
- Victim policy.
- Failed consequence delivery.
- Checkpoint restore без duplicate penalty.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] Ownership -> Crime theft resolution.
- [ ] Crime -> Society relationship consequence.
- [ ] Authority response mapping.
- [ ] Duplicate crime input.
- [ ] Victim policy.
- [ ] Failed consequence delivery.
- [ ] Checkpoint restore без duplicate penalty.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

### TraversalNavigationConstructionIntegration

Freeze unit: `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration`. Admission: 8 API, 0 obligations, 0 lifecycle, 0 stale, 0 external candidates.

Confirmed defects: не подтверждены на admission.

Код: `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration/include`, `EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration/src`. Tests: `EngineFramework/DevelopmentInfrastructure/Tests/traversal_navigation_construction_integration_tests.cpp`


Главные риски: accepted external work, durable local checkpoint/outbox, retry/idempotence, stale cursor/revision, partial progress, restore/reconciliation.

Функциональная поверхность, которую нужно доказать/доработать:

- TraversalNavigationCapabilityProvider maps traversal capabilities into NavigationSemantics without owning either state.
- TraversalNavigationAdapter::CanUseLink propagates stale/invalid subject and capability decisions correctly.
- ConstructionNavigationLayerPayload encoding/decoding validates durable payloads and stable layer identity.
- ConstructionNavigationAdapter::QueueNavigationOperation creates durable placement outputs without duplicate semantic operations.
- ProcessPendingOutputs performs idempotent add/update/remove, acknowledges only after Navigation commit, and supports retry after partial external progress.
- Duplicate delivery is idempotent by stable layer ID; already-applied state is acknowledged rather than duplicated.
- ConstructionTraversalAdapter::CancelTraversalSessions handles the documented partial-progress/retry contract without claiming batch atomicity that the API does not provide.
- Corrupt payloads, stale world references and restore/replay paths leave both sides reconcilable.

Минимальный regression pack: happy path; duplicate delivery; provider failure before/after accepted work; retry; stale identity/cursor; checkpoint roundtrip; allocation failure at durable local publication.

Правило исправления: сначала воспроизвести нарушение текущего контракта, затем исправить минимально возможный ownership-local участок. Если код уже удовлетворяет контракту, production code не менять, добавить только недостающее evidence/tests/docs.

Дополнительный checklist из исходного Goal 4:
- [ ] TraversalNavigationCapabilityProvider maps traversal capabilities into NavigationSemantics without owning either state.
- [ ] TraversalNavigationAdapter::CanUseLink propagates stale/invalid subject and capability decisions correctly.
- [ ] ConstructionNavigationLayerPayload encoding/decoding validates durable payloads and stable layer identity.
- [ ] ConstructionNavigationAdapter::QueueNavigationOperation creates durable placement outputs without duplicate semantic operations.
- [ ] ProcessPendingOutputs performs idempotent add/update/remove, acknowledges only after Navigation commit, and supports retry after partial external progress.
- [ ] Duplicate delivery is idempotent by stable layer ID; already-applied state is acknowledged rather than duplicated.
- [ ] ConstructionTraversalAdapter::CancelTraversalSessions handles the documented partial-progress/retry contract without claiming batch atomicity that the API does not provide.
- [ ] Corrupt payloads, stale world references and restore/replay paths leave both sides reconcilable.

Module exit:

- [ ] 37/37 criteria имеют `PASS/N/A`, `NOT_AUDITED = 0` в block-local projection.
- [ ] 15/15 dossier fields reviewed.
- [ ] Все API/obligation/candidate decisions имеют anchors/rationale.
- [ ] Все найденные defects имеют regression target/symbol.
- [ ] Module tests Debug/Release green.

## 7. Подтверждённые проблемы и план исправления

Ниже только подтверждённые проблемы baseline. Подозрительный код без доказанного contract violation остаётся audit-risk внутри соответствующего Bxx.

### G4-INFRA-001 · distributed across B01-B08

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h:40-50, 592-618`; helper напрямую включён в 31 test `.cpp`, ещё 2 shared sweep headers зависят от него.

Проблема: Framework allocation-failure tests rely on process-global `operator new/new[]/delete` replacement. The helper already contains an MSVC checked-STL exception for a two-pointer allocation, so some allocations are deliberately not injectable and unrelated CRT/STL bookkeeping can be faulted. Поэтому такой механизм не является надёжным freeze evidence.

Причина: fault model привязан ко всему процессу, а не к проверяемой mutation/publication boundary. MSVC Debug STL/CRT выполняет собственные allocations; `_ITERATOR_DEBUG_LEVEL` и checked-STL не позволяют helper отличать engine allocation от debug-runtime allocation.

Рекомендуемая доработка: определить один narrow fault-seam convention. Каждый Bxx переводит только owned module allocation-failure tests на private/module-local deterministic fail points в candidate construction, primary-container publication, index publication и journal/outbox publication. Shared helper остаётся read-only. После merge integrator проверяет 0 реальных пользователей и удаляет/deprecates shared helper и зависимые sweep paths.

Особенности окружения: official qualification выполняется на Windows 11 + MSVC `/W4 /WX`. GCC/Clang portable runs являются дополнительным, а не заменяющим evidence.

Риски исправления: нельзя заменить global injection на один искусственный fail point и потерять coverage остальных fallible publication boundaries. Нельзя добавлять public test API, Debug skips, catch-all success paths или новые size-based исключения.

Обязательные regressions: для каждого migrated module выполнить sweep каждой named fallible boundary и сравнить полный pre/post snapshot на failure. Финальная repository-проверка: process-global allocation override отсутствует, и ни одно Goal 4 evidence не зависит только от старого helper.

Критерий закрытия: defect получает regression symbols/targets в owned blocks, проходит Debug/Release, попадает в block handoffs и canonical defect registry; после serial convergence remaining users старого helper = 0.


### G4-DOC-001 · Framework docs

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/README.md:7; missing docs/GameFramework/README.md`.

Проблема: The Framework README points at a non-existent documentation root. Unlike Base and Runtime, Framework currently has no stable 52-module contract/audit documentation tree.

Причина: Framework grew faster than the freeze documentation layer and the old link was never brought into the current `docs/EngineFramework` convention.

Рекомендуемая доработка: Create `docs/EngineFramework/README.md` and stable per-module contract/audit docs or an equivalent generated/module-anchor structure. Each module document must state responsibility, owned state, dependencies, lifecycle, persistence boundary, limits, threading and `LOCAL_READY` status without claiming `FROZEN`.

Особенности окружения: These docs feed human review and canonical anchors. Generated evidence remains single-writer during serial convergence.

Риски исправления: Do not copy stale API descriptions from the old plan. Docs must describe current headers and must not move whole-engine Goal 5-9 responsibilities into Goal 4.

Обязательные regressions: Broken-link check plus canonical dossier/API anchor validation. All 52 module docs must resolve to actual module paths and current public surfaces.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-RB-001 · RuntimeBridge

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/RuntimeBoundary/RuntimeBridge/src/runtime_bridge.cpp:452-459`.

Проблема: `Visible()` subtracts and squares `float` coordinates in `float`. Finite Runtime positions can therefore overflow during subtraction or squaring, producing `inf`, an invalid direction or an invalid ray distance.

Причина: The bridge assumes finite `float` components imply finite `float` distance arithmetic. Runtime World accepts finite transforms without a small-magnitude bound.

Рекомендуемая доработка: Promote components to `double` before subtraction, compute an overflow-safe norm (`std::hypot` or scaled norm), validate representability of the resulting ray distance, then normalize in wide arithmetic before narrowing the unit direction. If the true distance cannot be represented by Runtime `RaycastQuery::max_distance`, return an explicit semantic-range failure instead of constructing a malformed ray.

Особенности окружения: `FLT_MAX` is a valid finite Runtime component. The opposite-sign `±FLT_MAX` case can overflow already at subtraction, not only at squaring.

Риски исправления: Simply casting the already-computed `delta` to `double` is insufficient. Clamping an unrepresentable max distance can create false visibility results.

Обязательные regressions: Finite `FLT_MAX` vs 0, opposite-sign large coordinates, near-zero delta, normal visible/occluded cases, and an unrepresentable true distance. Assert the backend never receives NaN/Inf direction or distance.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PROC-001 · Processes

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Processes/src/processes.cpp:226-243`.

Проблема: `EvaluateProgress()` computes an integer semantic progress value through `long double`. On MSVC, `long double == double`, so a process one tick before completion can round to 1,000,000 and appear complete.

Причина: The code assumes extended precision when converting very large `int64_t` durations to floating point.

Рекомендуемая доработка: Compute `floor(elapsed * 1'000'000 / total)` with portable overflow-safe integer/rational arithmetic and clamp only after the exact quotient is known.

Особенности окружения: Concrete MSVC boundary: `total=INT64_MAX`, `elapsed=INT64_MAX-1`. Exact result is 999999; binary64 produces 1000000.

Риски исправления: Using `__int128` alone is not portable to MSVC. Reuse or introduce a tested portable mul/div helper without expanding the public API.

Обязательные regressions: One tick before a huge completion boundary, exact half/third fractions, elapsed=0, elapsed=total, paused path, and partitioned time advancement.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-RESPROD-001 · ResourcesProduction

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:375-470, 610-649, 717-785, 900-970; RestoreSnapshot:1122-1297`.

Проблема: Several create/reserve/transfer APIs advance the real ID generator before revision preflight. A restored `revision=UINT64_MAX` can therefore return `revision_exhausted` after consuming an ID.

Причина: ID generation uses the live generator instead of a staged copy. Restore accepts boundary revision/generator states.

Рекомендуемая доработка: Copy the relevant generator, allocate the candidate ID from the copy, preflight revision/journal/publication capacity, stage containers/indices, then commit generator + revision + state together.

Особенности окружения: The bug is easiest to expose through a crafted valid snapshot because normal execution cannot practically reach `UINT64_MAX`.

Риски исправления: Caller-supplied IDs also call generator-advance helpers. Preserve monotonicity and duplicate semantics while making failures atomic.

Обязательные regressions: For stockpile, node, site, reservation, transaction, capability and plan creation: restore max revision, invoke with auto ID, expect failure and byte/semantic-equivalent snapshot including generator state.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-RESPROD-002 · ResourcesProduction

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/ResourcesProduction/src/resources_production.cpp:393-398, 638-648, 761-785, 1314-1323`.

Проблема: Revision/state changes occur before fallible `unordered_map::emplace`, index updates and `changes_.push_back`. Allocation failure can leave an advanced revision, partial primary/index state, or an escaping exception.

Причина: Mutation is committed incrementally instead of staging every fallible publication before the no-fail commit point.

Рекомендуемая доработка: Use candidate primary/index/journal state or guaranteed pre-reserved capacity. Stage all allocations first, then commit by no-throw swap/move/update. Add private fault seams at each publication phase.

Особенности окружения: MSVC Debug allocation behavior is exactly why the old global allocator harness is not acceptable evidence.

Риски исправления: Blindly copying every large map for every mutation may create unacceptable complexity. Prefer narrow candidate objects, `reserve`, node handles or local staged deltas where no-throw commit can be proven.

Обязательные regressions: Fault injection at primary insert, reserved-index insert, transaction publication and journal append. On failure verify revision, generator, primary maps, derived indices and journal are unchanged.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-SOC-001 · Society

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Society/include/Epidemic/GameFramework/Society/society.h:423-428; src/society.cpp:328-355, 376-423, 466-525; RestoreSnapshot:818-1022`.

Проблема: Several Society mutators ignore the `bool` result of `Bump()`. After restoring `revision=UINT64_MAX`, they still modify memberships, relationships or reputation while the global revision does not advance.

Причина: Some newer paths check `Bump()`, older paths retained fire-and-forget calls.

Рекомендуемая доработка: Centralize revision preflight and require it before every revision-bearing mutation. Mutations must fail before changing authoritative/derived state when the revision is exhausted.

Особенности окружения: Restore accepts max revision, so the boundary is reachable in tests and persisted state even if normal runtime would never perform 2^64 mutations.

Риски исправления: No-op/idempotent calls should remain successful without consuming revision if that is the current contract. Do not force bumps for true no-ops.

Обязательные regressions: Max-revision restore regressions for SetMembershipRole, RemoveMembership, existing/new SetRelationship, ApplySocialChange and SetReputation. Verify full snapshot equality on failure.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-SOC-002 · Society

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Society/src/society.cpp:283-307, 397-423, 1035-1044`.

Проблема: Membership/relationship ID generators and revision can advance before fallible map/index/journal publication. `Record()` performs a potentially allocating `deque::push_back` after state changes.

Причина: The service commits generator, primary state, derived indices and journal in multiple fallible steps.

Рекомендуемая доработка: Stage generator copies, primary record, all derived index inserts and journal append before committing. Define a single no-fail commit point.

Особенности окружения: Use narrow module-local fault seams after G4-INFRA-001. The existing global allocator helper is not sufficient.

Риски исправления: Society has bidirectional/semantic indices. A fix that protects only the primary map can still leave index corruption.

Обязательные regressions: Allocation/revision failures for AddMembership, RemoveMembership, new/existing relationship and reputation. Validate both query directions, generators, revision and journal.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PER-001 · Perception

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-307, 339-360`.

Проблема: Spatial deltas are formed after converting each `int64_t` millimetre coordinate to `long double`. On MSVC adjacent coordinates above 2^53 can become identical, corrupting distance and FOV decisions.

Причина: The implementation assumes `long double` has more integer precision than binary64.

Рекомендуемая доработка: Compute signed coordinate differences exactly before floating conversion, with an overflow-safe signed-difference representation, then use wide/scaled norm and dot-product logic. Do not subtract potentially opposite-sign int64 values directly in int64.

Особенности окружения: MSVC x64 defines `long double` with the same precision as `double`.

Риски исправления: A naive `a-b` in `int64_t` introduces signed overflow at opposite extremes. The fix must handle `[INT64_MIN, INT64_MAX]` coordinates.

Обязательные regressions: `2^53` vs `2^53+1`, opposite-sign extremes, axis-aligned and diagonal FOV thresholds, zero-length target vector and deterministic threshold equality.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PER-002 · Perception

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:930-970`.

Проблема: Awareness decay advances `last_decay_at` through `long double(steps) * long double(interval)`. On MSVC a mathematically valid value below `INT64_MAX` can round up to the boundary and advance time too far.

Причина: An exactly bounded integer product is unnecessarily converted through binary64.

Рекомендуемая доработка: After `steps = elapsed / interval`, compute the advance using checked integer multiplication. The product is mathematically <= elapsed, so this can be implemented without floating point and without overflow.

Особенности окружения: Concrete case: `last=0`, `now=INT64_MAX-1`, `interval=1`. Exact advance is `INT64_MAX-1`; binary64 rounds it to 2^63 and the current clamp chooses `INT64_MAX`.

Риски исправления: Preserve the current number-of-whole-intervals semantics. Do not change decay cadence or suspicion thresholds while fixing timestamp arithmetic.

Обязательные regressions: The concrete max-boundary case, exact-multiple/non-multiple intervals, repeated partitioned calls vs one combined call, and no overshoot of `now`.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PER-003 · Perception

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Perception/src/perception.cpp:300-336; RegisterSense:68-80`.

Проблема: `DistanceSquared()` saturates the squared distance into signed 64-bit `Fixed` before `DistanceMm()` takes the square root. Every real distance above `sqrt(INT64_MAX) ≈ 3,037,000,499 mm` collapses to roughly that same distance. Since `base_range_mm` has no corresponding upper bound, far targets can be falsely treated as in range.

Причина: Squared-distance storage uses the same 64-bit semantic type as linear distance.

Рекомендуемая доработка: Do not represent the intermediate square in `Fixed`. Compute linear distance directly with overflow-safe wide/scaled arithmetic, or compare exact/wide squared distance to squared range without narrowing. Only the final linear result should saturate to `Fixed` if required by contract.

Особенности окружения: This is toolchain-independent. Example: true distance 4,000,000,000 mm with range 3,500,000,000 mm is out of range, but the current saturated square yields approximately 3,037,000,499 mm.

Риски исправления: Changing distance math affects attenuation and identification thresholds. Preserve truncation/rounding semantics explicitly and update only tests that encoded the incorrect saturation.

Обязательные regressions: Distances below/at/above `sqrt(INT64_MAX)`, 4e9 vs 3.5e9 range, very large diagonal coordinates, attenuation monotonicity and FOV consistency.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-SIM-001 · Simulation

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Simulation/src/simulation.cpp:381-419`, restore boundary `579-684`.

Проблема: `ISimulationLayerExecutor::Commit()` может успешно принять внешнюю работу, после чего `SimulationService` только затем вызывает `Bump()`. При восстановленном `revision = UINT64_MAX` внешний commit уже выполнен, а Framework не может пометить task `Completed` и возвращает `revision_exhausted`.

Причина: локальная возможность durable publication проверяется после accepted external work. Контракт executor требует идемпотентный `Commit`, что защищает от двойного внешнего эффекта на retry, но не устраняет состояние, в котором локальная revision исчерпана навсегда и accepted work нельзя терминально зафиксировать.

Рекомендуемая доработка: до вызова внешнего `Commit` подготовить весь локальный terminal publication: проверить/зарезервировать revision и journal capacity, обеспечить no-fail локальную фиксацию состояния task/prepared summary. После успешного external Commit должна остаться только не бросающая локальная commit-фаза. Если нужен reconciliation state, он должен существовать до external accepted work.

Особенности окружения: boundary достижим через валидный `RestoreSnapshot`, который принимает snapshot revision до `UINT64_MAX`, в том числе active pending interval с prepared data.

Риски исправления: нельзя переносить внешний `Commit` до `Prepare`; нельзя нарушить существующее требование idempotence по stable `SimulationTaskId`; нельзя помечать task Completed до фактического успешного external Commit.

Обязательные regressions: восстановить prepared active interval при максимально допустимой revision; доказать, что external `Commit` не вызывается, если локальный terminal publication невозможен. Отдельно проверить recoverable commit failure, retry и save/load pending interval.

Критерий закрытия: regression target/symbol существует, путь выполняется в Debug/Release, defect добавлен в block handoff и canonical defect registry.


### G4-SIM-002 · Simulation

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Simulation/src/simulation.cpp:199-253`, `444-465`, `712-720`.

Проблема: локальная revision изменяется до fallible publication. `CreateIntervalExecution()` вызывает `Bump()` до `active_intervals_.emplace` и `Record()`. Финализация interval вызывает `Bump()` до `summaries_.push_back` и journal publication. `Record()` сам выполняет `deque::push_back` и не является `noexcept`. Allocation failure способен оставить продвинутую revision, частичное interval/summary state либо выбросить `std::bad_alloc` из API, которое возвращает `Result`.

Причина: fallible container/journal operations расположены после начала commit-фазы.

Рекомендуемая доработка: stage/copy только необходимое состояние, заранее зарезервировать или построить fallible interval/summary/journal publication, затем выполнить единый no-fail commit revision + generators + primary state + summary/journal. Добавить private module-local fault seams вместо process-global allocator.

Особенности окружения: существующий `simulation_tests.cpp` fault-sweep проверяет allocation failure только на `RestoreSnapshot`; mutation publication boundaries сейчас не закреплены.

Риски исправления: полное копирование всех больших containers на каждый tick может ухудшить complexity. Предпочтительны staged records, container reserve/node handles или другой доказуемый no-throw commit. Нельзя потерять resumable interval semantics и retention policy.

Обязательные regressions: fault injection перед `active_intervals_` publication, summary publication и journal publication. На failure должны совпадать revision, generators, active interval/region state, summaries и journal cursor.

Критерий закрытия: regression target/symbol существует, путь выполняется в Debug/Release, defect добавлен в block handoff и canonical defect registry.


### G4-COMBAT-001 · Combat

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Combat/src/combat.cpp:58-70, 286-307`.

Проблема: `ScaleRatioSat()` uses `long double` for exact-looking int64 ratio scaling. On MSVC this is binary64 and can produce the wrong integer near the int64 boundary.

Причина: The helper relies on floating-point precision to decide an integer quotient and saturation boundary.

Рекомендуемая доработка: Replace it with portable exact/saturating signed mul-div arithmetic. Define truncation toward zero explicitly.

Особенности окружения: Concrete MSVC case: `M=INT64_MAX`, `value=M-1`, `old_max=M`, `new_max=M-1`. Exact truncation is `M-2`; the current binary64 path rounds all three inputs to 2^63 and returns/clamps to `M`.

Риски исправления: MSVC has no standard `__int128`. Do not introduce a compiler-only implementation without a portable fallback and cross-toolchain tests.

Обязательные regressions: The concrete boundary, min/max signed combinations allowed by the API, zero/negative policy cases, preserve-ratio monotonicity and normal small values.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PROG-001 · Progression

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:305-319`.

Проблема: `RemoveProfile()` erases the profile before checking global revision exhaustion. It can return failure after authoritative state was removed.

Причина: Revision preflight is ordered after destructive mutation.

Рекомендуемая доработка: Check revision/journal publication capacity first, stage the journal record, then erase and publish in the no-fail commit phase.

Особенности окружения: Boundary is reachable by RestoreSnapshot with max revision.

Риски исправления: Profile removal also interacts with pending reservations. Preserve the existing reservation rejection semantics.

Обязательные regressions: Max revision and journal-publication failure preserve profile, tracks, modifiers, unlocks, indices/generators and journal.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PROG-002 · Progression

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:372-384, 747-799, 803-905, 1070-1097`.

Проблема: Several mutators alter profile state and then call `Bump()` while ignoring failure. `CommitProgressGrant()` can erase the reservation before proving revision capacity.

Причина: Revision and reservation lifecycle are not treated as part of the transaction.

Рекомендуемая доработка: Precompute all required revision increments and journal records, verify capacity, stage profile/reservation changes, then commit atomically. `CommitProgressGrant()` should consume its reservation only when the terminal outcome is durably determined.

Особенности окружения: Restore can inject max global/profile revisions and exhausted journal sequence.

Риски исправления: Milestone evaluation can emit multiple changes. Preflight the full count, not only one revision, and avoid double-granting unlocks on retry.

Обязательные regressions: Max global/profile revision for SetBaseAttribute, grant/revoke perk/unlock, progress commit and milestone event fan-out. Retry must not duplicate rewards.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-PROG-003 · Progression

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Progression/src/progression.cpp:462-492`.

Проблема: `AddModifier()` calls the live `modifier_ids_.Next()` and advances the generator before `CanBump()` and before `profile->modifiers.push_back`. A failed revision check or allocation can consume an ID.

Причина: ID generation is not staged with the rest of the mutation.

Рекомендуемая доработка: Use a staged generator copy and staged modifier publication; commit generator only after revision/journal and container publication are guaranteed.

Особенности окружения: This is visible in snapshots because modifier generator state is persisted.

Риски исправления: Caller-supplied modifier IDs must still advance the generator past accepted IDs without allowing duplicate reuse.

Обязательные regressions: Revision exhaustion and allocation failure leave modifier list and generator snapshot unchanged; caller-supplied high ID still advances the generator on success.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-TRAV-001 · Traversal

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Traversal/include/Epidemic/GameFramework/Traversal/traversal.h:470-475; src/traversal.cpp:215-260, 405-482, 540-678; RestoreSnapshot:829-943`.

Проблема: Most Traversal mutators ignore the `bool` result of `Bump()`. Restoring `revision=UINT64_MAX` allows state/session/route/carrier mutation with a stale revision.

Причина: Only a few older/newer paths use checked revision handling; the service lacks one enforced mutation preflight.

Рекомендуемая доработка: Require a checked next revision before every state-changing operation. Stage state/session/route/binding changes and publish only after preflight.

Особенности окружения: Restore accepts max revision. Journal exhaustion (`next_change_sequence=0`) is already intentionally tested as snapshot-required mode and is not automatically the same defect.

Риски исправления: Do not conflate revision exhaustion with the explicitly supported terminal journal behavior. Preserve documented idempotent/no-op outcomes.

Обязательные regressions: Max revision for capability grant/revoke, mode change, route add/remove, start/suspend/resume/finalize session, board and disembark. Full snapshot unchanged on failure.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-TRAV-002 · Traversal

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/GameplayWorldStateOwners/Traversal/src/traversal.cpp:552-564, 648-653, 965-974 and analogous route/grant paths`.

Проблема: After revision advancement, Traversal performs fallible map/deque publication (`emplace`, `Record::push_back`) and multi-container updates. Allocation failure can leave partial state or an advanced revision.

Причина: No common staged publication transaction exists for session/route/grant/binding mutations.

Рекомендуемая доработка: Stage ID generator, primary record, derived references and journal entry before the commit point. Use private fault seams at each publication phase.

Особенности окружения: Use the new Goal 4 narrow fault-injection convention, not global `operator new`.

Риски исправления: Session state is cross-linked from `TraversalState::active_session`; both sides must commit or roll back together.

Обязательные regressions: Fault each session/route/grant/carrier publication boundary and assert state/session cross-links, generators, revision and journal remain coherent.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-NARRINT-001 · NarrativeIntegration

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/NarrativeIntegration/include/Epidemic/GameFramework/NarrativeIntegration/narrative_adapters.h:164-183; src/narrative_adapters.cpp:709-749`.

Проблема: `NarrativeExternalConsequenceOutbox::Bump()` unconditionally increments revision. Restore accepts a snapshot revision, including the maximum value, so the next mutation can wrap to zero.

Причина: The outbox has persisted revision state but no checked-next semantics.

Рекомендуемая доработка: Introduce private checked next-revision/preflight and reject mutation before state change on exhaustion. Validate all outbox state transitions, not only Execute.

Особенности окружения: Boundary comes from persisted/restore state and is therefore valid Goal 4 local evidence.

Риски исправления: Do not change stable external operation IDs or retry/idempotence semantics while adding revision checks.

Обязательные regressions: Restore max revision and exercise Execute, retry/acknowledge/fail/prune transitions. No revision wrap and no state change on exhaustion.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-NARRINT-002 · NarrativeIntegration

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/NarrativeIntegration/src/narrative_adapters.cpp:551-595`.

Проблема: `Execute()` increments outbox revision before `deliveries_.push_back`, which can allocate. Allocation failure can advance revision without creating the durable delivery record.

Причина: Metadata is committed before the fallible outbox publication.

Рекомендуемая доработка: Build/stage the delivery and ensure vector capacity/publication first, then commit revision and record together. A private fail point should sit immediately before publication.

Особенности окружения: The outbox is the durability boundary for external consequences, so false publication is especially dangerous for retries.

Риски исправления: Do not execute any external consequence in this path while fixing local durability. Preserve duplicate execution matching semantics.

Обязательные regressions: Allocation failure before publication, duplicate Execute after failure, capacity boundary, restore/retry and terminal acknowledgement.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-EXTINT-001 · ExtendedGameplayIntegration

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:410-456`.

Проблема: `TradeCoordinator::Prepare()` allocates an execution ID before validating every goods line. Ordinary invalid input can therefore return failure after changing the persisted execution ID generator.

Причина: Generator mutation is performed before the full validation/staging phase.

Рекомендуемая доработка: Validate the complete plan first or allocate from a staged generator copy. Commit the generator only together with the durable local execution record.

Особенности окружения: `CaptureSnapshot()` includes coordinator generator/execution state, so this is directly observable failure atomicity.

Риски исправления: Preserve deterministic correlation between trade execution ID and the economy transaction ID (`plan.money.id = id.value`).

Обязательные regressions: Party-valid but malformed goods, duplicate item, unavailable item and ownership mismatch must leave coordinator snapshot/generator unchanged.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.


### G4-EXTINT-002 · ExtendedGameplayIntegration

Статус: подтверждённая проблема admission baseline.

Где: `EngineFramework/IntegrationLayer/ExtendedGameplayIntegration/src/extended_gameplay_adapters.cpp:458-536`.

Проблема: Goods and money reservations are accepted by external owners before the local `executions_.emplace`. If local publication allocates/fails, external work can exist without a durable local execution/reconciliation record. Rollback-failure branches also rely on a late `emplace`.

Причина: The coordinator creates its durable ownership/reconciliation record after accepted external side effects.

Рекомендуемая доработка: Publish a durable local Prepared/Staging execution record, or otherwise guarantee non-throwing local capacity, before the first external reservation. Update the record after each accepted leg. If rollback fails, reconciliation state must already exist.

Особенности окружения: This is a local IntegrationLayer contract and belongs to Goal 4 even though real multi-owner causal-chain stress remains Goal 7.

Риски исправления: A premature record must not be mistaken for a fully prepared transaction after restore. Define explicit staging/reconciliation states and retry rules.

Обязательные regressions: Local publication failure before first external call means zero external calls. Failure after one accepted goods leg leaves a durable reconciliation record. Retry is idempotent and does not double-reserve.

Критерий закрытия: defect получает отдельный regression symbol/target, проходит Debug/Release и попадает в block handoff + canonical defect registry.

## 8. Обязательные audit-risks, которые нельзя автоматически считать defects

- Любой direct `Bump()`/revision increment требует проверки restore reachability и preflight. Если boundary недостижим по действующему contract, это не defect.
- Journal sequence `0` может быть намеренным terminal/snapshot-required состоянием. Не «исправлять» его без доказательства нарушения documented behavior.
- Live generator `.Next()` не всегда ошибка: некоторые APIs намеренно присваивают stable request ID даже rejected request. Требуется сравнить observable contract и snapshot semantics.
- `long double` является defect только там, где correctness реально зависит от дополнительной integer precision. На MSVC он равен `double`.
- Allocation после mutation является defect, если API обещает failure atomicity или durable reconciliation отсутствует. Если explicit reconciliation state уже существует до accepted work, надо доказать его, а не переписывать код.

Повторный scan отдельно проверил AI, Knowledge, NeedsLife и Population revision patterns. У AI опасные-looking `(void)Bump()` paths предварительно защищены `CanBump()/CanRecord()`. `Knowledge::Decay()` заранее резервирует достаточное количество revision increments. `NeedsLife::SweepExpiredPressures()` проверяет весь batch count до прямых increments. Эти места не внесены в defect list без нового reproduction.

## 9. Serial merge через Codex после восьми delta-архивов

После получения B01-B08 основной проект меняет только serial integrator:

1. Проверить manifests каждого delta и отсутствие файлов чужого ownership.
2. Применить дельты в порядке B01-B08. При textual conflict не выбирать случайную сторону: сверить ownership. Конфликт двух Bxx в одном production/test file является ошибкой плана/дельты.
3. Обработать `cross_block_findings.md`: направить fix владельцу либо сделать отдельный serial fix только если ownership уже очевиден и regression добавлен.
4. Проверить `allocation_fault_injection.h` references. Если пользователей не осталось, удалить старый helper и обновить только serial-owned shared sweep/CMake infrastructure.
5. Слить 8 handoff в canonical `module_dossier_reviews`, `coverage reviews`, `public_api_anchors`, defect registry и ledger.
6. Однократно перегенерировать `module_dossiers`, `coverage_manifests`, `public_api_inventory`, `public_surface_manifest`, `local_ready_contract`.
7. Проверить любой public API/header delta относительно admission baseline 3054 callables / 59 Framework headers. Изменение допускается только с явным rationale и regression/evidence.
8. Требовать `EngineFramework = 52/52 LOCAL_READY`, каждый 37/37 `PASS/N/A`, dossiers 15/15 reviewed, unresolved candidates = 0.

Шесть фактических public-header deltas и их regressions зафиксированы в `docs/freeze/goal4_public_surface_review.md`; callable IDs/signatures не изменились. Это не закрывает отдельный per-API evidence gate.

## 10. Финальный qualification gate Goal 4

- [x] Base Debug/Release, Runtime Debug/Release, Full Debug/Release green; exact CTest manifests совпадают. Base/Runtime qualification не затронута Framework-only delta; Full Debug/Release перепроверены после merge.
- [x] Все Framework module/integration targets green; disabled/skipped без explicit freeze exception = 0.
- [x] MSVC `/W4 /WX` green.
- [x] MSVC public-header self-containment green.
- [x] Все architecture/freeze validators и self-tests green.
- [x] `git diff --check` clean.
- [ ] Remote Architecture Freeze CI на recorded SHA green, включая ClangCL public-surface.
- [ ] Framework module count = 52, `LOCAL_READY = 52/52` (сейчас 0/52; 52/52 `BLOCKED` из-за недостаточно точного evidence).
- [x] Framework dossiers = 15/15 reviewed per module.
- [ ] Framework 37 criteria = 37/37 `PASS/N/A` per module (47 модулей содержат `BLOCKED` criterion).
- [ ] Framework public API `UNCLASSIFIED = 0`, complete contract/test anchors (классификация есть, но 1729 test/audit anchors указывают на `main()`/общую проверку; 621 contract anchors слишком общие).
- [ ] Framework mutation/lifecycle/stale/external unresolved = 0 (формальные statuses проставлены; содержательные доказательства ещё не закрыты).
- [ ] Все canonical defect records имеют существующий вызываемый regression target/symbol (B06 defects восстановлены; все G4 targets перечислены точно и зарегистрированы, 19 Framework/Goal 4 regression entries пока не имеют точного line anchor).
- [x] Process-global allocator fault mechanism больше не используется Framework Goal 4 tests.
- [ ] Все 52 module docs отражают проверенный canonical status и не заявляют `FROZEN` (индекс путей исправлен; block-local candidate/status нужно сверить после evidence review).

Критерий выхода: Goal 4 = `COMPLETE`, все 52 Framework production modules локально стабильны и не переносят module-local code/evidence debt в Goal 5.

## 11. Итог повторной проверки

Восемь блоков покрыли 52/52 Framework modules ровно по одному разу. Все delta применены. Шесть пропущенных B06 problem IDs и отдельная B06 infrastructure-регрессия восстановлены в canonical defect registry. Формальная schema-валидация evidence проходит, но `python docs/freeze/goal4_evidence_quality.py --check` выявляет несемантические якоря и не даёт считать локальный Goal 4 закрытым. Старый allocator harness удалён после подтверждения отсутствия пользователей в исходном коде.

Подтверждённый admission defect set теперь содержит 23 ID. Новые `G4-SIM-001/002` добавлены после повторного анализа и не были в прошлой версии. Остальные suspicious patterns остаются обязательными audit-risks, а не объявлены проблемами без доказательства.

Этот документ является действующим подробным планом Goal 4 и заменяет `Milestone_4_EngineFramework_Local_Closure_20_Block_Plan_2026-09-22.md` для выполнения работ.
