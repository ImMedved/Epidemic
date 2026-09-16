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

# Цель 1. Восстановить проверяемый baseline и зафиксировать единый freeze contract

Эта цель выполняется до массовой проверки модулей.

## 1.1. Восстановить build и architecture gates

[~] Вернуть или заново создать `cmake/ArchitectureFreeze.cmake`, который уже ожидается top-level CMake.

[x] Валидатор должен проверять отсутствие Base -> Runtime/Framework зависимостей.

[x] Валидатор должен проверять отсутствие Runtime -> Framework зависимостей.

[~] Для Runtime majors разрешить только явно утвержденные dependencies, прежде всего RuntimeFoundation.

[x] Framework BaseInfrastructure и GameplayWorldStateOwners не должны напрямую зависеть от Runtime.

[~] RuntimeBridge остается утвержденной Runtime boundary.

[~] Direct Runtime dependencies IntegrationLayer разрешаются только по центральному allowlist.

[~] Production includes сканируются вместе с CMake link dependencies.

[~] Каждый public header компилируется отдельной translation unit.

[~] Вернуть CI workflow, который запускает те же проверки, что локальный build.

[~] Удалить расхождения между `docs/architecture.md`, CMake validator и фактическими allowlists.

Повторная проверка 2026-09-14:

- Base Debug configure с `CMAKE_BUILD_TYPE=Debug`, Runtime/Framework `OFF/OFF` и включенным architecture gate проходит.
- Runtime Debug configure с `CMAKE_BUILD_TYPE=Debug`, Runtime/Framework `ON/OFF` и включенным architecture gate проходит.
- Full Debug и Full Release configure с Runtime/Framework `ON/ON`, явным `CMAKE_BUILD_TYPE` и включенным architecture gate проходят.
- Self-containment повторно проверен явной сборкой target для Base, Runtime и Full.
- Base покрывает `52`, Runtime `173`, Full `232` отдельных public header translation units.
- Предыдущая проверка ошибочно считала шаги Ninja за число headers и не заметила, что glob пропускал все `59` Framework headers из-за дополнительного уровня каталогов. Glob исправлен, подпункт помечен `[~]`.
- При первом full configure валидатор ошибочно отклонил разрешенную RuntimeBridge dependency из-за `$<LINK_ONLY:...>` generator expression; исправлено нормализацией link dependency перед allowlist checks, поэтому затронутые подпункты помечены `[~]`.
- IntegrationLayer include allowlist теперь проверяет каждый `#include` отдельно: разрешенный include больше не может маскировать второй запрещенный include в том же файле.
- RuntimeBoundary link dependencies и Runtime include prefixes проверяются по центральным allowlists.
- CI matrix повторяет Base Debug, Runtime Debug, Full Debug и Full Release configure/build/self-containment/test gates.
- Повторный review обнаружил, что self-containment собирался с глобальными include roots и не доказывал корректную consumer visibility. Python architecture gate усилен: include из public header требует `PUBLIC/INTERFACE` direct link, include только из implementation допускает `PRIVATE`.
- Усиленный gate нашел и исправил `60` модулей, которые публично включали Foundation, но объявляли `EpidemicFoundation` как `PRIVATE`. После promotion всех edges четыре независимых configure/build профиля и полный test baseline прошли.
- Generated header TUs создавались через безусловный `file(WRITE)`, поэтому каждый configure провоцировал полную пересборку. Генерация переведена на content-aware `file(CONFIGURE`; повторный configure/self-containment во всех четырех профилях подтверждает `ninja: no work to do`.
- Hard-freeze review обнаружил, что production include scanner рекурсировал только от `EngineBase/include`, `EngineRuntime/include` и `EngineFramework/include`, а реальные headers/sources лежат глубже в каталогах модулей. Scan root исправлен на весь layer с исключением Apps/Tests/DevelopmentInfrastructure; CMake negative suite принимает valid fixture и отвергает `10/10` запрещенных dependency/include fixtures.

## 1.2. Зафиксировать текущий build baseline

На Windows 11 получить независимые результаты:

[x] Base Debug configure/build/test.

[x] Runtime Debug configure/build/test без Framework.

[~] Full Debug configure/build/test.

[~] Full Release configure/build/test.

[x] Зафиксировать точное число tests и durations.

[x] Зафиксировать compiler/toolset, CMake и generator.

[x] `git diff --check` либо эквивалентная проверка чистоты дерева.

Baseline 2026-09-14, Windows 11 (`10.0.26200.7462`):

- Base Debug: configure/build проходят; CTest `9/9`, `12.25 s`; self-containment `52` headers.
- Runtime Debug без Framework: configure/build проходят; CTest `29/29`, `37.19 s`; self-containment `173` headers.
- Full Debug: configure/build проходят; CTest `88/88`, `335.01 s`; self-containment `232` headers.
- Full Release: configure/build проходят; CTest `88/88`, `13.33 s`; self-containment `232` headers.
- Toolchain: MSVC `19.50.35729.0`, toolset path `14.50.35717`, CMake `4.2.3-msvc3`, Ninja `1.12.1`, generator `Ninja`, C++20.
- Для single-config Ninja в каждом configure явно задан `CMAKE_BUILD_TYPE=Debug` или `Release`; включены `BUILD_TESTING=ON` и `EPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON`.
- Первые Full Debug/Release builds обнаружили MSVC PDB/object path failure в длинных IntegrationLayer binary directories. Для трех длинных CMake subtrees заданы короткие binary directories, после чего обе конфигурации пересобраны и полностью прошли; подпункты помечены `[~]`.
- `git diff --check` проходит. Git сообщает только ожидаемые LF-to-CRLF conversion warnings, whitespace errors отсутствуют.
- Исторический baseline с `EPIDEMIC_WARNINGS_AS_ERRORS=OFF` содержал MSVC `C4834`, `C4702` и `C4100`. Долг устранен: ignored `[[nodiscard]]` results помечены явно, исчерпывающие `std::visit` используют `if constexpr ... else`, неиспользуемый bookkeeping parameter обозначен намеренно; все четыре актуальных профиля собираются с `EPIDEMIC_WARNINGS_AS_ERRORS=ON` (`/W4 /WX`) без исключений.
- Повторный baseline после исправления CMake visibility: Base Debug `9/9` за `0.97 s`, Runtime Debug `29/29` за `8.37 s`, Full Debug `88/88` за `194.80 s`, Full Release `88/88` за `12.00 s`. Все четыре configure/build/self-containment профиля прошли; различие durations с первым прогоном является ожидаемым влиянием состояния машины/cache, test counts неизменны.
- Финальный hard-freeze baseline 2026-09-15 с warnings-as-errors: Base Debug `9/9` за `1.02 s`, Runtime Debug `29/29` за `14.72 s`, Full Debug `89/89` за `240.63 s`, Full Release `89/89` за `11.87 s`. После тестов повторные configure и явная сборка `EpidemicPublicHeaderSelfContainment` во всех четырех профилях проходят с `ninja: no work to do`.

Пока baseline не воспроизводится, дальнейшие результаты аудита не являются freeze evidence.

## 1.3. Единый dossier каждого production-модуля

Для каждого из 78 модулей обязательно записать:

[x] Ответственность модуля.

[~] Public headers и public types.

[x] Dependency list.

[~] External ports/callbacks/providers/backends.

[~] Authoritative state.

[~] Derived/cache/index state.

[~] ID spaces, generations, revisions и cursors.

[~] State machines.

[~] Persistent и transient state.

[~] Snapshot/restore contract.

[~] Threading contract.

[~] Public mutation API.

[~] Read/query API, которыми проверяются invariants.

[x] Локальные invariants.

[~] Hard limits, budgets и ожидаемые complexity bounds.

Результат 2026-09-14:

- Создан `docs/freeze/module_dossiers.md` со статусом `BASELINE_INVENTORIED`: `78/78` production-модулей, `232/232` public headers и `15/15` обязательных полей на модуль.
- Инвентарь точно совпадает с CMake: EngineBase `9`, EngineRuntime `17`, EngineFramework `52`.
- `docs/freeze/module_dossiers.py --check` сверяет module set, layer counts, public headers, responsibilities, все module markers, все поля и полное соответствие сгенерированного dossier текущему дереву.
- Dossier добавлен в индекс документации и в CI architecture-freeze workflow. CI проверяет его один раз в Full Debug job до configure/build/test.
- Статус не повышает модули до `LOCAL_READY`: отсутствующий public snapshot, threading guarantee или complexity bound записан явно как audit debt целей 2-6.
- Первая версия lexical inventory давала ложные public API и type classifications: private helper calls и private nested types попадали в public surface, substring-поиск путал `Id`/`State` с частями других слов, а нетипично названные non-const mutators терялись. Парсер исправлен на visibility-aware public class/struct declarations, namespace-level free functions, const/static/read classification и CamelCase/suffix matching.
- Authoritative/derived state дополнен storage members и private implementation state; threading statements берутся только из public contracts, а отсутствие гарантии записывается как external-serialization debt. Все затронутые подпункты помечены `[~]`.
- При повторной проверке 1.3 старый name-level callable extractor оказался слабее нового exact inventory 1.5. Dossier переведен на те же signature IDs и anchors для mutation/lifecycle/callback и query/factory API; `UNCLASSIFIED` count сохраняется явно, поэтому два baseline-документа больше не могут молча расходиться.
- Negative self-test dossier discovery принимает production inventory и отвергает `4/4` испорченных module inventories; это проверяется CI, а не остается предположением генератора о самом себе.

## 1.4. Единый критерий `LOCAL_READY`

Каждый модуль без исключения должен доказать:

### Architecture и ownership

[x] Одна определенная ответственность.

[x] Нет второго authoritative owner тех же данных.

[~] Нет скрытых peer/lower-level dependencies.

[x] Внешние системы используются через утвержденные interfaces/ports.

Результат Architecture и ownership 2026-09-14:

- Созданы `docs/freeze/architecture_ownership_matrix.md` и воспроизводимый gate `docs/freeze/architecture_ownership.py`: покрыты `78/78` production-модулей.
- Для каждого модуля зафиксированы одна ответственность, authoritative domain либо явный статус non-owner, наблюдаемые cross-module includes и внешний boundary/port contract.
- Gate проверяет точное совпадение 78 модулей с реестром responsibilities, уникальность всех authoritative domains, наличие прямого CMake link edge для каждого cross-module public include и отсутствие production include через `..`/чужой `src`.
- Первая проверка нашла скрытые транзитивные зависимости: `EpidemicEngineBaseSupport -> EpidemicFoundation`, `EpidemicRuntimeResources -> EpidemicFoundation` и `EpidemicGameFrameworkGameplayIntegration -> EpidemicGameFrameworkSupportRandom`. Они добавлены как явные CMake dependencies, поэтому подпункт отмечен `[~]`.
- Прямой доступ к Win32/D3D SDK подтвержден только в `EngineBase/Platform/src/Windows` и `EngineBase/RHI_D3D11/src`; внешняя линковка ограничена `user32`/`shell32` и `d3d11`/`dxgi` соответственно. Остальные модули взаимодействуют через project contracts/ports; новый SDK include или link вне allowlist ломает gate.
- При повторной проверке direct dependency gate расширен проверкой visibility. Исправлены `60` скрытых consumer dependencies на Foundation; public include теперь требует `PUBLIC/INTERFACE` link, поэтому глобальные self-containment include roots больше не могут замаскировать этот класс нарушения.
- Authoritative domains больше не выводятся автоматически из layer/module path: `architecture_ownership.py` содержит явно reviewed registry всех state owners и отдельный registry non-owner/coordinator modules. Gate требует точного совпадения registry с production inventory и запрещает overlap/duplicate ownership.
- Architecture Python self-test принимает production graph и отвергает duplicate ownership, missing responsibility, malformed dependency visibility, multi-include и external SDK boundary fixtures. CMake self-test независимо отвергает `10/10` запрещенных link/include graph fixtures.
- Проверка добавлена в `architecture-freeze.yml` перед configure/build/test Full Debug job. Этот результат закрывает только subsection Architecture и ownership; остальные критерии `LOCAL_READY` не считаются выполненными автоматически.

### Public contracts

[~] Каждый public callable классифицирован.

[~] Для каждого mutator определены preconditions.

[~] Для каждого mutator определены success postconditions.

[~] Для каждого mutator определены failure semantics.

[~] Overloads считаются отдельными contracts.

[~] Invalid enums, invalid IDs, malformed payloads и stale handles отклоняются однозначно.

### State consistency

[~] Primary state согласован.

[~] Все indexes согласованы с primary state.

[~] Generators/generations/revisions/cursors имеют проверенное boundary behavior.

[~] Failed operation не публикует revision/event/journal record ложной mutation.

[~] No-op semantics определена явно.

### Lifecycle

[~] Все допустимые transitions проверены.

[~] Все запрещенные transitions отклоняются.

[~] Shutdown/cleanup semantics проверены.

[~] Retry cleanup проверен там, где внешний cleanup может отказать.

### Failure atomicity

[~] Single-record mutation не оставляет partial record.

[~] Multi-container mutation commit-ит все либо ничего.

[~] External prepare/commit/cancel/rollback contracts проверены.

[~] Durable reconciliation используется, если rollback сам может отказать.

### Persistence, если применимо

[~] Полный snapshot.

[~] Полная validation до live mutation.

[~] Restore строит candidate off-state.

[~] Failed restore не меняет live state.

[~] Successful restore восстанавливает generators, generations, revisions, cursors и persistent/transient boundary.

### Tests

[~] Happy path.

[~] Invalid input.

[~] Duplicate identity.

[~] Stale identity.

[~] Empty state.

[~] Boundary/overflow/underflow.

[~] Wrong lifecycle state.

[~] Callback/backend exception/failure.

[~] Regression test для каждого исправленного defect.

Результат полного определения `LOCAL_READY` 2026-09-14:

- Созданы `docs/freeze/local_ready_contract.md`, `docs/freeze/local_ready_ledger.json` и validator `docs/freeze/local_ready_contract.py`.
- Все `37/37` критериев имеют стабильный ID, точную applicability, обязательные evidence kinds и явное правило допустимости `N/A`; ledger содержит отдельную запись каждого критерия для `78/78` production-модулей.
- Public callable идентифицируется точной fully-qualified signature с header/line anchor; overloads не могут схлопываться по имени. Test anchor обязан указывать executable и точный case/function с assertions, простое текстовое совпадение API не считается evidence.
- `PASS` требует все заданные типы evidence (`contract`, `test`, `state`, `fault`, `lifecycle`, `persistence`, `architecture`); допустимый `N/A` требует module-specific rationale.
- Validator запрещает stale module/criterion set, nonexistent anchor file/line/symbol, unregistered test target, missing assertion anchors, неполный `PASS`, необоснованный `N/A`, несогласованный module status, пустой/ложный `BLOCKED` и `LOCAL_READY` при любом unresolved criterion. Self-test принимает полностью доказанный `LOCAL_READY` record и отклоняет `13` классов неправильного ledger.
- В ledger все 78 модулей имеют `PASS` по четырем уже доказанным Architecture/ownership критериям и статус `IN_AUDIT`, но сохраняется `0/78 LOCAL_READY`: этот пункт фиксирует единый admission contract цели 1 и не подменяет локальные аудиты целей 2-4. Новые подпункты отмечены `[~]`, потому что до этой работы формализованного проверяемого contract не было.
- Contract check и его negative self-test добавлены в Full Debug CI job.

## 1.5. API inventory используется только как coverage index

[~] Исправить scanner, чтобы он не терял overloads.

[~] Исправить prefix-классификацию `Cancel*` и других mutators.

[~] Неизвестный public non-const API должен становиться `UNCLASSIFIED`.

[~] Matrix должна отвечать: найден ли contract, классифицирован ли он, есть ли реальный test/audit anchor.

[~] Нельзя считать строку покрытой простым текстовым совпадением имени метода в tests.

[~] Matrix не заменяет module audit.

Результат 2026-09-14:

- Созданы `docs/freeze/public_api_inventory.py`, `docs/freeze/public_api_inventory.md` и explicit reviewed anchor registry `docs/freeze/public_api_anchors.json`.
- Исторический inventory этого прогона содержал `3872` signature-level строки из `232` public headers всех `78` production-модулей; `65` overload-групп были представлены `203` отдельными строками. Adversarial review 2026-09-16 доказал, что число неполно из-за обрезания declaration по `{` внутри default arguments/noexcept expressions, поэтому `3872` больше не является authoritative baseline и подлежит полной регенерации в 1.7.
- Сканер поддерживает Allman scopes, public/private visibility, inline и multiline declarations, template specialization и локальные struct/class macros. Первые версии теряли весь Allman-style API, схлопывали одинаковые specialization signatures, принимали macro body/invocation и constructor initializer list за callable; эти defects исправлены, поэтому подпункты отмечены `[~]`.
- В неполном историческом inventory все `18` найденных `Cancel*` имели classification `MUTATOR`, а `207` строк были сохранены как `UNCLASSIFIED`. Эти counts не переносятся в новый baseline: structural/semantic classification имеет подтвержденные ошибки для const snapshots, pure operators, const value-returning methods и namespace factories.
- Matrix явно показывает declaration, classification, contract anchor и test/audit anchor. Отсутствующий anchor отображается как `MISSING`, а не как покрытие.
- Scanner вообще не ищет API names в test source. Anchor принимается только из registry при существующем `path:line::symbol`, явном `reviewed=true`, а для test также при заданных target и reviewed assertions.
- Fixture self-test проверяет distinct overload IDs, `Cancel*`, const query, `UNCLASSIFIED`, private exclusion, callable-field exclusion, Allman scopes, macro expansion и отказ принять name-only test evidence.
- Вручную reviewed oracle (`fixtures/public_api_oracle.hpp` + `fixtures/public_api_oracle.json`) фиксирует `17` representative callables и ранее нашел false negative для `operator=`/`operator==`. Review 2026-09-16 доказал, что corpus недостаточен для completeness claim: в нем отсутствовали braced default arguments и nested braced expressions в `noexcept`.
- Документ явно имеет статус coverage index и не повышает module status; проверка inventory и self-test добавлены в Full Debug CI job.

## 1.6. Exit criteria цели 1

[x] Architecture validator реально существует и запускается.

[x] CI gate существует.

[x] Base/Runtime/Full baseline воспроизводится.

[x] 78 module dossiers имеют общий формат.

[x] Scanner completeness и semantic classification подтверждены независимым oracle/manifest после исправлений 1.7.

[~] Общие fault, state-compare и test utilities сами покрыты tests.

Результат exit-проверки цели 1, 2026-09-15:

- `cmake/ArchitectureFreeze.cmake` выполняется при configure с `EPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON`; gate проверяет слои, ownership, допустимые зависимости, public-header visibility и self-containment. Workflow `.github/workflows/architecture-freeze.yml` запускает validator, generated-document checks, negative self-tests, четыре build-профиля и CTest на push/PR.
- Актуальный строгий baseline с `EPIDEMIC_WARNINGS_AS_ERRORS=ON`: Base Debug `9/9` за `1.02 s`, Runtime Debug без Framework `29/29` за `14.72 s`, Full Debug `89/89` за `240.63 s`, Full Release `89/89` за `11.87 s`. Добавленный direct utility target увеличил Framework с `54` до `55` executables и Full с `88` до `89`; исторические прогоны `88/88` в разделе 1.2 оставлены как журнал предыдущего состояния.
- `module_dossiers.py --check` подтвердил `78/78` модулей, `232/232` public headers и единые `15/15` полей. Architecture/ownership check подтвердил `78/78` responsibilities и ownership domains.
- Исторический `public_api_inventory.py --check` подтвердил внутреннюю согласованность сгенерированных `3872` строк, но не полноту public surface. Review 2026-09-16 воспроизвел zero-row результат для valid declarations с `= {}`, обнаружил отсутствующие Base API, 48 ошибочных `CaptureSnapshot() const -> MUTATOR`, pure `operator| -> MUTATOR`, `Path::Join() const -> MUTATOR` и оборванную `ShuffleUnchecked` signature. Старые classification counts аннулированы до регенерации.
- Добавлен отдельный CTest target `EpidemicGameFrameworkTestUtilitiesTests`. Он напрямую проверяет allocation observation/injection/classification и bounded fallback, empty/missing/mismatch/captured state comparisons, mutation sweep fresh-fixture и rollback semantics, оба restore sweep overloads и baseline propagation. До этого mutation/restore wrappers проверялись только косвенно доменными suites, поэтому пункт отмечен `[~]`.
- В Release устранён `C4100` внутри allocation fault helper: размер bookkeeping allocation теперь явно считается намеренно неиспользуемым в конфигурациях без checked iterators.

Повторный hard-freeze review 2026-09-15:

- [~] Исправлен разрыв между описанием `LOCAL_READY` и enforcement: общий `evidence_anchors.py` теперь проверяет repository-relative path, существование файла/строки/symbol, CTest-регистрацию target и отдельные assertion anchors. `BLOCKED` требует rationale и согласованного module status. LOCAL_READY self-test отклоняет `13` malformed ledgers; тот же anchor validator используется API inventory.
- [~] Добавлены независимые negative fixtures: CMake принимает valid graph и отвергает `10/10` forbidden dependency/include graphs; Python architecture и dossier validators отвергают malformed discovery/ownership/visibility/SDK cases.
- [~] Добавлен вручную reviewed scanner oracle, который не генерируется lexical parser. Он исправил operator false negative и увеличил historical inventory с `3234` до `3872`, но второй review нашел не покрытые oracle braced-default/nested-brace defects; окончательная completeness остается открытой в 1.7.
- [~] Автоматически производный authoritative domain заменен явными reviewed registries owners и non-owners; точное покрытие и уникальность являются обязательным gate.
- [~] Source-level CI contract теперь проверяется отдельным validator: точные четыре профиля, push/PR triggers, отсутствие `continue-on-error`, warnings-as-errors и все checks/self-tests/build/self-containment/CTest commands. Self-test отвергает `19/19` malformed workflow fixtures. Последующий adversarial review доказал, что это только lexical source check: semantic validation workflow и реальный GitHub run остаются обязательными в 1.7.
- [~] Warning debt устранен без suppressions: четыре профиля конфигурируются и собираются с `/W4 /WX`, все public headers self-contained, CTest полностью зеленый.

Статус цели 1 после выполнения и повторной проверки 1.7: `HARD_FROZEN`. Критерии 1.1-1.7 воспроизводятся локально и в опубликованном remote baseline; дальнейшие цели обязаны сохранять эти gates зелёными.

## 1.7. Закрыть доказательные обходы и опубликовать воспроизводимый baseline

Независимые adversarial reviews 2026-09-15 и 2026-09-16 подтверждены против текущего дерева. В production graph сейчас не найдено фактической запрещенной транзитивной зависимости, но validators допускают ее появление; несколько evidence attacks воспроизведены буквально. Второй review дополнительно доказал, что текущий API inventory неполон и содержит semantic misclassification. Этот раздел является частью цели 1 и возвращает ей статус `HARD_FROZEN` только после выполнения обязательных подпунктов.

### Обязательные блокеры до `HARD_FROZEN` и массового заполнения ledger

[x] Исправить declaration parser: `{` завершает declaration только как начало function body после сбалансированного parameter/declarator suffix, но не внутри `= {}`, nested braced initializer, `noexcept(...)`, `requires` или template expression. Добавить positive fixtures для каждого случая и negative fixtures для поврежденной/незакрытой declaration.

[x] Перегенерировать API inventory после parser fix и считать прежние `3872/884/207` неавторитетными. Gate обязан доказать отсутствие truncated signatures и включить ранее потерянные Base API (`Application(options = {})`, dispatcher/task scheduling, `Error::Create`, Null/D3D11 factories) и соответствующие Runtime/Framework declarations.

[x] Расширить независимый completeness oracle либо добавить compiler/AST comparison всего public surface: ожидаемый manifest не должен генерироваться тем же lexical parser. Diff между scanner и oracle обязан быть пустым или состоять только из явно reviewed exclusions.

[x] Переработать classification с учетом declaration form и semantics: constructor/destructor, assignment/pure operators, member/static/free function, const member, factory, lifecycle, mutation, query, unknown. `CaptureSnapshot() const`, pure `operator|` и `Path::Join() const` не могут быть mutators; namespace factories должны иметь factory classification. Неоднозначные строки получают explicit reviewed override, а не угадываются prefix-only правилом.

[x] Проверять полное транзитивное замыкание project target dependencies для каждого layer rule, а не только непосредственные `LINK_LIBRARIES`/`INTERFACE_LINK_LIBRARIES`.

[x] Разрешать CMake `ALIAS` до реального target и проверять путь через нейтральный wrapper; добавить negative fixtures как минимум для `Base -> NeutralBridge -> Runtime` и `Base -> NeutralAlias -> Runtime`.

[x] Связать `path:line::symbol` строго: symbol обязан находиться на указанной строке либо в явно заданном проверяемом диапазоне. Self-test должен отвергать существующий symbol на неправильной строке.

[x] Заменить regex-регистрацию CTest evidence на manifest реально настроенного профиля через `ctest --show-only=json-v1`; test внутри `if(FALSE)` или выключенной конфигурации не может считаться зарегистрированным.

[x] Проверять workflow как YAML и валидировать исполнимость обязательных steps, их `if`, matrix scope, shell и failure policy. Fixture с `if: false` на freeze step обязан отклоняться.

[x] Добавить per-item evidence manifests: exact public callable, lifecycle transition, stale-identity surface, external callback/backend boundary и defect regression. Module-level `PASS` для критериев со словом «каждый» должен вычисляться из полного дочернего inventory, а не приниматься по одному anchor.

[x] Сделать `callable_id` независимым от номера строки: ID строится из module, header, fully-qualified scope и normalized signature; line остается навигационным metadata. Добавление комментария не должно инвалидировать API evidence.

[x] Технически запретить `API-CLASSIFIED=PASS` и `LOCAL_READY`, пока в модуле есть `UNCLASSIFIED`. Historical count `207` недостоверен до parser/classifier regeneration; после нее все unknown rows разбираются владельцем соответствующего module audit. Подтвержденные stateful примеры: `TryRestoreSnapshot`, `NotifyScheduleDue`, `EvaluateCrimeCandidate`, `ExchangeEquipmentReservations`.

[x] Зафиксировать profile-specific test manifest и ожидаемые counts/labels (`9/29/89/89` для текущей матрицы), чтобы удаление или отключение теста ломало gate, даже если оставшийся CTest набор зеленый.

[x] Создать отдельный freeze commit, отправить его в remote и получить реальный зеленый GitHub Actions push/PR run. Записать commit SHA, runner image, compiler/toolset, CMake и Python versions; локально проверенный незакоммиченный worktree не является опубликованным freeze.

### Hardening, обязательный до финального whole-engine freeze

[x] Разделить каждое эвристическое поле module dossier на `DISCOVERED` и `REVIEWED`; `LoadDynamicLibrary` не должен автоматически считаться snapshot/restore API только из-за токена `Load`. Module audit обязан исправить ложные находки до `LOCAL_READY`.

[x] Добавить compiler/AST-based public surface manifest для non-callable C++ contracts: public structs/fields, enums и values, aliases, constants, inheritance, templates и constraints. Отдельно зафиксировать обещание source/API compatibility без необоснованного обещания стабильного C++ binary ABI.

[x] Сделать production source discovery fail-closed для новых C++ расширений (`.inl`, `.ipp`, `.cc`, `.cxx`, `.ixx`, `.cppm` и следующих): неизвестное расширение ломает gate до добавления в единый registry. Generated production sources/headers проходят тот же post-generation scan.

[x] Усилить self-containment per-target consumer fixtures: consumer включает public headers и линкует только соответствующий target, используя его реальные `PUBLIC/INTERFACE` include directories, compile definitions, features и transitive requirements.

[x] Зафиксировать cross-engine error/exception policy для expected failures, programmer violations, allocation failure, throwing callbacks, destructors, fallible cleanup и no-op revision/journal semantics. Разрешенные module-specific отклонения должны быть явными.

[x] Расширить configuration qualification либо доказать эквивалентность: Base Release и Runtime Release без Framework, CI/local toolchain parity, test timeouts и pinned versions там, где floating `windows-latest`, Python `3.x` или action major могут разрушить воспроизводимость.

[x] Добавить compiler-diversity public-header profile, как минимум Clang-cl/Clang наряду с MSVC, и регистрировать найденные portability defects в module ledger. Подтвержденный долг Goal 4: defaulted equality для `EnvironmentHazard`, `EnvironmentLayer` и `EnvironmentSampleHazard` удален компилятором, потому что member `GameplayTagSet` не предоставляет equality; требуются regression и осознанное исправление в Framework Environment audit.

Локальная квалификация 1.7, 2026-09-16:

- Новый authoritative inventory содержит `4301` exact callable из `232` public headers и `78` production-модулей; signatures сбалансированы, stable IDs не зависят от строк, `UNCLASSIFIED=0`, а `291` неоднозначная строка закреплена reviewed overrides. Независимый oracle содержит `25` вручную заданных positive/negative случаев, включая braced defaults, nested braces, `noexcept`, `requires`, operators и malformed declaration.
- Architecture self-test принимает valid graph и отвергает `14/14` forbidden transitive, alias, include и source-discovery fixtures. Evidence validators строго проверяют line/range anchors, configured CTest JSON, semantic YAML workflow и полные per-item child inventories.
- Non-callable manifest закрепляет hashes и source/API contracts всех `232` public headers; per-target consumers компилируют каждый header с реальными target requirements. MSVC является локальным compiler gate, ClangCL является отдельным обязательным remote profile. Обещание стабильного C++ binary ABI явно не даётся.
- Локальная strict-матрица `/W4 /WX` и exact CTest manifests зелёные: Base Debug `9/9`, Base Release `9/9`, Runtime Debug `29/29`, Runtime Release `29/29`, Full Debug `89/89`, Full Release `89/89`. Для тяжёлого Debug fault-sweep подтверждён runtime `133.71 s`, поэтому явный общий timeout исправлен с `120` на `300` секунд.
- Portability regression исправлен осознанными equality для `GameplayTagSet` и `GameplayContext`; прямые tests подтверждают доступность defaulted equality для `EnvironmentHazard`, `EnvironmentLayer` и `EnvironmentSampleHazard`.
- Локальный toolchain: Windows, Visual Studio 2026 Developer Command Prompt `18.5.1`, MSVC `19.50.35729.0`, CMake `4.2.3-msvc3`, Python `3.12.14`.

Результат публикации 1.7, 2026-09-16:

- Freeze baseline опубликован последовательностью обычных commit без переписывания истории: основной evidence commit `2e34d2f`, ClangCL portability follow-up `049f5a7`, финальный verified code SHA `2abe6d0d27dcf847dfa2fcb9d080fce743f4fa8a`.
- GitHub Actions run `#9`, id `35148315116`, `https://github.com/ImMedved/Epidemic/actions/runs/35148315116`, завершён `success`: `freeze-contract`, `clang-public-surface` и все шесть Base/Runtime/Full Debug/Release jobs зелёные.
- Remote environment: Microsoft Windows Server 2025, runner image `windows-2025-vs2026`, MSVC `19.51.36256.0`, ClangCL `22.1.3`, CMake `4.4.3`, Python `3.13.7`, `actions/checkout@v7.0.1`, `actions/setup-python@v7.0.0`.
- Runs `#7` и `#8` намеренно сохранены как отрицательное evidence: первый обнаружил ClangCL/MSVC-STL vectorized-algorithm incompatibility, `FARPROC` type mismatch, missing aggregate field и dead-code warnings; второй сузил остаток до одного dead helper. Все найденные причины устранены, MSVC regression targets повторно прошли перед run `#9`.

Критерий выхода 1.7 выполнен: все обязательные блокеры имеют positive и adversarial negative tests, полный strict build/test baseline проходит из чистого checkout, удалённый CI зелёный на записанном commit SHA, статус цели 1 установлен в `HARD_FROZEN`.

# Цель 2. Полный локальный freeze-аудит EngineBase

Все модули проходят общий checklist цели 1. Ниже перечислены дополнительные обязательные проверки именно этого слоя.

## 2.1. Foundation

[ ] `Error` сохраняет стабильный machine-readable code.

[ ] `Result<T>` корректен для value, error, move, empty/invalid misuse согласно контракту.

[ ] Invalid ID имеет одно однозначное представление.

[ ] Разные typed ID нельзя случайно смешать.

[ ] Hash и equality согласованы.

[ ] Handle equality учитывает generation.

[ ] Stale handle не становится валидным после remove/recreate.

[ ] Checked и boundary arithmetic для frame/time value types покрыта.

[ ] `Path` normalization не меняет семантику пути неожиданно и проверена на empty, separators, roots и malformed input.

[ ] Foundation не выполняет I/O, не создаёт threads и не зависит от верхних модулей.

## 2.2. Memory

[ ] Allocate/deallocate contract проверен для normal size и alignment.

[ ] Invalid alignment и invalid size обрабатываются по контракту.

[ ] TrackingAllocator учитывает успешные allocations ровно один раз.

[ ] Failed allocation не увеличивает current usage.

[ ] Deallocation возвращает current usage к правильному значению.

[ ] Peak usage не уменьшается ошибочно.

[ ] Budgets работают на границе, до границы и после превышения.

[ ] Allocation tags не смешивают статистику.

[ ] Frame allocator reset инвалидирует предыдущий lifetime только в разрешённой точке.

[ ] Overflow counters и byte arithmetic не wrap-around.

[ ] OOM path не повреждает tracker state.

## 2.3. Diagnostics

[ ] Disabled logger/profiler не меняет engine behavior.

[ ] Counter increment/decrement и boundary behavior корректны.

[ ] ProfileScope закрывает событие через RAII на normal return и exception path.

[ ] Thread diagnostic names не создают dangling references.

[ ] Sink failure не становится скрытым каналом изменения authoritative engine state.

[ ] Logging из failure path не маскирует исходную ошибку.

[ ] Diagnostics не используется как event bus или authoritative storage.

## 2.4. Core

[ ] Полная state machine `Constructed -> Bootstrapped -> Initialized -> Running -> ShutDown`.

[ ] Нельзя вызвать lifecycle method из неправильного state.

[ ] Duplicate service registration отклоняется.

[ ] ServiceContainer закрывается после initialization и не открывается повторно.

[ ] ModuleRegistry обнаруживает missing dependency.

[ ] ModuleRegistry обнаруживает dependency cycle.

[ ] Module initialization order детерминирован.

[ ] Partial initialization failure корректно освобождает уже созданное.

[ ] Shutdown order обратим зависимостям.

[ ] EventBus проверен для direct и queued dispatch.

[ ] Subscription removal во время dispatch имеет определённую семантику.

[ ] Reentrant event publication не ломает iteration.

[ ] TaskScheduler проверен для submit, complete, cancel/wait contracts и invalid handles.

[ ] MainThreadDispatcher не теряет completion.

[ ] Frame handlers вызываются в фиксированном phase order.

[ ] Stop request завершает Run без лишнего Tick.

## 2.5. Platform

[ ] Window create/destroy lifecycle.

[ ] Native close корректно отражается в wrapper и event stream.

[ ] Resize, minimize, restore и zero client area обрабатываются отдельно.

[ ] Focus gain/loss и input-related platform events не дублируются.

[ ] Event pump не возвращает stale events повторно.

[ ] High-resolution clock монотонен.

[ ] Exit request имеет идемпотентную семантику.

[ ] Dynamic library load failure не оставляет partial handle.

[ ] Symbol lookup на отсутствующий symbol возвращает controlled failure.

[ ] Library lifetime не переживает invalid native handle.

[ ] Windows headers и Win32 types не протекают через public neutral contracts.

## 2.6. Input

[ ] Platform events принимаются до publication snapshot.

[ ] Snapshot одного frame immutable для consumers.

[ ] Key press, hold и release различаются корректно.

[ ] Mouse press, release, motion и wheel transitions не теряются.

[ ] Несколько transitions одного key/button внутри одного frame сохраняют правильную sequence.

[ ] Focus loss/reset очищает held state по контракту.

[ ] `Reset()` не создаёт ложных gameplay events.

[ ] Publish без новых events сохраняет held state и очищает transient transitions по контракту.

[ ] Unknown/invalid key/button не повреждает state.

## 2.7. RHI

[ ] Device/context/swap chain creation failures атомарны.

[ ] `BeginFrame` нельзя вызвать дважды без `EndFrame`.

[ ] `EndFrame` нельзя вызвать без active frame.

[ ] `Clear` разрешён только внутри active frame.

[ ] Present имеет определённое поведение до и после resize.

[ ] Resize с zero dimensions отклоняется или deferred строго по контракту.

[ ] Minimized surface не вызывает invalid resize.

[ ] Null RHI соблюдает те же observable lifecycle contracts.

[ ] Device/swap chain destruction не оставляет active frame state.

## 2.8. RHI_D3D11

[ ] D3D11 factory failure корректно освобождает частично созданные COM objects.

[ ] Device/context lifetime корректен.

[ ] Swap chain creation и destruction корректны.

[ ] Render target пересоздаётся после resize без stale RTV.

[ ] Minimize/restore flow не вызывает invalid DXGI operations.

[ ] Present/device lost/error paths возвращают контролируемый результат.

[ ] Debug layer path проверен отдельно.

[ ] Ни один upper module не требует downcast к D3D11 concrete implementation.

## 2.9. Support

[ ] Каждая registration helper либо полностью регистрирует свой bundle, либо не меняет composition root.

[ ] Duplicate registration имеет controlled failure.

[ ] `RegisterEngineBase`, Windows, Input и Graphics composition tested в допустимых комбинациях.

[ ] Null graphics и D3D11 graphics дают одинаковый service contract.

[ ] Main window и swap chain wiring не создают duplicate owners.

[ ] Frame handler order platform/input/presentation определён тестом.

[ ] Failed later registration не повреждает уже валидные ранее registered services.

[ ] Support не превращается в global service locator.

## 2.10. Exit criteria EngineBase

[ ] 9 из 9 модулей имеют `LOCAL_READY`.

[ ] Все EngineBase unit, integration, regression и smoke applications проходят.

[ ] D3D11 smoke отдельно проходит на Windows 11.

[ ] После этого EngineBase считается локально готовым. Системный freeze выполняется только после целей 5-9.

---

# Цель 3. Полный локальный freeze-аудит EngineRuntime

## 3.1. RuntimeFoundation

[ ] Все Runtime IDs имеют invalid zero state.

[ ] Разные ID spaces не смешиваются.

[ ] Runtime budgets корректны для operation, byte и time boundaries.

[ ] Checked time arithmetic не overflow.

[ ] Saturating arithmetic насыщается в правильную сторону.

[ ] `Vec3`, `Quat`, `Transform`, `Aabb`, `Sphere` валидируют NaN, infinity и impossible geometry.

[ ] Quaternion normalization contract проверен.

[ ] Ограничение TRS без shear явно тестируется на запрещённых hierarchy cases.

## 3.2. Time

[ ] Advance на zero, normal и large delta.

[ ] Pause/Resume state machine.

[ ] TimeScale validation и boundary values.

[ ] Fractional remainder не создаёт cumulative floating drift.

[ ] Skip корректно сбрасывает remainder по контракту.

[ ] Calendar day/month/year boundaries.

[ ] Day phase boundaries.

[ ] Multiple boundaries за один large Advance.

[ ] No-op не создаёт ложную revision.

[ ] Snapshot/restore clock state.

## 3.3. Serialization

[ ] Writer не допускает duplicate object fields.

[ ] Document требует valid type ID, format version и non-null root.

[ ] Reader корректно отвергает wrong type.

[ ] SerializerRegistry запрещает duplicate incompatible registration.

[ ] Freeze registry запрещает дальнейшую registration.

[ ] Migration chain непрерывна.

[ ] Missing migration step даёт controlled failure.

[ ] Migration обязана сохранить type ID.

[ ] Migration обязана вернуть ровно expected target version.

[ ] Failed migration не изменяет исходный immutable document.

[ ] Deep/nested arrays, bytes, null и empty structures roundtrip.

## 3.4. Resources

[ ] Request создаёт отдельный lease ownership.

[ ] Два consumers одного resource не освобождают lease друг друга.

[ ] Dependency graph detects cycles.

[ ] Dependency rollback снимает только ownership текущей операции.

[ ] Loader artifact ID/type совпадают с request.

[ ] Null payload запрещён.

[ ] Retry policy не создаёт duplicate active load.

[ ] Cancellation на queued и loading states.

[ ] Release exactly once.

[ ] Stale ResourceHandle не даёт доступ к новому generation.

[ ] Budget boundary и eviction.

[ ] Failed load не оставляет active dependency leases.

[ ] Shutdown очищает или сохраняет retryable ownership строго по контракту.

## 3.5. Assets

[ ] Asset registration и duplicate ID.

[ ] Metadata/schema validation.

[ ] Dependency references существуют и корректно валидируются.

[ ] Asset version/revision semantics.

[ ] Immutable asset descriptor не меняется через borrowed aliases.

[ ] Missing asset и wrong type queries.

[ ] Registry freeze.

[ ] Snapshot/catalog reload, если присутствует.

## 3.6. Streaming

[ ] Несколько demands одного target разделяют request, но имеют отдельные handles.

[ ] Release последнего demand корректно запускает cancellation/unload.

[ ] Successor во время `Unloading` существует максимум один.

[ ] Waiting successor удаляется при потере последнего demand.

[ ] Budgeted progressive load не превышает policy неконтролируемо.

[ ] Prepare/commit/rollback проверены на каждой failure boundary.

[ ] Failed rollback сохраняет ownership для retry.

[ ] Failed unload сохраняет ownership для retry.

[ ] Resource leases активируются только после commit.

[ ] Persistence override lifetime соответствует loaded lifetime.

[ ] Shutdown запрещает новые demands.

[ ] Successful shutdown не оставляет live demands, requests, leases или resident ownership.

## 3.7. Scene

[ ] Node create/remove.

[ ] Parent/child relationship.

[ ] Reparent valid path.

[ ] Reparent cycle rejection.

[ ] Reparent failure сохраняет старого parent и child list.

[ ] Local/world transform consistency.

[ ] Stale node handle rejection.

[ ] Removal subtree semantics.

[ ] Projection queue ordering.

[ ] Failed projection не оставляет partial transform tree.

[ ] Snapshot/restore hierarchy, если поддерживается.

## 3.8. World

[ ] Region registration и duplicate.

[ ] Chunk registration и state transitions.

[ ] Unknown chunk не маскируется как unloaded.

[ ] Object create/move/residency/persistence transitions.

[ ] Destroyed object terminal.

[ ] Нельзя воскресить destroyed record.

[ ] `PlayerTouched` и выше требуют persistent identity.

[ ] Placement variant validation.

[ ] Revision conflict.

[ ] Materialization только для совместимого placement.

[ ] Demotion token invalidates after object revision.

[ ] Token revoke.

[ ] Stale token не commit-ит demotion.

[ ] Queries detached от internal storage.

## 3.9. Simulation

[ ] Job create/run/continue/complete lifecycle.

[ ] Generation handle stale after removal.

[ ] Scheduled task activation.

[ ] CancelJob снимает associated schedule.

[ ] Failed cancellation retry semantics.

[ ] Proposal publish разрешён только из допустимого job state.

[ ] Proposal queue commit и discard.

[ ] Terminal job удерживается до обработки последнего pending batch.

[ ] World memory TTL zero/negative/boundary.

[ ] Memory capacity hard limit.

[ ] Abstract fact validation.

[ ] Attention finite и в `[0,1]`.

[ ] Shutdown запрещает new work.

[ ] Budget exhaustion приводит к controlled defer, а не partial commit.

## 3.10. Physics

[ ] Body create/remove и stale handles.

[ ] Backend create failure не оставляет registry entry.

[ ] Transform/velocity mutation validation.

[ ] Simulation step order.

[ ] Collision/contact publication не дублируется.

[ ] Scene projection происходит через утверждённый queue, а не прямой чужой state write.

[ ] Failed backend destroy сохраняет retryable ownership.

[ ] Shutdown повторяем после partial cleanup failure.

[ ] ID/generation exhaustion.

## 3.11. Navigation

[ ] Navigation object/layer registration.

[ ] Query invalid start/end.

[ ] Path success, no path, partial/deferred result по контракту.

[ ] Environment projection update не оставляет half-updated navigation state.

[ ] Backend failure controlled.

[ ] Query cancellation/stale request.

[ ] Rebuild/update ordering.

[ ] Shutdown и backend cleanup retry.

## 3.12. Animation

[ ] Skeleton/clip registration.

[ ] Invalid skeleton/clip compatibility.

[ ] Animation instance create/remove и stale handle.

[ ] Play/pause/stop/update state machine.

[ ] Resource lease удерживается только нужный lifetime.

[ ] Evaluator failure не публикует partial pose.

[ ] Pose publication привязана к правильному owner.

[ ] Shutdown retry после backend/evaluator cleanup failure.

## 3.13. Audio

[ ] Sound registry duplicate и invalid descriptor.

[ ] Emitter/listener create/remove и stale handles.

[ ] Play/Pause/Resume/Stop state machine.

[ ] Fade normal, zero duration, completion и pause/resume continuation.

[ ] Virtualize освобождает backend voice ровно один раз.

[ ] One-shot completion.

[ ] Mixer hierarchy и gain propagation.

[ ] Failed voice/listener cleanup сохраняет handle для retry.

[ ] Shutdown terminal и retryable.

[ ] New work rejected after shutdown starts.

[ ] Generation overflow.

## 3.14. Environment

[ ] Region создаётся атомарно с weather, season и climate.

[ ] Per-region revision не создаёт false conflict между регионами.

[ ] No-op setter не увеличивает revision.

[ ] Weather/climate/surface validation.

[ ] Existing SurfaceId нельзя молча перенести в другой region.

[ ] Invalid region/surface queries.

[ ] Snapshot/projection consistency.

## 3.15. Renderer

[ ] Renderer initialization и backend command sink validation.

[ ] Render object/proxy create/update/remove.

[ ] Resource binding lifetime.

[ ] Scene/animation projection ingestion.

[ ] Frame prepare/submit order.

[ ] Failed backend submission не corrupt-ит internal lifecycle.

[ ] Resize/recreate path.

[ ] Shutdown retry после partial cleanup failure.

[ ] После terminal shutdown новая render work отклоняется.

## 3.16. Persistence

[ ] Store/read/delete operations.

[ ] Transaction/staging semantics.

[ ] Partial write failure не становится visible committed state.

[ ] Version mismatch.

[ ] Corrupted payload.

[ ] Atomic replacement.

[ ] Detached read data не содержит borrowed references.

[ ] Cleanup и retry behavior.

[ ] Empty store и missing record behavior.

## 3.17. Support

[ ] `PrepareEngineRuntime` не меняет Application.

[ ] `CommitPreparedRuntime` регистрирует aggregate атомарно.

[ ] Failed preparation не оставляет registered partial services.

[ ] Reference profile wiring.

[ ] Production profile требует обязательные external backends.

[ ] Partial override запрещён там, где contract требует полный набор roles.

[ ] Один adapter instance используется для всех его roles, без hidden duplicates.

[ ] Coordinator frame order зафиксирован тестом.

[ ] Scene/Renderer, Resources/Renderer, Scene/Physics, Scene/Audio, Resources/Animation, Resources/Audio, Animation/Renderer, Streaming, Environment/Navigation и Time/Simulation adapters проверены отдельно.

[ ] Retry cleanup semantics adapters.

[ ] Runtime shutdown order.

## 3.18. Exit criteria EngineRuntime

[ ] 17 из 17 модулей имеют `LOCAL_READY`.

[ ] Каждый major проходит собственный isolated suite.

[ ] Runtime architecture tests проходят.

[ ] Runtime integration suite проходит.

[ ] Runtime regression suite проходит.

[ ] Runtime Support reference composition проходит end-to-end smoke.

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
