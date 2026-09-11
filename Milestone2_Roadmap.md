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
| Effects | `UNPROVEN` |
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

### 2. Усилить общий fault-injection harness

Доработать `EngineFramework/DevelopmentInfrastructure/Tests/allocation_fault_injection.h`:

1. Добавить bounded sweep helper, принимающий имя API, максимальный fault index и callback одного запуска.
2. Возвращать диагностический результат: API, fault index, был ли вызван метод, тип отказа и достигнутый postcondition.
3. Различать `std::bad_alloc`, контролируемый `Result` с allocation code, callback failure и успешный вызов.
4. Гарантировать отключение fault injection через RAII перед capture/compare postconditions.
5. Сохранить исключение для внутренней 16-байтовой bookkeeping allocation MSVC checked iterators.
6. Ограничить каждый sweep числом реально наблюдаемых allocations плюс небольшой запас; отсутствие успешного завершающего прохода считать ошибкой теста.
7. Не использовать большие load fixtures для fault sweeps. Для каждого API создавать минимальное валидное состояние.

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
