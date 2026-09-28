# Milestone 4: локальное закрытие EngineFramework тремя независимыми delta-блоками

Дата независимой перепроверки: 2026-09-28.

Исходное дерево для выполнения: `Epidemic Engine 27-09-26-3` плюс возвращённая папка `docs/`, плюс действующие `Milestone 4.md` и `work-plan.md`.

Цель этой итерации: не финализировать весь движок, а довести Goal 4 до содержательно доказанного локального состояния без module-local code/evidence debt, чтобы можно было переходить к Goals 5+. Remote Architecture Freeze CI в эти три блока не входит и выполняется отдельно после локального convergence.

## 0. Результат независимой перепроверки перед новой итерацией

Возврат `docs/` полностью снимает найденный ранее archive-integrity blocker. В объединённом дереве присутствуют 53 Markdown файла `docs/EngineFramework`, включая индекс и документы всех 52 модулей, а также 42 файла `docs/freeze`. `foundation_tests.cpp::TestFrameworkDocumentationRoot()` проходит без каких-либо заглушек или изменений production-кода.

Serial merge теперь воспроизводится. Повторный запуск `_goal4_handoff/merge_goal4_evidence.py` логически идемпотентен и сообщает: 52 модуля, 3054 Framework API, 39 физических Goal 4 defect records в handoff, 0/52 `LOCAL_READY`, 52/52 `BLOCKED`, 124 rebased stale line anchors.

Следующие validators проходят на объединённом дереве:

* `architecture_ownership.py --check` и `--self-test`;
* `ci_gate_contract.py --check` и `--self-test`;
* `coverage_manifests.py --check` и `--self-test`;
* `ctest_manifest.py --check`;
* `local_ready_contract.py --check` и `--self-test`;
* `public_api_inventory.py --check` и `--self-test`;
* `public_surface_manifest.py --check` и `--self-test`.

Canonical inventory после merge содержит 4311 whole-engine public callables, из них 3054 Framework, 857 Runtime и 400 Base. Framework содержит 3348 reviewed mutation obligations, 73 reviewed lifecycle candidates, 770 reviewed stale-identity candidates и 17 reviewed external-boundary candidates. `UNCLASSIFIED = 0`.

Единственный ожидаемо красный локальный evidence gate: `python docs/freeze/goal4_evidence_quality.py --check`.

Текущий exact output этого gate:

```text
defect_without_line_anchor: 19
framework_not_local_ready: 52
generic_api_contract: 621
generic_api_test: 1729
generic_criterion_test: 712
generic_na_rationale: 70
modules_with_generic_criteria: 26
```

Это подтверждает, что проблема после возврата `docs/` находится в качестве исходных B01-B08 handoff, а не в отсутствии canonical infrastructure.

Дополнительный аудит raw `local_ready*.json` выявил более широкий слой долга, чем показывает итоговый quality gate. У Bxx различается сама форма evidence. В части файлов evidence хранится как typed objects, в части как массив строк, в B08 как одна строка с путём. Поэтому canonical merge вынужден подставлять fallback evidence и затем правильно оставляет модуль `BLOCKED`.

Суммарно в raw B01-B08 найдено 1706 criterion source gaps, где worker projection не содержит полного набора typed evidence требуемых видов с точными anchors. Эти source gaps необходимо исправлять в исходных handoff, а не вручную в `docs/freeze/local_ready_ledger.json`.

Кодовая регрессия в проверенном Framework slice не обнаружена. Дополнительный portable Debug probe на GCC 14.2 с C++23 и warnings-as-errors собрал и выполнил все 55 Framework module/integration targets: 55/55 PASS. Release probe на Clang 17 с C++23 и warnings-as-errors также дал 55/55 PASS. Три Framework Truth targets под Clang Release дали 3/3 PASS.

Неофициальные portability observations, которые не блокируют Goal 4:

* GCC 14.2 Release `-O3 -Werror` выдаёт `-Wfree-nonheap-object` из libstdc++ optimizer path при компиляции `PerceptionKnowledgeAIIntegration::ObservationPayload`; Clang Release тот же код собирает и тест проходит. Считать semantic defect без дополнительного воспроизведения нельзя.
* `Tests/Truth/truth_test.h` не имеет завершающего newline, из-за чего GCC Debug с `-Werror` выдаёт `backslash-newline at end of file`. Clang принимает файл. Это безопасный portability cleanup, но не официальный MSVC/ClangCL blocker Goal 4.

## 0.1. Defect traceability, которую нужно нормализовать

Действующий Milestone 4 перечисляет 23 исходных confirmed problem IDs. Handoff после выполнения B01-B08 содержит 39 физических Goal 4 defect records. Восемь из них являются block-local представлениями одного распределённого `G4-INFRA-001`, поэтому это 32 логических Goal 4 findings.

Следовательно, помимо исходных 23 problem IDs, во время Bxx аудита появилось 9 дополнительных tracked findings:

* `G4-B02-COND-001`;
* `G4-B02-EFF-001`;
* `G4-B02-EFF-002`;
* `G4-B02-EFF-003`;
* `G4-B02-INT-001`;
* `G4-B02-OWN-001`;
* `G4-B06-PROG-004`;
* `B08-INT-001`;
* `B08-PRSINT-001`.

Восемь из этих девяти являются semantic/failure-atomicity findings. `G4-B02-OWN-001` является portability/compiler-warning cleanup без изменения поведения.

Эти findings уже имеют fixes и regression coverage в коде, но их нужно официально синхронизировать с defect traceability. После новой итерации canonical metadata должна однозначно различать:

* 32 логических Goal 4 findings;
* 39 физических handoff defect records, если block-local split `G4-INFRA-001` сохраняется;
* исторический `environment-defaulted-equality`, который остаётся отдельным ранее закрытым Framework defect и не должен ошибочно считаться новым Goal 4 finding.

Необходимо убрать одностороннюю проверку вида «всё из Milestone присутствует в registry». Gate должен также обнаруживать новые Goal 4 defects, которые присутствуют в handoff/canonical registry, но не зарегистрированы в действующей модели Goal 4.

# 1. Общие правила для трёх параллельных агентов

Все три агента стартуют от одного и того же полного baseline. Они не обмениваются промежуточными изменениями и возвращают отдельные delta ZIP.

Production ownership сохраняется по исходным B01-B08. Агент может изменять production code только если при содержательном evidence review он сначала воспроизвёл реальную ошибку контракта отдельным regression test. Запрещено менять production code только ради того, чтобы сделать evidence удобнее.

Каждый агент обязан исправлять именно raw handoff своего scope:

* `public_api_anchors.json`;
* `coverage_reviews.json`;
* `dossier_reviews.json`, только если фактический review требует correction;
* `local_ready*.json`;
* `defects.json`;
* `manifest.md`;
* `cross_block_findings.md`;
* owned module docs;
* owned module tests;
* owned production files только при новом воспроизведённом defect.

Запрещено вручную редактировать generated canonical outputs `coverage_manifests.json`, `public_api_anchors.json`, `module_dossier_reviews.json`, `local_ready_ledger.json`. Они создаются serial integrator после объединения трёх delta.

Нельзя использовать `main()` как доказательство конкретного API или критерия. Нельзя ссылаться только на общий заголовок `Responsibility`, `Public API`, `Public mutation API` или `Test evidence`, если этот раздел не содержит точного проверяемого contract для данного callable. Нельзя ставить generic assertion `if (...)`, `Check(...)` или `return 0` без привязки к конкретному проверяемому утверждению.

Каждый PASS criterion должен содержать typed evidence objects всех требуемых kinds из `REQUIRED_KINDS`. Каждый test evidence обязан иметь точный `path:line::symbol`, зарегистрированный CTest target и конкретные assertion anchors. Каждый `N/A` обязан иметь module-specific rationale, который объясняет, почему соответствующая поверхность действительно отсутствует.

Если существующий тест реально не доказывает contract, агент обязан добавить или расширить owned regression. Нельзя просто выбрать ближайшую строку теста, которая синтаксически существует.

Каждый defect record должен иметь существующий registered target и точный regression anchor. Для defect, который проверяется несколькими executables, должны быть перечислены все targets и конкретные regression anchors для соответствующих проверок. Проза вместо regression anchor запрещена.

Delta ZIP каждого агента содержит только изменённые файлы с project-relative paths. Build directories, `.git`, generated canonical freeze JSON и файлы другого блока запрещены.

# 2. Параллельный блок A: B01 + B02 + B03

## 2.1. Ownership

Этот агент единолично владеет evidence и разрешёнными изменениями B01, B02 и B03.

Модули: 19.

B01: Foundation, SupportRandom, Queries, Facts, Time, RuntimeBridge.

B02: Entities, Materials, Environment, Conditions, Effects, Interaction, Ownership.

B03: ItemsInventory, Equipment, Economy, Processes, ResourcesProduction, Loot.

Никакие B04-B08 файлы агент не меняет.

## 2.2. Текущая очередь блока A

Scope: 1221 public API.

Raw evidence audit:

* 769 generic API test anchors;
* 93 generic contract anchors;
* 543 criterion source gaps в raw LOCAL_READY projections;
* 0 пустых `N/A` rationales;
* 14 block defect records, которые нужно привести к closure-grade traceability без reliance на merge heuristics.

Разбивка generic API test anchors:

| Bxx | API | Generic test | Generic contract |
|---|---:|---:|---:|
| B01 | 326 | 326 | 0 |
| B02 | 403 | 196 | 93 |
| B03 | 492 | 247 | 0 |

## 2.3. Обязательная работа

1. Перепроверить все 1221 API records. Для каждого callable оставить точный contract anchor и тестовый anchor, который реально проверяет соответствующий контракт. Один тест может доказывать несколько API только когда assertion действительно относится к каждому из них.

2. B01: заменить 326/326 API anchors, которые сейчас опираются на generic test entry points. Особое внимание Foundation value/ID semantics, Queries provider/freeze/budget paths, Facts transaction/publication, Time scheduling/catch-up, RuntimeBridge conversion/retry/reconciliation.

3. B02: заменить 196 generic test anchors и 93 generic contract anchors. Не потерять дополнительные B02 findings, найденные во время аудита: `G4-B02-COND-001`, `G4-B02-EFF-001/002/003`, `G4-B02-INT-001`, `G4-B02-OWN-001`.

4. B03: заменить 247 generic test anchors. Проверять именно transactional boundaries Processes и ResourcesProduction, а не только happy-path state queries.

5. Полностью переписать raw LOCAL_READY projections B01-B03 в canonical typed evidence form. После блока не должно оставаться ни одного source gap по required evidence kinds.

6. Для всех 14 defect records дать точные regression anchors. Distributed infra evidence должен перечислять реальные targets и конкретные проверочные места, а не текст вида «all owned tests».

7. Проверить, что `G4-DOC-001` после возвращения `docs/` закрывается настоящим `TestFrameworkDocumentationRoot`, и не оставлять никаких artificial placeholder docs/tests.

8. Сохранить Framework public callable set. Любое новое изменение public header требует отдельного rationale и regression, иначе production header не менять.

## 2.4. Tests блока A

Минимум выполнить все owned module/integration targets Debug и Release с warnings-as-errors. Если official Windows/MSVC доступен, использовать его как основной локальный результат. Portable GCC/Clang является дополнительным сигналом.

Все изменённые tests должны реально выполнять новые anchors. Любой новый defect получает regression сначала, fix после воспроизведения.

## 2.5. Exit блока A

* B01-B03 API records: generic test = 0, generic contract = 0.
* Raw criterion source gaps = 0.
* Пустые `N/A` = 0.
* Все 14 block defect records имеют exact target и exact regression anchors.
* Owned tests Debug/Release green.
* Public API delta отсутствует либо отдельно документирован.
* `cross_block_findings.md` содержит только findings чужого ownership, без попытки их исправить.

Рекомендуемое имя архива: `Goal4_Closure_A_B01_B03_delta.zip`.

# 3. Параллельный блок B: B04 + B05 + B06

## 3.1. Ownership

Этот агент единолично владеет evidence и разрешёнными изменениями B04, B05 и B06.

Модули: 16.

B04: RolesJobs, NeedsLife, Population, Encounters, Society, Crime.

B05: Perception, Knowledge, NavigationSemantics, AI, Simulation.

B06: Combat, Abilities, Progression, Construction, Traversal.

Никакие B01-B03 или B07-B08 файлы агент не меняет.

## 3.2. Текущая очередь блока B

Scope: 1123 public API.

Raw evidence audit:

* 606 generic API test anchors;
* 123 generic contract anchors;
* 592 criterion source gaps;
* 30 пустых `N/A` rationales;
* 17 block defect records.

Разбивка:

| Bxx | API | Generic test | Generic contract | Raw criterion gaps | Empty N/A |
|---|---:|---:|---:|---:|---:|
| B04 | 445 | 0 | 0 | 222 | 30 |
| B05 | 316 | 244 | 0 | 185 | 0 |
| B06 | 362 | 362 | 123 | 185 | 0 |

## 3.3. Обязательная работа

1. B04: API anchors в основном уже предметные. Основная проблема находится в LOCAL_READY projection. Исправить typed evidence для всех criteria шести модулей и заменить все 30 пустых `N/A` на module-specific rationale. Нельзя механически написать один и тот же rationale для разных модулей.

2. B05: заменить 244 generic API test anchors. Perception уже содержит часть хороших точных anchors, их не ухудшать. Knowledge, NavigationSemantics, AI и Simulation требуют систематического per-callable review. Проверить, что exact tests для `G4-PER-001/002/003` и `G4-SIM-001/002` действительно покрывают заявленный boundary, а не только соседний happy path.

3. B06: заменить все 362 generic test anchors и 123 generic contract anchors. Это самый плотный callable-evidence участок блока. Особое внимание Combat exact integer scaling, Progression revision/generator/journal transactionality, Traversal revision and journal semantics.

4. Официально включить `G4-B06-PROG-004` в tracked Goal 4 findings как post-admission finding. Его regression должен иметь точный line anchor, target и проверять именно accepted mutation plus journal allocation failure semantics.

5. Нормализовать raw LOCAL_READY B04-B06. После работы все 37 criteria каждого из 16 модулей должны иметь корректный typed PASS/N/A proof без merge-generated fallback.

6. Defect traceability: заменить prose regressions для distributed `G4-INFRA-001` records и Simulation records на точные anchors. Все остальные defect regressions также перепроверить на фактический вызываемый test case.

7. Не менять production code, если existing tests и review подтверждают корректность. Если найден новый boundary defect, сначала создать failing regression, затем минимальный ownership-local fix, затем добавить новый defect ID в block handoff.

## 3.4. Tests блока B

Прогнать все owned B04-B06 targets Debug/Release с warnings-as-errors. Обязательно отдельно выполнить regression paths Society, Perception, Simulation, Combat, Progression и Traversal.

## 3.5. Exit блока B

* Generic API test = 0.
* Generic API contract = 0.
* Raw criterion source gaps = 0.
* Empty/generic `N/A` = 0.
* Все 17 defect records имеют exact regression traceability.
* `G4-B06-PROG-004` официально присутствует в handoff как post-admission Goal 4 finding.
* Owned tests Debug/Release green.
* Нет нового public surface без отдельного rationale.

Рекомендуемое имя архива: `Goal4_Closure_B_B04_B06_delta.zip`.

# 4. Параллельный блок C: B07 + B08 + hardening evidence validators

## 4.1. Ownership

Этот агент единолично владеет evidence и разрешёнными изменениями B07 и B08.

Модули: 17.

B07: World, SaveGame, Narrative, Dialogue, NarrativeIntegration, WorldIntegration.

B08: Integration, StateIntegration, InteractionTimeIntegration, InteractionEffectsIntegration, GameplayIntegration, ExtendedGameplayIntegration, PerceptionKnowledgeAIIntegration, PopulationSimulationIntegration, ProcessResourceSimulationIntegration, SocialLegalIntegration, TraversalNavigationConstructionIntegration.

Дополнительное exclusive ownership этого блока:

* `docs/freeze/goal4_evidence_quality.py`;
* `_goal4_handoff/merge_goal4_evidence.py` только если изменение необходимо для нового строгого defect/evidence contract;
* validator-specific tests/fixtures, если они создаются;
* `Tests/Truth/truth_test.h` разрешено изменить только для добавления отсутствующего финального newline, без иных semantic изменений.

Агенты A и B эти shared files не меняют.

## 4.2. Текущая очередь блока C

Scope: 710 public API.

Raw evidence audit:

* 354 generic API test anchors;
* 405 generic contract anchors;
* 571 criterion source gaps;
* 40 пустых `N/A` rationales;
* 8 block defect records.

Разбивка:

| Bxx | API | Generic test | Generic contract | Raw criterion gaps | Empty N/A |
|---|---:|---:|---:|---:|---:|
| B07 | 405 | 354 | 405 | 164 | 0 |
| B08 | 305 | 0 | 0 | 407 | 40 |

## 4.3. Обязательная работа B07

1. B07 имеет самый большой contract-anchor debt. Заменить 405 generic contract anchors на точные module contract anchors.

2. Заменить 354 generic test anchors. World и WorldIntegration уже имеют часть более точных test anchors, их сохранить. SaveGame должен доказываться в пределах Goal 4 как in-memory orchestrator, без переноса storage/file-I/O требований из Goal 5.

3. Исправить 164 raw criterion source gaps. B07 evidence должен быть пригоден к merge без fallback из первого API модуля.

4. Дать exact regressions для B07 distributed infra и NarrativeIntegration defect paths.

## 4.4. Обязательная работа B08

1. B08 per-API anchors по текущему heuristic уже не generic. Их всё равно выборочно семантически перепроверить, особенно external-boundary adapters.

2. Полностью заменить `local_ready_projection.json`, где evidence сейчас часто представлен одной строкой `docs/EngineFramework/<Module>.md`. Все 407 source gaps должны исчезнуть.

3. Заменить 40 пустых `N/A` на module-specific rationale.

4. Сохранить и официально зарегистрировать дополнительные findings `B08-INT-001` и `B08-PRSINT-001`. У обоих уже есть точные regression anchors. Перепроверить их после любых test edits.

5. `G4-EXTINT-001/002` и B08 infra regression descriptions перевести в exact line/symbol anchors, без текстовых «blocks» вместо regression location.

## 4.5. Hardening quality gate

Текущий `goal4_evidence_quality.py` правильно ловит placeholders, но имеет несколько blind spots. Исправить их до serial convergence.

Обязательные проверки:

1. Bidirectional defect traceability. Сейчас validator проверяет, что каждый `G4-*` из Milestone присутствует в registry, но не проверяет обратное. Новый gate обязан обнаруживать post-admission Goal 4 findings, которые есть в handoff/canonical registry, но не включены в официальную tracked model.

2. Разделить logical defect ID и block-local regression records для `G4-INFRA-001`. Допустимы два решения:
   * один logical parent `G4-INFRA-001` плюс explicit child/regression records B01-B08;
   * восемь physical records с обязательным `logical_id: G4-INFRA-001`.
   Нельзя оставлять смесь случайных key styles без формальной связи.

3. Multi-target defects должны иметь конкретные regression anchors для фактически заявленных targets. Один prose `regression` рядом со списком `targets` недостаточен.

4. Gate должен проверять raw B01-B08 LOCAL_READY evidence shape до canonical merge: evidence является массивом typed objects, kinds соответствуют criterion, anchors имеют точную форму, test records содержат target и assertions, `N/A` rationale непустой и module-specific.

5. Gate должен отвергать evidence строкой вместо object/list и отвергать пустой `assertions` для test evidence.

6. Добавить negative/self-test fixtures как минимум для: `main()` placeholder, generic section contract, string evidence, empty assertions, empty/generic N/A, untracked extra Goal 4 defect, multi-target defect без per-target regression anchors.

7. Merge не должен автоматически превращать неполный raw evidence в достаточный PASS proof. Он может нормализовать и rebase валидные anchors. При недостатке доказательства он обязан сохранить `BLOCKED`.

## 4.6. Portability cleanup

Добавить финальный newline в `Tests/Truth/truth_test.h`. Это устраняет реальный GCC warnings-as-errors build break и не меняет semantics.

GCC 14.2 Release warning `-Wfree-nonheap-object` в `PerceptionKnowledgeAIIntegration::ObservationPayload` пока не считать подтверждённым production defect. Clang Release собирает этот код и тест проходит. Production refactor разрешён только если агент воспроизведёт проблему sanitizer/runtime test или покажет toolchain-independent нарушение. Если это только optimizer false positive, зафиксировать observation в manifest и не менять корректный код ради GCC.

## 4.7. Exit блока C

* B07 generic API test = 0, generic API contract = 0.
* B07+B08 raw criterion source gaps = 0.
* B08 empty/generic N/A = 0.
* Все 8 block defect records имеют exact traceability.
* Новые B08 findings официально tracked.
* Quality gate имеет bidirectional defect checks и raw-handoff negative tests.
* `truth_test.h` завершается newline.
* Owned tests Debug/Release green.

Рекомендуемое имя архива: `Goal4_Closure_C_B07_B08_Validators_delta.zip`.

# 5. Serial convergence после получения трёх delta ZIP

Этот этап выполняет один integrator. Его нельзя параллелить с тремя worker blocks.

1. Применить A, B, C. Любой конфликт файлов между A/B/C считается ошибкой ownership. Допустимые shared files принадлежат только C.

2. Проверить manifests и `cross_block_findings.md`. Если worker нашёл defect чужого ownership, не исправлять его молча во время merge. Вернуть owner либо сделать отдельный serial regression+fix с явной записью.

3. Запустить `_goal4_handoff/merge_goal4_evidence.py` ровно на объединённом handoff.

4. Перегенерировать canonical outputs только штатными generators/merge. Не переносить вручную значения из worker JSON в canonical JSON.

5. Запустить все freeze validators и self-tests.

6. Запустить `python docs/freeze/goal4_evidence_quality.py --check`. Ожидаемый результат: exit code 0, никаких remaining quality counters.

7. Проверить canonical ledger:

```text
EngineBase      9/9  LOCAL_READY
EngineRuntime  17/17 LOCAL_READY
EngineFramework 52/52 LOCAL_READY
```

8. Каждый Framework module должен иметь 37/37 `PASS/N/A`, 15/15 dossier fields `REVIEWED`, unresolved API/obligation/lifecycle/stale/external = 0.

9. Defect registry должен быть внутренне согласован. Ожидаемая модель: 32 logical Goal 4 findings, если все девять post-admission findings сохраняются как отдельные tracked findings. Физических records может быть 39 из-за block-local split `G4-INFRA-001`, но связь с logical IDs должна быть машиночитаемой. Исторический `environment-defaulted-equality` не входит в 32 Goal 4 findings.

10. Все defect targets должны реально существовать в configured CTest manifest. Все regression anchors должны разрешаться в текущем tree после merge.

11. Выполнить Full local qualification на официальном Windows/MSVC baseline: Base, Runtime, Full Debug/Release, `/W4 /WX`, exact CTest manifests, public-header self-containment, architecture/freeze validators, disabled/skipped audit. Remote CI в рамках этой итерации можно оставить отдельным последующим gate, как и было решено.

12. Только после пункта 11 обновить `Milestone 4.md` и `work-plan.md` фактическими результатами. Не отмечать checklist массово до прохождения evidence quality gate.

# 6. Критерий окончания этой итерации

Локальная итерация считается завершённой, когда одновременно выполнено всё ниже:

* Framework 52/52 `LOCAL_READY`;
* `goal4_evidence_quality.py --check` green;
* raw B01-B08 handoff не содержит fallback/generic evidence;
* 3054 Framework API имеют содержательные exact contract/test anchors;
* 3348 obligations, 73 lifecycle, 770 stale-identity и 17 external-boundary decisions остаются reviewed и имеют достаточное supporting evidence;
* все 32 logical Goal 4 findings официально учтены либо документированно переклассифицированы, каждый semantic defect имеет permanent exact regression;
* все локальные Debug/Release tests и validators green;
* public surface delta отсутствует либо отдельно объяснён;
* никакой module-local code/evidence debt не переносится в Goal 5.

После этого разрешён переход к Goal 5 с локальным статусом наподобие `LOCAL_COMPLETE_PENDING_REMOTE_CI`. Финальный `Goal 4 = COMPLETE` выставляется после предусмотренного действующим планом remote Architecture Freeze CI, когда он будет выполнен отдельно.
