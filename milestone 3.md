# Epidemic Engine: план Goal 3 — локальный freeze EngineRuntime

Дата актуализации baseline: 2026-09-19.

Основа: архив `Epidemic 19-09-2026-2`, общий `work-plan.md` и уже закрытый Goal 2.

Статус входа: `READY_TO_START`. EngineBase = `9/9 LOCAL_READY`. Admission baseline после Goal 2: Base Debug/Release `14/14`, Runtime Debug/Release `34/34`, Full Debug/Release `94/94`, суммарно `284/284` CTest по отчёту последней qualification. Freeze validators, exact manifests и negative/self-tests согласованы с текущим деревом.

Цель документа: последовательно довести все 17 production-модулей `EngineRuntime` до `LOCAL_READY`, исправляя только подтверждённые локальные defects и нарушения уже существующих contracts. Whole-engine persistence/determinism, sanitizer/race qualification, semantic cluster validation и load/final freeze остаются в Goals 5–9.

---

# 3.0. Правила выполнения Goal 3

## 3.0.1. Как читать этот план

Используются четыре типа пунктов:

- `DEFECT`: подтверждённая проблема текущего baseline. Требуется regression, исправление и evidence.
- `AUDIT`: обязательная проверка существующего contract. Изменять production code только если проверка выявила нарушение.
- `SCOPE`: уже принятое решение о границах ownership. Это не задача на добавление API.
- `EXIT`: критерий, после которого module можно перевести в `LOCAL_READY`.

Нельзя превращать audit checklist в feature work. Новый public API добавляется только когда без него невозможно выполнить уже существующий Runtime contract либо будущий обязательный system contract, который иначе потребует ломать локально frozen API.

## 3.0.2. Принятые SCOPE-решения

- Resources не получает искусственный `Shutdown()`. Cancellation определяется ownership `ResourceLease` и существующими processing boundaries.
- Navigation не получает искусственный `Shutdown()`. Backend/provider lifetime принадлежит переданным dependencies.
- Animation не получает искусственный `Shutdown()`. Lifetime evaluator/resource source не принадлежит Animation.
- Renderer не получает resize/recreate API. Presentation resize остаётся в Base RHI/integration layer.
- Navigation проверяется по реальным contracts: `NavTileId`, `RegisterTile`, dirty/rebuild lifecycle и handle-based path queries.
- `ISceneProjectionQueue` проверяется в Runtime Support. Physics локально знает только `IPhysicsTransformSink`.
- Реальный `ResourceLease` lifetime для Animation и Audio проверяется в Support adapters. Локальные majors проверяют только свои neutral resource ports.
- Resources byte budget остаётся soft для уже начатой минимальной единицы работы. Не вводить новый hard-current-payload contract.
- Persistence backend не получает выдуманные `Save()`/`Flush()` methods. Публичный backend contract состоит из `Load()` и `CommitSnapshot(snapshot, durability)`; `PersistenceDurability` передаёт требуемый уровень durability одному atomic backend commit.
- Общий `IsValidSphere()` не добавляется только ради symmetry. Сейчас `Sphere` не имеет нескольких независимых authoritative consumers, требующих общего validator. `Scene::QuerySphere` продолжает валидировать `center/radius` на своей public boundary.
- Наличие test executable само по себе не является evidence `LOCAL_READY`. Каждый Runtime module закрывается через Goal 1 ledger и конкретные anchors.

## 3.0.3. Подтверждённые дефекты текущего baseline

Ниже перечислены только проблемы, подтверждённые чтением production code и текущих public contracts. Они являются обязательным объёмом Goal 3. Остальные пункты module sections являются audit gates: код меняется только если проверка действительно обнаружит нарушение.

### G3-DOC-001 — Runtime docs преждевременно называют модули frozen

Проблема: часть `EngineRuntime/*/docs` описывает Runtime как уже frozen до прохождения Goal 3.

Что не так: документация создаёт ложное freeze evidence и противоречит `LOCAL_READY` ledger, где Runtime ещё должен пройти собственный audit.

Причина: тексты были подготовлены как целевой контракт раньше фактического admission gate.

Предлагаемый фикс: до закрытия конкретного Runtime-модуля использовать формулировки `target contract`, `freeze candidate`, `planned frozen contract`. Слово `frozen` в нормативном смысле допускается только после перевода соответствующей записи ledger в `LOCAL_READY` и прохождения validators.

Regression/evidence: module dossier и docs scan не должны утверждать frozen status для Runtime module раньше его ledger state.

### G3-RF-001 — документация RuntimeFoundation противоречит фактическому TRS contract

Проблема: документация описывает TRS compose/decompose как более строгую операцию, чем реально гарантирует public API и production implementation.

Что не так: текущий код намеренно использует deterministic approximate TRS decomposition для matrix, содержащих scale/rotation interactions, а документация может читаться как обещание полного сохранения shear/non-TRS transform.

Причина: documentation contract не был синхронизирован с уже выбранной математической моделью RuntimeFoundation.

Предлагаемый фикс: зафиксировать текущий deterministic approximate TRS contract в public docs и tests. Не добавлять matrix/shear transform hierarchy, новые типы или альтернативную систему transform composition ради Goal 3.

Regression/evidence: tests на deterministic compose/decompose, negative/degenerate scale и документированный tolerance; public docs используют ту же семантику.

### G3-TIME-001 — скрытый remainder не входит в persistent deterministic state

Проблема: `TimeSystem` использует `tick_remainder_numerator_`, который влияет на будущие fixed ticks, но `TimeSnapshot` его не содержит и restore contract отсутствует.

Что не так: два состояния с одинаковым публичным snapshot могут иметь разное дальнейшее deterministic поведение после persistence/restore.

Причина: snapshot проектировался как frame observation, а не как полный checkpoint deterministic clock.

Предлагаемый фикс: не расширять frame-facing `TimeSnapshot`. Добавить минимальный persistent/checkpoint state, включающий current time/tick state, `time_scale`, paused state, `tick_remainder_numerator_`, revision и immutable configuration identity, необходимую для проверки совместимости. Restore сначала полностью валидирует candidate state и compatibility, затем выполняет один no-fail commit. Invalid restore не меняет live clock.

Regression/evidence: save/restore при ненулевом remainder даёт тот же дальнейший tick sequence, что непрерывное выполнение; invalid checkpoint сохраняет точный pre-state.

### G3-RES-001 — замена dependency set ресурса не атомарна

Проблема: `ResourceManager::FinishLoadedArtifact()` сначала удаляет старые dependencies, затем отдельно вызывает `ResourceDependencyGraph::SetDependencies()` для нового набора.

Что не так: если после `RemoveDependencies(slot.id)` новый `SetDependencies()` получает `std::bad_alloc`, старый dependency graph уже потерян, хотя новая версия ресурса ещё не опубликована.

Причина: replace реализован как две independent mutations authoritative/derived graph state.

Предлагаемый фикс: заменить пару `RemoveDependencies + SetDependencies` на private atomic `ReplaceDependencies`. Полностью построить candidate dependency set/node до изменения live map, затем опубликовать replacement одной no-fail swap/move operation. Пустой новый набор также должен удалять старый node только в финальном commit.

Regression/evidence: существующий ресурс имеет старый dependency set; fault во время подготовки нового set оставляет старый graph побайтно/семантически неизменным и load attempt остаётся retryable.

### G3-RES-002 — WaitingForDependencies job может потеряться при requeue failure

Проблема: `ProcessPendingLoads()` уже извлекает load job из очереди, после чего `FinishLoadedArtifact()` переводит slot в `WaitingForDependencies`, сохраняет `pending_artifact` и повторно вызывает `load_queue_.Enqueue(...)`.

Что не так: если requeue получает `std::bad_alloc`, slot остаётся `WaitingForDependencies` с pending artifact, но больше не имеет queue entry. Ресурс может навсегда перестать обрабатываться.

Причина: dequeue текущей ownership записи происходит раньше, чем гарантирована публикация следующего schedulable state.

Предлагаемый фикс: waiting state должен сам быть durable owner retry. После сохранения `pending_artifact` ресурс добавляется в отдельный waiting/retry ownership set, публикация которого подготовлена до удаления текущего queue ownership. Повторная обработка waiting resources должна исходить из этого durable set, а не зависеть от успешного второго `Enqueue` того же job. В каждый момент должен существовать ровно один authoritative retry owner.

Regression/evidence: fault на requeue не теряет resource; после готовности dependency retry завершает load ровно один раз, без duplicate jobs и без вечного `WaitingForDependencies`.

### G3-RES-003 — `LoadSlot()` смешивает loader callback и Runtime-local finalization в одном exception boundary

Проблема: `ResourceManager::LoadSlot()` держит `loader->Load(request)` и последующий `FinishLoadedArtifact(...)` внутри одного `try/catch`.

Что не так: после successful external load локальный `std::bad_alloc` из dependency staging, graph preparation или другого Runtime-owned finalization кода попадает в `catch` как `resource.loader_exception`. Slot переводится в `Failed`, выполняется rollback, а уже успешно полученный artifact теряется. Retry затем повторно вызывает loader для того же logical load.

Причина: containment boundary внешнего callback шире самого callback и захватывает внутреннюю fallible publication.

Предлагаемый фикс: сузить `try/catch` до одного `loader->Load()`. После successful load немедленно передать artifact no-fail move в durable `slot.pending_artifact` и дальше выполнять Runtime-local finalization вне loader catch. `pending_artifact` очищается только после окончательного semantic failure либо successful Ready commit. Local allocation failure сохраняет artifact и retry ownership и не меняет его на `resource.loader_exception`. Добавить compile-time/implementation assertion, что move используемого artifact storage не аллоцирует.

Regression/evidence: fake loader считает вызовы и возвращает success; fault внутри dependency/finalization staging происходит после callback. После failure loader call count остаётся `1`, slot/artifact остаются retryable, retry завершает публикацию без второго `Load()` и совпадает с clean execution.

### G3-RES-004 — queue ownership удаляется до гарантии следующего durable owner

Проблема: `ProcessPendingLoads()` делает `load_queue_.Dequeue()` до `LoadSlot()`/`FinishLoadedArtifact()`. После dequeue остаются fallible Runtime-owned operations, включая `loading_resources_.insert(...)` и подготовку pending artifact/dependencies.

Что не так: если такая операция бросает до публикации нового retry owner, queue entry уже потеряна. Slot может остаться `Queued`/`Loading`/`WaitingForDependencies`, но больше не иметь schedulable ownership. Для `pending_artifact` path дополнительно опасен destructive move из optional до доказанного commit.

Причина: dequeue одновременно используется как выбор work item и как окончательная передача ownership, хотя следующий owner ещё не подготовлен.

Предлагаемый фикс: разделить наблюдение и commit очереди. Добавить private `PeekFront()`/`PopFrontNoThrow()` либо эквивалентный in-flight token. Job остаётся authoritative queue owner, пока processing path не перешёл в `Ready/Failed` либо не опубликовал durable waiting/retry owner. `pending_artifact` не перемещать destructively из slot до final commit; finalization работает через owned/staged artifact, который остаётся доступным после local failure. Убрать allocation-dependent `loading_resources_` из post-dequeue gap либо подготовить его ownership до удаления job.

Regression/evidence: fault сразу после выбора front job, при marking loading и в pending-artifact finalization не теряет job. После снятия fault ресурс обрабатывается ровно один раз, queue/waiting ownership не дублируются, loader не вызывается повторно после уже принятого artifact.

### G3-STR-001 — Streaming Tick превращает local allocation failure в semantic request failure

Проблема: `StreamingRuntime::Tick()` ловит широкие exceptions при построении work list и обработке request, включая `std::bad_alloc` из Runtime-owned staging.

Что не так: allocation failure может увеличить `statistics_.failed`, откатить request либо превратить его в controlled provider/runtime failure. Это нарушает общую allocation policy: pre-call observable state должен сохраняться, если authoritative operation ещё не была принята.

Причина: один catch scope смешивает external provider/backend exceptions и локальные allocation failures Runtime.

Предлагаемый фикс: `std::bad_alloc` из Runtime-owned preparation/staging не конвертировать в streaming semantic failure. Preflight allocations выполняются до request mutation и при failure пробрасывают `std::bad_alloc` с неизменными request/statistics. Catch external provider exceptions должен охватывать только сам external call; после него применяются существующие commit/rollback flags.

Regression/evidence: fault в `BuildWorkList`, reserve result и local staging сохраняет requests/statistics; provider exception по-прежнему даёт документированный controlled request failure.

### G3-STR-002 — successful `IStreamingDataSource::ExecuteStep()` не имеет durable acceptance до local commit

Проблема: `StreamingRuntime::ExecutePlanStep()` получает successful `ExecuteStep(...)`, после чего ещё проверяет actual byte budget/counter overflow, резервирует revision и только затем двигает `processed_bytes`/cursor.

Что не так: callback уже мог принять logical step у внешнего data source, а последующий Runtime-local failure оставляет request на том же cursor. Retry снова вызывает `ExecuteStep()` для того же step. Public `IStreamingDataSource` не гарантирует idempotency такого повторения и не имеет stable step-acceptance token.

Причина: successful external step result не получает durable Runtime ownership до последующих local validation/publication boundaries.

Предлагаемый фикс: не вводить новый transaction API. Добавить private `pending_step_result` в `RequestRecord`. После successful callback немедленно сохранить `StreamingStepResult` no-fail в pending state вместе с cursor identity. Все budget/revision/local commit операции выполняются уже из pending result. При retry существующий pending result используется без повторного callback. Pending очищается только после commit cursor/accounting либо при окончательном semantic failure, который явно завершает request. Public contract дополнить правилом: Result failure/exception от `ExecuteStep()` не означает accepted step; Result success принимается Runtime ровно один раз.

Regression/evidence: fake data source увеличивает `execute_count`; fault после successful `ExecuteStep()` и до cursor/accounting commit оставляет `execute_count == 1`; retry использует pending result, итог совпадает с clean execution.

### G3-SIM-001 — успешный `ExecuteStep()` может быть выполнен повторно после local staging failure

Проблема: `SimulationRuntime` вызывает внешний `ISimulationJob::ExecuteStep()`, получает success, а затем выполняет fallible Runtime-owned staging результата/proposals.

Что не так: job implementation может уже изменить собственное состояние. Если последующий Runtime allocation получает `std::bad_alloc`, `JobRecord` остаётся до commit, и retry вызывает `ExecuteStep()` ещё раз для того же logical step.

Причина: external authoritative callback расположен раньше, чем Runtime гарантировал durable ownership его успешного результата.

Предлагаемый фикс: перед callback подготовить всё, что можно подготовить без знания результата. После successful `ExecuteStep()` результат должен немедленно перейти no-fail move в private durable `pending_step_result` конкретного `JobRecord`. Пока pending result не опубликован в Runtime state/proposal queues, повторный Tick продолжает publication этого результата и не вызывает job повторно. После полного commit pending state очищается.

Regression/evidence: fake job увеличивает `execute_count`; fault после successful callback не приводит к второму ExecuteStep; retry даёт то же состояние/proposals, что clean execution, при `execute_count == 1`.

### G3-SIM-002 — post-commit housekeeping и result bookkeeping содержат allocations

Проблема: после authoritative mutations вызываются `PruneTerminalJobs()`, `PruneExpiredMemoryEvents()` и динамическое накопление `result.failures`.

Что не так: поздний `std::bad_alloc` может вырваться после commit части jobs/proposals/events, хотя вызывающий код не получает точного durable описания принятого prefix.

Причина: housekeeping строит временные vectors/work lists после, а не до mutation boundary.

Предлагаемый фикс: pruning сделать deterministic no-allocation проходом по live containers либо заранее полностью построить removal work list до первой mutation. Все result buffers (`failures` и аналогичные) резервируются до исполнения первого элемента соответствующего work list. После authoritative commit не должно оставаться Runtime-owned allocation point, необходимого для завершения этой операции.

Regression/evidence: deterministic faults на прежних prune/result boundaries либо невозможны после commit, либо происходят до mutation и оставляют exact pre-state.

### G3-PHYS-001 — acceptance semantics `IPhysicsStepper::Tick(delta)` не определена при partial fixed-step failure

Проблема: `Tick()` добавляет `delta` в accumulator до выполнения всех fixed steps. Один или несколько backend steps могут успешно завершиться, после чего следующий step/sync возвращает failure.

Что не так: caller не знает, принят ли переданный `delta`. Повторная передача того же delta может добавить время второй раз, несмотря на уже выполненный successful prefix.

Причина: public contract описывает Result, но не фиксирует точку acceptance и retry semantics для multi-step Tick.

Предлагаемый фикс: зафиксировать prefix-progress contract без введения нового gameplay API. После успешных validation/preflight добавление `delta` в accumulator является точкой acceptance и происходит ровно один раз. Failure до этой точки сохраняет pre-state. Failure после acceptance не требует повторной передачи delta; уже завершённые fixed steps не переигрываются. Если backend simulation уже success, а local snapshot/contact sync failed, существующий pending-sync state обязан завершить именно этот backend step без второго `SimulateFixed`. Public comments и tests должны прямо это описывать.

Regression/evidence: failure на первом backend call, на втором после одного success и при sync после backend success; backend call counts, accumulator и consumed interval соответствуют одному принятию delta.

### G3-NAV-001 — byte-budget estimate расходится с фактической reference path shape

Проблема: `EstimatedPathBytes()` при наличии obstacle source использует branch, который исключает cost-provider midpoint, тогда как `BuildReferenceResult()` после non-blocking obstacle всё равно применяет cost provider и может добавить midpoint.

Что не так: при obstacle provider без blocker и cost > 1 estimate считает 2 points, а результат содержит 3 points. Query может пройти `max_bytes`, хотя реальный path превышает budget.

Причина: две функции независимо кодируют правила построения reference path и уже разошлись.

Предлагаемый фикс: вынести единый private calculation `ReferencePathShape/PointCount`, который выполняет obstacle и cost decisions один раз. `EstimatedPathBytes()` и `BuildReferenceResult()` используют этот один результат. Budget check выполняется до allocation result vector.

Regression/evidence: non-blocking obstacle + cost > 1 + budget между размером 2 и 3 `Vec3` должен deterministically вернуть budget failure и не публиковать oversized result.

### G3-NAV-002 — local allocation failure может стать semantic path failure и повторно вызвать backend

Проблема: backend/provider call, Runtime-owned validation/copy и catch `std::exception` находятся в одном exception scope.

Что не так: `std::bad_alloc` при копировании успешного backend path может пометить query `Failed`; retry может повторно вызвать backend, хотя тот уже успешно сформировал результат. Reference provider path аналогично маркирует local allocation как `provider_exception`.

Причина: exception boundary не отделяет external callback от Runtime staging/publication.

Предлагаемый фикс: catch provider/backend exceptions только вокруг external call. Все Runtime allocations выполняются до callback либо после success сохраняются в durable pending result no-fail move. Local `std::bad_alloc` сохраняет query state и не конвертируется в path semantic failure. Backend `BuildPath` для query contract фиксируется как read/replay-safe; Runtime-owned publication failure не должна требовать его повторного вызова.

Regression/evidence: backend call counter + successful path; injected local publication failure не переводит query в `Failed` и не вызывает backend второй раз только из-за Runtime staging.

### G3-NAV-003 — `Tick()` purges queries до последующей fallible work-list preparation

Проблема: `NavigationRuntime::Tick()` сначала вызывает `PurgeReleasedAndExpired()`, затем строит query work list.

Что не так: если `BuildQueryWorkList()` получает `std::bad_alloc`, expired/released records уже удалены, хотя основной Tick не дошёл до обработки queries.

Причина: две preparation/mutation стадии расположены в неправильном порядке.

Предлагаемый фикс: сначала полностью построить purge list и query processing list без изменения live state. Только после успешной подготовки обоих lists выполнить no-fail purge commit и далее process prepared query list.

Regression/evidence: при наличии expired/released records fault во второй preparation оставляет record/query set точно равным pre-call state.

### G3-ANIM-001 — animator state коммитится до успешной публикации pose

Проблема: `AnimationRuntime::Tick()` меняет animator state/revision раньше, чем `IAnimationPoseSink::Publish()` гарантированно завершился.

Что не так: sink failure оставляет animator продвинутым без соответствующего external pose; retry уже не эквивалентен clean execution.

Причина: локальный commit расположен перед external publication boundary.

Предлагаемый фикс: полностью построить candidate animator state, pose buffer и `shared_ptr<const PoseBuffer>` off-state. Вызвать pose sink с candidate pose. Только после successful sink publication одним no-fail commit заменить live animator state и revision. Public sink contract должен требовать: Result failure/exception означает, что pose не был externally committed; если конкретный sink не может это гарантировать, ему понадобится собственная idempotent/dedup mechanism, но её не добавлять без реального consumer requirement.

Regression/evidence: sink failure/exception сохраняет exact animator pre-state; retry даёт тот же pose/state, что clean Tick.

### G3-ANIM-002 — semantic animation events молча теряются при allocation failure

Проблема: `QueueEvent(...) noexcept` поглощает allocation failure. При полном event buffer код также может удалить старое событие до неудачного push нового.

Что не так: Play/Stop/Crossfade/Tick могут успешно изменить authoritative animator state, но соответствующее semantic event исчезает без error/reconciliation.

Причина: bounded-event policy и allocation policy реализованы одной `noexcept` функцией без durable pending state.

Предлагаемый фикс: event storage должен гарантировать no-fail publication после animator commit. Для baseline использовать заранее выделенный bounded ring buffer фиксированной capacity, установленной при runtime creation; enqueue в ring не аллоцирует. Overflow policy фиксируется отдельно и выполняется без allocations. Не использовать `std::vector/deque` growth в post-commit semantic event path.

Regression/evidence: Play/Stop/Crossfade/Tick на заполненном buffer и при allocation fault до commit дают документированную overflow semantics без silent loss из-за allocator failure; после state commit enqueue не может бросить.

### G3-AUDIO-001 — внешний audio backend не имеет freeze-level retry/commit contract

Проблема: один Audio Tick последовательно вызывает listener/spatial/gain/stop operations и затем `Update(delta)`, но `IAudioBackend` не фиксирует semantics failure после частично выполненной последовательности.

Что не так: если ранняя backend команда уже применена, а поздняя команда или `Update` failed, Runtime может retry последовательность. Без idempotency/no-commit guarantees это способно задвоить временное продвижение или оставить backend и local state рассинхронизированными.

Причина: ownership и retry policy есть в Runtime, но public backend boundary не описывает, что означает Result failure/exception.

Предлагаемый фикс: не добавлять transaction API. Зафиксировать минимальный backend contract: setters/control projections в Tick idempotent при повторной передаче того же значения; их failure/exception либо не применяет command, либо повтор того же command безопасен. `Update(delta)` failure/exception строго означает, что delta не был принят/продвинут. Reference backend и test fake должны доказать этот contract. Если существующий реальный backend не способен выполнить это правило, только тогда требуется отдельный durable reconciliation token; заранее его не проектировать.

Regression/evidence: fault на каждой backend command и `Update`; retry не удваивает temporal advance и приводит local/backend projection к clean state.

### G3-AUDIO-002 — rollback только что созданного backend voice может потерять cleanup ownership

Проблема: `AudioRuntime::RollbackCreatedVoice()` пытается выделить место в `pending_voice_cleanups_` уже после успешного `CreateVoice()`, непосредственно перед rollback `DestroyVoice()`. Если `reserve()` получает `std::bad_alloc`, код продолжает rollback без гарантированного retry slot.

Что не так: если после этого `DestroyVoice()` возвращает failure или backend бросает exception, созданный voice остаётся живым во внешнем backend, но Runtime не сохраняет `BackendVoiceHandle` и pinned clip resource в `pending_voice_cleanups_`. Cleanup ownership теряется без возможности повторить destroy.

Причина: durable retry ownership подготавливается после external ownership acquisition, а не до него. `RollbackCreatedVoice()` пытается создать bookkeeping только в failure path, где allocation уже не может считаться безопасной.

Предлагаемый фикс: до любого вызова backend `CreateVoice()` заранее резервировать достаточную capacity `pending_voice_cleanups_` для максимального количества voices, которое текущая операция способна создать. Для `Play()` и `FadeIn()` это минимум один slot; для Tick/one-shot batch нужно preflight по точному либо безопасному верхнему числу возможных новых voices до первого `CreateVoice()`. После successful CreateVoice `RollbackCreatedVoice()` больше не должен выполнять `reserve()` или иные allocations. Если `DestroyVoice()` не подтвердил cleanup, `VoiceOwnership{voice, clip_resource}` помещается в заранее подготовленную capacity и остаётся retryable. Если preflight capacity allocation не удалась, операция обязана завершиться до вызова `CreateVoice()`.

Regression/evidence: deterministic allocation failure на cleanup-capacity preflight не вызывает `CreateVoice()`; после successful `CreateVoice()` и последующего injected failure `DestroyVoice()` handle всегда остаётся в pending cleanup ownership; повторный cleanup уничтожает voice ровно один раз; clip resource остаётся pinned до confirmed destroy.

### G3-REN-001 — failure `AbortFrame()` теряется, после него разрешён новый backend frame

Проблема: `AbortFrameNoThrow()` игнорирует Result failure и exceptions. После failed Submit/End runtime переводит frame в Failed, но не сохраняет факт, что backend frame мог остаться открытым.

Что не так: следующий `PrepareFrame/RenderFrame` способен вызвать новый `BeginFrame()` поверх незавершённого backend frame.

Причина: local frame state и external backend-frame ownership не имеют reconciliation marker.

Предлагаемый фикс: добавить private `frame_recovery_pending_`. Любой путь, где backend frame мог быть открыт и `AbortFrame()` не подтвердил success, ставит marker. Пока marker установлен, новый `BeginFrame` запрещён. Следующий `PrepareFrame`, `RenderFrame` или `Shutdown` сначала повторяет Abort/reconciliation; только successful Abort очищает marker. Public `IRenderCommandSink` contract отдельно фиксирует semantics failed `BeginFrame`: failure не открывает frame. Resize API в Goal 3 не добавлять.

Regression/evidence: Begin success, Submit/End failure, первый Abort failure, второй Abort success; новый Begin не вызывается до recovery, затем frame pipeline снова работает.

### G3-PERS-001 — `InMemoryPersistenceBackend::CommitSnapshot()` не даёт strong commit guarantee

Проблема: reference backend выполняет `snapshot_ = snapshot` напрямую.

Что не так: `PersistenceSnapshot` содержит несколько vectors. Copy assignment может успеть заменить часть полей и получить `std::bad_alloc` на следующем vector. Предыдущий durable snapshot тогда частично повреждён, хотя public `IPersistenceBackend` обещает сохранить его при failed commit.

Причина: fallible deep copy выполняется непосредственно в durable storage.

Предлагаемый фикс: сначала полностью построить local `PersistenceSnapshot candidate = snapshot`. После успешной deep copy опубликовать candidate в durable state только через доказанно no-throw swaps/moves всех fields. `durability` contract остаётся на `CommitSnapshot`; новые `Save/Flush` API не вводятся.

Regression/evidence: deterministic allocation fault в каждой vector-copy стадии; `Load()` после каждого failure возвращает exact previous snapshot, successful retry возвращает exact new snapshot.

### G3-SUP-001 — Scene transform adapter может повторно применить уже successful projection

Проблема: `RuntimeSceneTransformAdapter::Flush()` выполняет `SetWorldTransform` последовательно, но заменяет `pending_` на retry list только после завершения всего loop. Exception из позднего callback оставляет исходный `pending_` со всеми entries.

Что не так: если transform A успешно применён, transform B бросил exception, retry снова применяет A.

Причина: external success prefix не переносится сразу в durable ownership retry state.

Предлагаемый фикс: до первого external write полностью подготовить retry storage. Для каждой записи successful `SetWorldTransform` немедленно считается принятой и больше не входит в retry ownership; Result failure/exception сохраняет только эту запись в preallocated retry set и сохраняет first error. После обхода всех независимых entries одним no-fail swap заменить `pending_` на retry set, затем вернуть first failure. Allocation после первого successful projection недопустим.

Regression/evidence: A success, B failure/throw; после первого Flush pending содержит только B; retry не вызывает A второй раз и сохраняет deterministic order.

### G3-SUP-002 — coordinator может получить allocation failure после уже выполненного frame prefix

Проблема: `EngineRuntimeCoordinator::Tick()` последовательно изменяет majors, а `last_update_order`, `RuntimeTickResult.executed_steps/failures` и `RuntimeFrameEvents` материализуются динамически во время или после этих mutations.

Что не так: поздний `std::bad_alloc` может выйти после Time/Resources/Simulation и других accepted phases. Caller не получает надёжного результата, но повтор полного Tick способен переиграть уже выполненный prefix.

Причина: coordinator не имеет durable ownership accepted frame/progress, а bookkeeping остаётся fallible после начала orchestration.

Предлагаемый фикс: добавить private `PendingCoordinatorFrame`, создаваемый и полностью инициализируемый до первой major phase. Он хранит accepted `RuntimeFrameInput`, phase cursor и moved per-phase results/source-event batches. Cursor продвигается только после того, как результат phase durably помещён в pending frame. Если последующая result/event materialization получает `std::bad_alloc`, pending frame и cursor сохраняются; повторный Tick с тем же input продолжает с cursor, не повторяя завершённые phases. Пока pending frame существует, другой input получает controlled `frame_retry_required` без mutation. После успешной финализации public `RuntimeTickResult` pending frame очищается. Для shutdown отдельно заранее reserve фиксированного списка shutdown steps до установки `shutdown_started_`.

Regression/evidence: faults после нескольких разных phases показывают, что повтор вызывает только незавершённый suffix; order и failures совпадают с clean execution, а новый frame input не принимается до reconciliation.

### G3-SUP-003 — `IRuntimeEventSink` exception/commit semantics не определены

Проблема: coordinator обрабатывает Result failure `event_sink->Publish(events)`, но exception из sink может выйти наружу. Batch не имеет stable identity для dedup commit-then-fail.

Что не так: source event buffers очищаются только после Result success, что правильно, но exception path не является documented containment boundary. Если sink внешне committed batch и затем сообщил failure/throw, retry задублирует events.

Причина: public sink boundary не определяет atomicity publication.

Предлагаемый фикс: зафиксировать минимальный contract `IRuntimeEventSink`: Result failure или exception означает, что batch не был externally committed. Coordinator ловит sink exception в DiagnosticsEvents boundary, сохраняет source buffers и записывает phase failure. Buffers очищаются только после confirmed success. Reference `InMemoryRuntimeEventSink::Publish()` должен сначала полностью построить candidate batch, а затем заменить `last_` только через no-throw swap/move; прямой fallible aggregate assignment в authoritative state недопустим. Stable batch id/dedup API не добавлять, пока реальный sink не потребует commit-then-fail semantics.

Regression/evidence: Result failure и throw-before-commit сохраняют source buffers; clean retry публикует batch ровно один раз и только после success очищает sources.

### G3-SUP-004 — Animation/Audio resource adapters имеют post-acquire lease publication gap

Проблема: `RuntimeAnimationResourceSource::ResolvePayload()` и `RuntimeAudioResourceSource::{GetSoundState,LoadClip}` сначала успешно вызывают `IResourceManager::RequestLease()`, а затем делают `held.emplace(...)`. В `LoadClip()` дополнительно lease удаляется из `held_` до successful allocation wrapper `LeasedAudioClipResource`.

Что не так: `std::bad_alloc` после успешного `RequestLease()` способен оставить acquisition без локального owner, поэтому его больше некому `Release()`. В wrapper path lease может потеряться между erase старого owner и созданием нового.

Причина: локальный ownership slot готовится после внешнего acquisition, а transfer выполняется до готовности destination owner.

Предлагаемый фикс: использовать тот же pattern, который уже применён в `RuntimeRenderResourceBridge`: сначала выделить пустой local ownership slot, затем вызвать `RequestLease()`, после success заполнить существующий slot no-fail; при manager failure удалить пустой slot. Для передачи lease в audio wrapper сначала полностью создать destination wrapper/lease-state entry, затем no-fail снять source ownership. Ни одного allocation boundary между relinquish source owner и publish destination owner.

Regression/evidence: deterministic fault на local slot publication и wrapper creation после successful `RequestLease`; ResourceManager reference/acquisition count возвращается к pre-state либо lease остаётся в явном retry owner; Shutdown не обнаруживает потерянных acquisitions.

### G3-SUP-005 — audio lease cleanup path объявлен `noexcept`, но сам может бросить/аллоцировать

Проблема: `RuntimeAudioLeaseState::ReleaseWrapper()` и `ReleaseOrQueue()` вызывают fallible `manager_->Release()`, а при failure делают `pending_releases_.push_back(...)`. `ReleaseWrapper()` вызывается из destructor `LeasedAudioClipResource` и объявлен `noexcept`.

Что не так: exception из custom manager либо `std::bad_alloc` при `push_back` внутри destructor path приводит к `std::terminate`. Даже без exception нет гарантии, что failed cleanup ownership будет сохранён при allocation failure.

Причина: retry bookkeeping создаётся только после failed cleanup, то есть в момент, когда destructor уже не имеет права аллоцировать/бросать.

Предлагаемый фикс: pending cleanup ownership должен существовать до передачи lease wrapper-у. Хранить durable lease entries в `RuntimeAudioLeaseState`; wrapper содержит stable token/entry identity. Destructor только no-fail помечает existing entry как `release_requested` и выполняет contained best-effort `Release`. При failure entry остаётся существующей для `Drain()`, без `push_back` и без allocations. `Drain()` удаляет entry только после confirmed release. Все exceptions ResourceManager containment выполняются внутри lease state.

Regression/evidence: throwing/failing manager и injected allocation pressure во время wrapper destruction не завершают процесс; lease остаётся pending, `Drain()` retry освобождает его ровно один раз, `ActiveWrappers()` и pending ownership сходятся к нулю.

### G3-SUP-006 — reference audio backend потребляет voice ID до publication commit

Проблема: `ReferenceAudioBackend::CreateVoice()` вызывает `AllocateMonotonicId(next_voice_)` до `voices_.emplace(...)`.

Что не так: если map publication получает `std::bad_alloc`, voice не существует, но monotonic ID уже потреблён. Это нарушает общий identity allocation contract Runtime.

Причина: ID allocation используется как immediate commit вместо reserve/commit.

Предлагаемый фикс: заменить на `ReserveMonotonicId`/`PeekMonotonicId`. Сначала получить reserved value без продвижения allocator, затем опубликовать voice record; только после successful emplace выполнить no-fail `Commit()`.

Regression/evidence: injected failure при `voices_.emplace` сохраняет `next_voice_`; следующий successful CreateVoice получает тот же ID, который получил бы clean execution.

### G3-SUP-007 — reference animation pose bridge обновляет два индекса неатомарно

Проблема: `RuntimeAnimationPoseBridge::Publish()` сначала может удалить old owner/animator mappings, затем отдельно выполняет `poses_by_animator_[...] = pose` и `poses_by_owner_[...] = pose`.

Что не так: allocation failure на первой или второй map publication оставляет два индекса рассинхронизированными либо уже удаляет предыдущий authoritative projection. Это также нарушает sink semantics, требуемую `G3-ANIM-001`: failure публикации не должен означать partial external commit.

Причина: два derived authoritative indexes мутируются напрямую вместо candidate transaction.

Предлагаемый фикс: построить candidate copies обоих maps, выполнить stale mapping removal и обе new publications только в candidates, затем после полной успешной preparation no-fail swap обоих maps. Альтернатива через preallocated node handles допустима, если доказан тот же strong guarantee.

Regression/evidence: fault на preparation каждого индекса сохраняет exact old pose mapping в обоих indexes; success одновременно заменяет animator/owner mapping; Animation retry после sink failure эквивалентен clean Tick.

## 3.0.4. Общие AUDIT-классы риска

Для каждого из 17 modules проверить, не предполагая заранее необходимость code change:

[ ] Все public enum inputs отклоняют значения вне declared domain до mutation.

[ ] Все public numeric inputs отклоняют NaN/infinity, отрицательные значения и overflow там, где они недопустимы.

[ ] Все ID/generation/revision/sequence allocators имеют exhaustion test и не wrap-around.

[ ] Semantic no-op не публикует ложный revision/state/event и не вызывает внешний effect, если contract не говорит обратное.

[ ] Любая multi-state mutation сохраняет согласованность на allocation failure либо имеет явно documented prefix-progress/reconciliation semantics.

[ ] Любой loader/provider/backend/sink/source callback считается throwing boundary. Exceptions не выходят через public operation, если contract обещает `Result` containment.

[ ] Если внешний effect уже commit-нут и rollback не гарантирован, module сохраняет durable retry/reconciliation ownership.

[ ] Query results, snapshots и returned collections detached от mutable internal state там, где contract обещает snapshot/value semantics.

[ ] Observable ordering не зависит от `unordered_map` iteration.

[ ] Budget semantics определены отдельно для item/time/byte limits: hard, soft-current-unit, defer либо backpressure.

[ ] Cleanup semantics проверяются только для реально принадлежащего module ownership.

---

## 3.0.5. Результат дополнительного deep-pass текущего Runtime

Повторный проход по production code не выявил подтверждённых defects, требующих обязательного code change, в `Serialization`, `Assets`, `Scene`, `World` и `Environment`. Эти modules остаются полноценными AUDIT targets: отсутствие записи `G3-*` означает «проверить и доказать текущий contract», а не «рефакторить для единообразия».

Дополнительные confirmed defects deep-pass сосредоточены в `Resources`, `Streaming` и `Support`, где external ownership/callback success пересекается с более поздней fallible Runtime publication. Их нельзя оставлять общими checklist-пунктами, поэтому они перечислены отдельными IDs выше.

# 3.0.A. Admission gate перед началом Runtime

Goal 2 закрыт. Текущий admission baseline:

[x] EngineBase `9/9 LOCAL_READY`.

[x] Base Debug/Release: `14/14` и `14/14`.

[x] Runtime Debug/Release: `34/34` и `34/34`.

[x] Full Debug/Release: `94/94` и `94/94`.

[x] Суммарная локальная qualification: `284/284` CTest по recorded Goal 2 report.

[x] Все шесть exact `ctest_manifest --check-profile` входят в текущую qualification evidence.

[x] `local_ready_contract`: `37/37` criteria, `78/78` ledger records, Base `9/9 LOCAL_READY`.

[x] Architecture, dossier, coverage, public API (`4309` exact callables), public surface (`232` headers), CI contract validators зелёные.

[x] Validator negative/self-tests зелёные, включая CMake ArchitectureFreeze adversarial fixtures.

[x] Goal 2 фиксирует `git diff --check` clean и отсутствие известного Base debt, переносимого в Goal 3.

Текущие числа `14/34/94` являются admission baseline. После добавления Runtime tests manifest обязан регенерироваться; эти числа не hardcodeятся как финальный Goal 3 count.

---

# 3.0.B. Единый алгоритм аудита каждого Runtime module

Каждый подпункт 3.1–3.17 выполняется одинаково.

## Шаг A. Contract inventory

[ ] Сверить public headers с Goal 1 API inventory.

[ ] Отметить все mutators, lifecycle operations, factories, callbacks/backends/providers и snapshot/query APIs.

[ ] Для каждого mutator записать preconditions, success state, no-op semantics и failure semantics.

[ ] Для каждого внешнего port записать ownership и exception/failure contract.

[ ] Исправить ложные `DISCOVERED` поля dossier и перевести реально проверенные поля в `REVIEWED`.

## Шаг B. State inventory

[ ] Выписать authoritative containers/records.

[ ] Выписать derived indexes/cache/queues.

[ ] Выписать ID/generation/revision/cursor counters.

[ ] Выписать pending cleanup/reconciliation state.

[ ] Выписать persistent и transient state. Если persistence для module `N/A`, указать конкретную причину.

## Шаг C. Mutation audit

[ ] Happy path.

[ ] Invalid input.

[ ] Duplicate identity.

[ ] Stale identity.

[ ] Wrong lifecycle/state.

[ ] Boundary/exhaustion.

[ ] Semantic no-op.

[ ] Allocation failure после начала потенциальной mutation.

[ ] External callback/backend failure и exception, если применимо.

[ ] Retry после controlled failure, если operation retryable.

## Шаг D. Regression acceptance

Если любой AUDIT-пункт не проходит, до изменения production code добавить в этот документ локальный `DEFECT` record с пятью обязательными полями: `Проблема`, `Что не так`, `Причина`, `Предлагаемый фикс`, `Regression/evidence`. Только после этого выполнять fix. Это запрещает менять contract по догадке и сохраняет однозначную историю Goal 3.

[ ] Любой найденный defect сначала получает failing regression.

[ ] Fix проходит regression и весь module suite.

[ ] После module fix прогоняется Runtime isolated suite и Runtime regression suite.

[ ] Изменение public contract обновляет API/public-surface manifests и affected evidence anchors.

Только после A–D module получает `LOCAL_READY`. Если AUDIT-пункт уже доказан существующими code/tests/evidence, достаточно зафиксировать anchors; production change не требуется.

---

# 3.1. RuntimeFoundation

Фактическая роль: общие Runtime IDs, budgets, time arithmetic, neutral states и spatial value types.

## IDs и handles

[ ] Все numeric Runtime IDs имеют единственный invalid state `0`.

[ ] `AssetId` и `ResourceId` имеют единый invalid `StringId` state и не смешиваются с numeric ID spaces.

[ ] Compile-time доказать невозможность случайного смешивания `RuntimeObjectId`, `PersistentObjectId`, `RegionId`, `ChunkId`, `SurfaceId`, `SimulationZoneId`, `LazyRuleId`.

[ ] Equality/hash согласованы для каждого ID type.

[ ] `PeekMonotonicId`, `ReserveMonotonicId`, `CommitMonotonicId`, `AllocateMonotonicId` проверены на `1`, normal value, `UINT_MAX`, exhausted `0`.

[ ] Failed reservation/operation не потребляет ID.

[ ] Commit reservation идемпотентно не продвигает allocator второй раз.

## Budget

[ ] `RuntimeBudget{}` означает unlimited.

[ ] Negative time budget отклоняется.

[ ] Zero item/byte/time имеют documented unlimited semantics.

[ ] Max item/byte values не приводят к signed conversion или overflow в consuming majors.

[ ] Документировать, что `RuntimeBudget` является value contract, а hard/soft semantics задаёт конкретный major.

## Time arithmetic

[ ] `CheckedSecondsToMicroseconds` отклоняет negative, NaN, infinity и overflow.

[ ] `CheckedScaleDuration` отклоняет negative/non-finite scale и overflow.

[ ] `CheckedAdd`, `CheckedSubtract`, `CheckedDifference` покрыты на `INT64_MIN/MAX`, включая `INT64_MIN` duration.

[ ] Saturating operators насыщаются в правильную сторону и не содержат signed UB.

## Spatial

[ ] `Vec3`, `Quat`, `Transform`, `Aabb` finite validation покрыта NaN/infinity по каждой component family.

[ ] Не добавлять общий `IsValidSphere` только ради freeze. Проверить существующие Sphere/query boundaries; новый common validator допустим только если одинаковая authoritative validation реально нужна нескольким независимым consumers.

[ ] `Normalize(valid quat)` возвращает normalized finite quaternion.

[ ] Поведение `Normalize(zero/invalid)` документировано. Текущее fallback-поведение в identity quaternion не должно использоваться как замена authoritative input validation.

[ ] `IsValidTransform` проверяет finite fields, normalized rotation и non-zero scale.

[ ] `IsValidAabb` отклоняет inverted bounds.

[ ] `TransformAabb` проверен для rotation, negative scale и degenerate bounds.

[ ] Закрыть `G3-RF-001`: зафиксировать уже существующую deterministic approximate TRS semantics и синхронизировать module docs/tests с public headers.

## Exit 3.1

[ ] Все RuntimeFoundation public helpers классифицированы и имеют boundary evidence.

[ ] Нет недокументированного spatial fallback, который превращает invalid authoritative input в valid state.

[ ] `EpidemicRuntimeFoundationTests` проходит Debug/Release.

[ ] RuntimeFoundation = `LOCAL_READY`.

---

# 3.2. Time

Фактическая роль: authoritative Runtime game clock, rational fractional accumulation, calendar/day phase, clock events.

## Configuration

[ ] `game_ticks_per_real_second > 0`.

[ ] `TimeScale numerator/denominator > 0` и normalizes через GCD.

[ ] Calendar units positive и multiplication boundaries overflow-safe.

[ ] Phase boundary enum valid, minute внутри configured day, duplicate minute rejected.

[ ] Empty phase list получает documented default boundaries.

## Advance/Skip

[ ] `Advance(0)` является no-op по authoritative revision.

[ ] Negative real delta rejected без mutation.

[ ] Normal delta и large delta.

[ ] Fractional remainder сохраняется между frames без floating drift.

[ ] Разные frame splits дают одинаковый `now` и remainder-equivalent future behavior.

[ ] `SetTimeScale` сбрасывает remainder ровно по текущему contract.

[ ] `Skip` сбрасывает remainder ровно по contract.

[ ] Scale/delta numerator/denominator multiplication overflow rejected before commit.

[ ] Game time `INT64` overflow rejected с полным pre-state preservation.

## Lifecycle/events/revision

[ ] Pause/Resume state machine.

[ ] Duplicate Pause и duplicate Resume являются no-op и не стирают предыдущие observable events неожиданно.

[ ] Day/month/year boundaries.

[ ] Dawn/Day/Dusk/Night boundaries.

[ ] Один large Advance/Skip может пересечь несколько semantic boundaries без потери обязательных events.

[ ] `last_delta` является transient и сам по себе не повышает authoritative revision.

[ ] Revision exhaustion rejected before mutation/event publication.

[ ] Allocation failure при построении event vector/candidate сохраняет полный pre-call state.

## DEFECT G3-TIME-001: persistence-complete clock state

`TimeSnapshot` остаётся observational snapshot. Не использовать его как save format.

[ ] Добавить отдельный persistent clock state/checkpoint: `now`, normalized current `time_scale`, `paused`, fractional remainder и authoritative revision.

[ ] Добавить capture API и validate-first/candidate-build restore API.

[ ] Restore проверяет immutable configuration compatibility: `game_ticks_per_real_second`, calendar definition и phase boundaries.

[ ] `last_delta`, current event buffer и operation-only `TimeRuntimeState` остаются transient, если на них нет отдельного persistent contract.

[ ] Failed validation/allocation restore сохраняет полный pre-state.

[ ] После successful restore следующий `Advance()` детерминированно совпадает с uninterrupted control run, включая случай ненулевого fractional remainder.

## Exit 3.2

[ ] `G3-TIME-001` закрыт до `LOCAL_READY`; Goal 5 может использовать готовый clock persistence contract без изменения frozen Time API.

[ ] Existing deterministic accumulation tests сохранены и дополнены persistent-state roundtrip/continuation tests.

[ ] `EpidemicRuntimeTimeTests` проходит Debug/Release.

[ ] Time = `LOCAL_READY`.

---

# 3.3. Serialization

Фактическая роль: immutable versioned documents, archive reader/writer, serializer registry, migration graph/executor.

## Archive writer/reader

[ ] Duplicate object field rejected.

[ ] Empty field name rejected.

[ ] Begin/End object mismatch controlled.

[ ] Begin/End array mismatch controlled.

[ ] Array element index validated.

[ ] Один array slot нельзя записать дважды.

[ ] Incomplete array нельзя Finalize.

[ ] Deep object/array, empty structures, bytes и null roundtrip.

[ ] Finalize требует valid type ID, supported/non-zero schema contract и valid root.

[ ] После успешного Finalize writer immutable.

[ ] Allocation sweep `BeginObject`, `BeginArray`, `BeginArrayElement`, `Write*`, `Finalize` подтверждает отсутствие partial archive tree publication. Учитывать существующий pre-reserve design, не переписывать его без причины.

[ ] Reader wrong type/kind/index/missing field даёт controlled failure.

## Serializer

[ ] Registry duplicate type rejected.

[ ] Registry null serializer/invalid type rejected.

[ ] Freeze idempotent; registration после freeze rejected.

[ ] Serializer metadata/body exceptions contained согласно cross-engine policy.

[ ] Deserialization строит candidate и не mutates output object до полного success.

[ ] Throw/failure во время commit semantics не оставляет half-mutated destination.

[ ] Typed serializer rejects wrong C++ type и wrong document type.

## Migration

[ ] Exact migration и multi-step chain.

[ ] Missing path.

[ ] Cycle.

[ ] Ambiguous path.

[ ] Каждый step сохраняет `type_id`.

[ ] Каждый step возвращает ровно declared target schema.

[ ] Intermediate output полностью валидируется до следующего step.

[ ] Migration exception contained.

[ ] Failed migration не изменяет source immutable document.

[ ] Migration path search allocation failure не меняет registry.

## Exit 3.3

[ ] Writer/serializer/migration failure atomicity доказана fault tests.

[ ] `EpidemicRuntimeSerializationTests` проходит Debug/Release.

[ ] Serialization = `LOCAL_READY`.

---

# 3.4. Resources

Обязательные confirmed fixes: `G3-RES-001`, `G3-RES-002`, `G3-RES-003`, `G3-RES-004`.

Фактическая роль: `ResourceLease` ownership, load queue, dependency leases/graph, payload residency, memory accounting и eviction.

## Identity и ownership

[ ] Каждый `RequestLease` создаёт новый acquisition ID.

[ ] Два consumers одного resource делят `ResourceHandle`, но имеют независимые acquisition IDs.

[ ] Release одного consumer не освобождает lease другого.

[ ] Duplicate/double release rejected.

[ ] Stale `ResourceHandle` после generation advance не получает новый payload.

[ ] Acquisition ID exhaustion не меняет queue/cache/reference count.

[ ] Generation exhaustion не допускает ABA через eviction/recreate.

[ ] Reference count overflow rejected before publication.

## Request/load queue

[ ] Invalid ResourceId/ResourceType rejected.

[ ] Один Ready resource не загружается повторно при новом lease.

[ ] Queue publication failure откатывает новый slot и acquisition state.

[ ] Acquisition publication failure откатывает queued job и slot publication.

[ ] Retry после loader failure не создаёт duplicate active load.

[ ] Loader exception contained.

[ ] Artifact ID/type обязаны совпадать с request.

[ ] Null payload rejected.

## Dependencies

[ ] Self dependency rejected.

[ ] Deep dependency cycle detected без recursive stack overflow.

[ ] Required dependency failure fails root.

[ ] Optional missing/failing dependency не блокирует root по contract.

[ ] Dependency RequestLease ownership публикуется atomically.

[ ] Failure публикации dependency ownership освобождает только lease текущей операции.

[ ] Rollback root не снимает shared dependency lease другого root/consumer.

[ ] Failed dependency release сохраняет lease в retryable bookkeeping, а не забывает ownership.

[ ] Dependency graph соответствует реально удерживаемым blocking dependencies.

## Budget/accounting/eviction

[ ] `max_items` ограничивает attempts/processing согласно documented statistics semantics.

[ ] `max_time` прекращает взятие новых jobs, не оставляя текущий slot half-mutated.

[ ] `max_bytes` является soft-current-unit budget: начатый payload может завершиться, затем новые jobs не начинаются.

[ ] `resident_bytes`, `bytes_loaded`, resource counts overflow-safe.

[ ] Memory budget failure не публикует payload и освобождает acquired dependencies.

[ ] Evict запрещён при references > 0.

[ ] `Evicting` является retryable prefix-progress state.

[ ] Повторный eviction не освобождает уже освобождённую dependency второй раз.

[ ] Evict generation advances только после завершения required cleanup.

## Cancellation semantics

В Resources нет public `Cancel()`. Cancellation определяется release ownership.

[ ] Release последнего queued lease гарантирует, что устаревший queued job не публикует payload.

[ ] Release последнего loading/waiting lease приводит к controlled rollback при следующей processing boundary.

[ ] In-flight/unreferenced records не становятся Ready с потерянным ownership.

## Exit 3.4

[ ] Не добавлять искусственный Shutdown API только ради старого checklist.

[ ] Все ownership/retry states наблюдаемы через tests/internal audit.

[ ] `EpidemicRuntimeResourcesTests` проходит Debug/Release.

[ ] Resources = `LOCAL_READY`.

---

# 3.5. Assets

Фактическая роль: immutable metadata catalog, logical locations, tags, dependency manifests и seal lifecycle.

## Registration/validation

[ ] Invalid AssetId/AssetType/AssetState rejected.

[ ] Duplicate ID rejected.

[ ] Version `0` rejected; version semantics определены как metadata version, не как global catalog revision.

[ ] Location kind enum validated.

[ ] Host absolute paths, UNC/device/rooted paths и Windows drive-relative paths rejected.

[ ] `..` traversal за engine root rejected.

[ ] Package/virtual location требует valid mount ID.

[ ] Generated location требует valid generator ID.

[ ] Path/dependency/tag hard limits проверены до mutation.

[ ] Duplicate tags/dependencies и self dependency обрабатываются по contract.

[ ] Allocation failure во время normalization/emplace не оставляет partial catalog entry.

## Dependency manifest

[ ] Required missing dependency fails manifest.

[ ] Optional missing dependency сохраняется/отмечается согласно contract.

[ ] Cycle detected.

[ ] Diamond dependencies deduplicated.

[ ] Deep graph обходится iteratively.

[ ] Manifest deterministic sorted независимо от unordered storage.

## Queries/seal

[ ] `FindById` возвращает detached metadata copy.

[ ] `FindByType`/`FindByTag` deterministic sorted.

[ ] Missing query controlled.

[ ] Seal idempotent.

[ ] Registration после Seal rejected без mutation.

[ ] Snapshot/catalog restore не требовать локально: в текущем public API такого contract нет. Если Assets будет загружаться из persistent catalog, это отдельный producer/bootstrap contract, а не скрытый restore.

## Exit 3.5

[ ] `EpidemicRuntimeAssetsTests` проходит Debug/Release.

[ ] Assets = `LOCAL_READY`.

---

# 3.6. Streaming

Обязательные confirmed fixes: `G3-STR-001`, `G3-STR-002`.

Фактическая роль: demand ownership, request coalescing, progressive plan, commit/rollback/unload, successor requests, shutdown.

## Demand/request identity

[ ] Несколько demands одного target имеют разные handles и один shared active request.

[ ] Demand handle содержит generation/request identity достаточную для stale rejection.

[ ] Stale demand не release/cancel новый request.

[ ] Demand/request/generation/revision/sequence exhaustion rejected atomically.

[ ] Request publication fault не потребляет identity и не оставляет record/mapping.

[ ] Demand publication fault не повреждает существующий request.

## Loading state machine

[ ] Requested -> Loading -> Prepared/Commit -> Resident/Active transitions соответствуют contract.

[ ] Incomplete progressive step не двигает cursor ложным образом.

[ ] Zero-work incomplete step не считается completion.

[ ] Commit выполняется только после полного plan completion и только один раз.

[ ] Last demand release до load выполняет cancellation/rollback.

[ ] Last demand release resident target запускает unload.

[ ] Новый demand во время reversible state использует existing request по contract.

[ ] Во время irreversible `Unloading` существует максимум один successor.

[ ] Все demands successor разделяют именно его.

[ ] Последний waiting demand удаляет successor без повреждения predecessor mapping.

## External failure boundaries

[ ] Data source plan build failure.

[ ] Data source exception.

[ ] Progressive step failure/exception.

[ ] Commit target failure/exception.

[ ] Rollback failure/exception.

[ ] Unload failure/exception.

[ ] Priority/provider/residency/world/persistence/resource port exceptions contained.

[ ] Failed rollback переводит request в explicit retry state и сохраняет ownership.

[ ] Failed unload сохраняет resident ownership и completed cleanup prefix.

[ ] Retry не повторяет уже committed external side effect.

## Budget/statistics

[ ] Invalid negative time budget rejected/normalized согласно API contract.

[ ] Item limit.

[ ] Byte limit по estimated/actual semantics.

[ ] Partial step получает только remaining budget.

[ ] Authoritative counters/identity fields reject overflow before mutation; diagnostic/statistical counters saturate только там, где это прямо зафиксировано их contract.

[ ] Deterministic request processing order при равном priority.

## Cleanup/history/shutdown

[ ] Terminal record cleanup bounded и не удаляет mapping нового successor.

[ ] Shutdown terminal: новые demands rejected с первого shutdown start.

[ ] Shutdown продолжает rollback/unload всех owned records best-effort.

[ ] Failed cleanup retryable на следующем `Shutdown()`.

[ ] `Shutdown()` complete только при отсутствии live demands/requests/temp/resident ownership.

## Exit 3.6

[ ] Все dependency ports проверены throwing fakes.

[ ] `EpidemicRuntimeStreamingTests` проходит Debug/Release.

[ ] Streaming = `LOCAL_READY`.

---

# 3.7. Scene

Фактическая роль: scene nodes, hierarchy, transforms, bounds, dirty flags, deterministic spatial queries и detached snapshot.

## Node identity/lifecycle

[ ] Create returns valid unique ID.

[ ] ID/revision exhaustion rejected before publication.

[ ] Destroy unknown/stale node rejected.

[ ] Destroy parent отсоединяет children по documented KeepWorld semantics.

[ ] Unbounded/deep hierarchy traversal выполняется iteratively; recursion допустима только для structurally bounded helper с явно доказанным hard bound.

## Hierarchy/reparent

[ ] Attach valid path.

[ ] Detach valid path.

[ ] Self parent rejected.

[ ] Cycle rejected.

[ ] KeepWorld сохраняет observable world transform в рамках frozen approximate TRS semantics.

[ ] KeepLocal сохраняет local transform.

[ ] Reparent failure сохраняет old parent child list, new parent child list, local/world transforms и revisions.

[ ] Allocation failure при staging child vectors/subtree state сохраняет полный pre-state.

## Transform/bounds

[ ] Invalid/non-finite transform rejected before mutation.

[ ] Non-invertible parent scale rejected для world->local conversion.

[ ] SetLocalTransform и SetWorldTransform обновляют subtree world results согласованно.

[ ] Bounds validation.

[ ] Rotated/non-uniform/negative scale bounds.

[ ] Dirty flags transient: clear dirty не повышает authoritative revision.

## Queries/snapshot

[ ] Query AABB/Sphere input validation.

[ ] Query result order deterministic.

[ ] Snapshot nodes deterministic sorted.

[ ] Snapshot detached и не содержит internal aliases.

[ ] Scene restore не требовать локально, если Scene официально transient materialization state. Это решение записать в dossier; восстановление выполняется системной rematerialization в Goal 5/7.

## Scope correction

[ ] Не проверять здесь `ISceneProjectionQueue`: он принадлежит Support.

[ ] Локально проверять только Scene mutation interfaces и state invariants.

## Exit 3.7

[ ] `EpidemicRuntimeSceneTests` проходит Debug/Release.

[ ] Scene = `LOCAL_READY`.

---

# 3.8. World

Фактическая роль: Runtime regions/chunks, object reality/residency/persistence identity, placement, containment и demotion tokens.

## Topology

[ ] Region registration invalid/duplicate.

[ ] Chunk registration invalid/duplicate.

[ ] Chunk обязательно принадлежит зарегистрированному region.

[ ] Chunk state transitions validated.

[ ] Unknown chunk отличается от known `Unloaded`.

[ ] Topology freeze contract: после freeze запрещённые topology mutations rejected.

## Runtime object identity

[ ] RuntimeObjectId monotonic, не reused.

[ ] Exhaustion rejected без создания record.

[ ] Persistent identity index создаётся atomically вместе с object record.

[ ] Persistent index publication allocation failure откатывает весь create/promotion.

[ ] Duplicate PersistentObjectId rejected.

[ ] Persistent identity нельзя произвольно rename после publication.

[ ] `PlayerTouched` и более высокий persistence tier требует persistent identity.

[ ] Persistence tier downgrade rejected, если contract monotonic.

## Placement/containment/reality

[ ] Все placement variant enums/IDs validated.

[ ] Surface placement требует valid region/chunk relation.

[ ] Owner/container missing rejected.

[ ] Self-container rejected.

[ ] Containment cycle rejected.

[ ] Reality/residency incompatible combination rejected.

[ ] Materialize разрешён только с compatible placement.

[ ] Demote требует collapse confirmation/token semantics по contract.

[ ] Destroyed object terminal; resurrection любой обычной mutation rejected.

[ ] Destroy объекта с dependents rejected либо выполняет явно documented cascade, без неявного orphaning.

## Revision/tokens

[ ] Все mutating commands требуют expected revision там, где API это обещает.

[ ] Revision conflict leaves state unchanged.

[ ] Object revision exhaustion rejected before mutation.

[ ] Demotion token содержит достаточную object/revision identity.

[ ] Token invalidated при object revision change.

[ ] Revoked/forged/stale token rejected.

[ ] Token publication allocation failure не потребляет ID и не меняет object.

[ ] Promotion/index failure не повреждает существующие demotion tokens.

## Queries

[ ] Find/query results detached.

[ ] Query by region/chunk/reality deterministic order.

[ ] Invalid enum domains rejected до mutation.

## Exit 3.8

[ ] `EpidemicRuntimeWorldTests` проходит Debug/Release.

[ ] World = `LOCAL_READY`.

---

# 3.9. Simulation

Обязательные confirmed fixes: `G3-SIM-001`, `G3-SIM-002`.

Фактическая роль: jobs, scheduled tasks, world memory, attention/relevance, abstract facts, proposal queue и commit target boundary.

## Jobs/scheduler

[ ] Submit valid job/metadata.

[ ] Null job/invalid metadata rejected.

[ ] Handle generation stale after terminal removal/reuse.

[ ] Submit/generation exhaustion atomic.

[ ] Pending -> Running -> Continue/Complete/Failed/Cancelled transitions.

[ ] Job `ExecuteStep` Result failure contained.

[ ] Job exception contained.

[ ] Failed step не публикует synthetic Running/Completed state.

[ ] Budget exhaustion leaves job retryable/deferred without duplicate work commit.

[ ] Equal-priority/budget order deterministic.

## Cancellation/scheduling

[ ] `CancelJob` вызывает job cancel boundary по contract.

[ ] Cancel failure/exception не теряет job ownership.

[ ] Associated scheduled task удаляется только в согласованной state transition.

[ ] Scheduled task ID exhaustion.

[ ] Due ordering deterministic.

[ ] Invalid due task не оставляет active phantom schedule.

[ ] Schedule budget exhaustion defers remainder.

## World memory/attention/facts

[ ] World memory ID exhaustion.

[ ] TTL zero/negative/boundary semantics.

[ ] Expiration budget и deterministic order.

[ ] Capacity hard limit и retention semantics.

[ ] Attention finite и `[0,1]`.

[ ] Relevance provider failure/exception contained.

[ ] Abstract fact validation и revision exhaustion.

[ ] Fact/query ordering deterministic.

## Proposal queue/commit

[ ] Publish допускается только из legal job/source state.

[ ] Proposal batch полностью validates до publication.

[ ] Allocation failure публикации не оставляет partial queue entry.

[ ] `CommitNext` external target failure означает no external commit согласно interface contract.

[ ] Commit target exception contained.

[ ] Failed commit оставляет batch retryable и не двигает queue ложным образом.

[ ] Success commit removes exactly one batch.

[ ] Discard semantics explicit и не маскируют committed batch.

[ ] Terminal job удерживается столько, сколько требуется pending proposal ownership.

## Shutdown

[ ] Первый Shutdown запрещает new jobs/schedules/work.

[ ] Pending proposals обрабатываются согласно explicit `ShutdownProposalPolicy`, не удаляются молча.

[ ] Cancel/cleanup best-effort, повторный Shutdown продолжает незавершённое.

[ ] Complete shutdown idempotent.

## Exit 3.9

[ ] `EpidemicRuntimeSimulationTests` проходит Debug/Release.

[ ] Simulation = `LOCAL_READY`.

---

# 3.10. Physics

Обязательный confirmed fix/contract closure: `G3-PHYS-001`.

Фактическая роль: collision shapes, body handles, fixed-step simulation, backend synchronization, contact/raycast data и retryable transform projection.

## Shape/body ownership

[ ] Shape registration input validation и duplicate ID.

[ ] Shape backend creation failure/exception не публикует registry record.

[ ] Shape publication allocation failure уничтожает созданный backend shape либо сохраняет retry cleanup ownership.

[ ] Unregister shape while referenced rejected.

[ ] Body create validates shape, transform, motion parameters и enums до backend call.

[ ] Backend body creation failure/exception не публикует body record.

[ ] Body publication allocation failure выполняет rollback либо durable cleanup.

[ ] Stale body handle rejected.

[ ] Body generation/shape revision/ID exhaustion atomic.

## Simulation

[ ] Fixed step default/configured values.

[ ] Accumulator handles zero/normal/large delta.

[ ] `max_substeps` ограничивает work и сохраняет remainder по contract.

[ ] Frame/step time counters overflow-safe.

[ ] Backend `SimulateFixed` failure/exception не приводит к локальному ложному advancement.

[ ] Backend snapshot batch fully validates before applying any body update.

[ ] Invalid one-body snapshot не оставляет partial writes другим bodies.

[ ] Retry после backend sync failure не resimulates already committed backend step, если contract требует только retry projection/sync.

## Transform boundary

[ ] Transform source failure before simulation controlled.

[ ] Transform sink failure после authoritative backend commit сохраняет pending projection/retry state.

[ ] Retry sink publication не выполняет physical step второй раз.

[ ] Здесь тестируется neutral `IPhysicsTransformSink`, не конкретный Scene queue.

## Contacts/query

[ ] Begin/Persist/End contact semantics explicit.

[ ] Invalid backend contact payload dropped/rejected atomically.

[ ] Duplicate contact publication policy explicit.

[ ] Raycast validates finite origin/direction/max distance.

[ ] Backend raycast hit referencing unknown body rejected.

## Shutdown

[ ] Destroy body failure retains body/handle for retry.

[ ] Destroy shape failure retains shape for retry.

[ ] Shutdown пытается независимые cleanup steps best-effort.

[ ] Shape не уничтожается раньше body, который её использует.

[ ] Repeated Shutdown continues pending cleanup и становится idempotent после completion.

## Exit 3.10

[ ] `EpidemicRuntimePhysicsTests` проходит Debug/Release.

[ ] Physics = `LOCAL_READY`.

---

# 3.11. Navigation

Обязательные confirmed fixes: `G3-NAV-001`, `G3-NAV-002`, `G3-NAV-003`.

Фактическая роль: tile registry, dirty rebuild, handle-based budgeted path queries и external navigation data/backend providers.

## Tiles/revision

[ ] `RegisterTile` invalid/duplicate ID и invalid enum state.

[ ] Tile state transition matrix.

[ ] Repeated `MarkTileDirty` является defined no-op и не создаёт duplicate work.

[ ] Dirty rebuild deterministic order.

[ ] Tile/navigation revision overflow rejected before publication.

[ ] Item/byte budget rebuild semantics documented и tested.

## Path request identity

[ ] Invalid start/target region/coordinates/non-finite values rejected.

[ ] Query handle ID/generation exhaustion.

[ ] Cancel pending/partial query.

[ ] Cancel terminal query semantics.

[ ] Released handle stale.

[ ] TTL-expired record stale.

[ ] Stale generation не видит новый query record.

## Provider/backend boundary

[ ] `INavigationDataSource::CurrentRevision` exception contained.

[ ] Cost provider exception contained.

[ ] Obstacle provider exception contained.

[ ] Backend failure/exception contained.

[ ] Backend `PathResult` validates finite points, legal state, start/target contract и expected source revision before publication.

[ ] Source revision change делает старый query/result stale по contract.

[ ] Partial/deferred/no-path/success states однозначно различаются.

[ ] Equal-order query processing deterministic under budget.

## Scope correction

[ ] Не требовать local Shutdown: такого API нет.

[ ] Environment -> Navigation projection adapter проверяется в 3.17 Support. Локально Navigation проверяет только neutral provider behavior.

## Exit 3.11

[ ] `EpidemicRuntimeNavigationTests` проходит Debug/Release.

[ ] Navigation = `LOCAL_READY`.

---

# 3.12. Animation

Обязательные confirmed fixes: `G3-ANIM-001`, `G3-ANIM-002`. Reference sink contract дополнительно закрывается через `G3-SUP-007`.

Фактическая роль: skeleton/clip registries, animator handles/state, evaluation, pose publication и bounded event buffer.

## Registry/resources

[ ] Skeleton ID/descriptor validation.

[ ] Clip ID/descriptor validation.

[ ] Duplicate skeleton/clip rejected.

[ ] Skeleton/clip compatibility validated.

[ ] Registry Freeze idempotent; registration after freeze rejected.

[ ] `IAnimationResourceSource` failure/exception contained.

[ ] Loaded descriptor полностью validated before registry/animator publication.

## Animator lifecycle

[ ] Create/destroy handle generation semantics.

[ ] ID/generation/revision exhaustion atomic.

[ ] Stale handle rejected for every mutator/query.

[ ] Play/Pause/Stop state matrix.

[ ] Play invalid clip rejected without mutation.

[ ] Crossfade with/without current clip.

[ ] Loop setting isolated per animator; immutable clip metadata не mutates.

[ ] Playback rate finite/valid.

[ ] Fractional playback accumulation deterministic.

[ ] LOD state transition and placeholder pose semantics.

## Evaluation/pose/events

[ ] Evaluator failure/exception не публикует partial pose.

[ ] Pose buffer owner matches animator owner.

[ ] Закрыть `G3-ANIM-001`: pose sink вызывается на полностью staged pose до no-throw authoritative animator/event commit.

[ ] Sink Result failure и exception оставляют animator snapshot, cached pose, event buffer и revision в pre-call state; retry эквивалентен clean Tick.

[ ] Закрыть `G3-ANIM-002`: semantic events не теряются silently на allocation failure; event buffer staging входит в ту же transaction semantics, что animator mutation.

[ ] Event buffer hard bound и overflow policy остаются explicit/deterministic, включая zero configured capacity fallback.

[ ] Play, Stop, Crossfade, loop/finished Tick events проверены на allocation failure до commit и при full buffer.

[ ] Tick processing order deterministic.

[ ] Event Clear semantics не mutates animator state.

## Scope correction

[ ] Не проверять `ResourceLease` здесь. Animation не видит Resources API.

[ ] Реальный resource lease lifetime проверяется в `Resources -> Animation` adapter 3.17.

[ ] Не требовать local Shutdown: public Animation lifecycle такого метода не имеет.

## Exit 3.12

[ ] `EpidemicRuntimeAnimationTests` проходит Debug/Release.

[ ] Animation = `LOCAL_READY`.

---

# 3.13. Audio

Обязательные major-level fixes: `G3-AUDIO-001` и `G3-AUDIO-002`. Resource-lease integration defects закрываются в Support через `G3-SUP-004` и `G3-SUP-005`.

Фактическая роль: sound registry, emitter/listener handles, backend voices, fades, mixer, one-shot event queue и terminal shutdown.

[ ] Закрыть `G3-AUDIO-001`: backend command/retry contract исключает повторное temporal advance и недокументированный commit-on-failure.

[ ] Закрыть `G3-AUDIO-002`: cleanup ownership для newly-created voice preflight-ится до `CreateVoice()` и не может потеряться при failed rollback.

## Sound/emitter/listener identity

[ ] Sound descriptor enums/numerics validated.

[ ] Duplicate sound ID rejected.

[ ] Emitter create input validated before resource/backend effects.

[ ] Emitter ID/generation/revision exhaustion atomic.

[ ] Listener ID/generation exhaustion atomic.

[ ] Stale emitter/listener handles rejected.

[ ] Listener publication failure после backend create выполняет rollback либо сохраняет pending cleanup ownership.

[ ] Main listener switch atomic; failed selection не теряет старый main listener.

## Playback/fade/virtualization

[ ] Play/Pause/Resume/Stop transition matrix.

[ ] Backend CreateVoice/Play/Pause/Stop/SetGain/SetSpatial exception/failure contained.

[ ] Play failure не оставляет phantom Playing state.

[ ] Fade zero duration, normal duration, completion.

[ ] Fade pause/resume continuation.

[ ] Failed fade/backend mutation preserves local state или explicit reconciliation.

[ ] Virtualize destroys backend voice exactly once и keeps semantic emitter alive.

[ ] Replay virtualized emitter follows documented restart behavior.

[ ] One-shot completion and cleanup.

[ ] Tick failure не advances local fade/playback state falsely.

## Mixer/events/resources

[ ] Mixer group ID/enum/gain validation.

[ ] Mixer parent cycle rejected.

[ ] Effective gain propagation deterministic.

[ ] Event queue bound и overflow policy explicit.

[ ] Resource source failure/exception contained.

[ ] Stream read contract validated.

[ ] Реальный ResourceLease ownership wrapper проверяется в Support 3.17.

## Shutdown

[ ] First shutdown terminal: new work rejected.

[ ] Voice/listener cleanup best-effort.

[ ] Failed cleanup retains handles for retry.

[ ] Successful cleanup removes ownership exactly once.

[ ] Repeated shutdown continues unfinished work и idempotent after complete.

## Exit 3.13

[ ] `EpidemicRuntimeAudioTests` проходит Debug/Release.

[ ] Audio = `LOCAL_READY`.

---

# 3.14. Environment

Фактическая роль: per-region weather/season/climate, surface ownership/state, revisioned detached snapshots/projections и optional update policy.

## Registration

[ ] Region registration requires complete valid weather/season/climate state.

[ ] Partial initialization rejected before mutation.

[ ] Duplicate region rejected.

[ ] Allocation failure publication preserves revision and region map.

[ ] Registration freeze idempotent; new regions/surfaces rejected after freeze as defined.

[ ] Enum domains validated.

## Region/surface ownership

[ ] Per-region revision conflicts isolated: change region A не создаёт false conflict в B.

[ ] Existing `SurfaceId` нельзя move в другой region.

[ ] Duplicate surface ownership in one batch rejected transactionally.

[ ] Unknown region/surface query controlled.

[ ] Weather/climate/season/surface numeric values finite и within contract.

[ ] Wind direction normalization/validation deterministic.

## Updates

[ ] Empty update no-op без revision bump.

[ ] Setter same value no-op без revision bump.

[ ] Batch validates полностью до commit.

[ ] Invalid one entry leaves all regions/surfaces unchanged.

[ ] Update policy Result failure contained.

[ ] Update policy exception contained.

[ ] Policy output ownership/enum/numeric validation перед commit.

[ ] Revision exhaustion rejected before mutation.

## Snapshot/projection

[ ] Snapshot detached, complete и revisioned.

[ ] Projection consistent with authoritative region/surface state.

[ ] Snapshot/order deterministic where collections observable.

[ ] Local restore не требовать, пока public API declares Environment state as runtime-owned/transient. Persistence role решить в Goal 5 map без скрытого обещания `EnvironmentSnapshot` как restore format.

## Exit 3.14

[ ] `EpidemicRuntimeEnvironmentTests` проходит Debug/Release.

[ ] Environment = `LOCAL_READY`.

---

# 3.15. Renderer

Обязательный confirmed fix: `G3-REN-001`.

Фактическая роль: render proxy/view state, resource bridge ownership, transform/pose ingestion, deterministic submissions и terminal frame/shutdown lifecycle.

## Factory/dependencies

[ ] Strict/production profile требует command sink/resource bridge/scene source согласно actual factory contract.

[ ] Mock/reference profile явно отделён от production behavior.

[ ] Dependency null/missing/throwing cases controlled.

## Proxy/view identity

[ ] Proxy descriptor validation, including finite transforms/visibility/layer/resource IDs.

[ ] Proxy ID exhaustion atomic.

[ ] Proxy publication allocation failure не acquires resources и не consumes ID.

[ ] Register/update/remove lifecycle.

[ ] Deferred destroy retains resource ownership until successful release.

[ ] Failed release preserves exact pending cleanup prefix for retry.

[ ] View create/update/remove/main-view lifecycle.

[ ] Main view invalid/missing behavior explicit.

[ ] Camera numeric inputs finite/valid.

## Resource/scene/pose boundaries

[ ] Resource bridge acquire failure/exception не публикует Ready proxy falsely.

[ ] Permanent resource failure maps to explicit failed state.

[ ] Missing/not-ready resource skips/defer semantics.

[ ] Scene transform source failure/exception controlled.

[ ] Animation pose source failure/exception controlled.

[ ] `PrepareFrame` stages all fallible transform/pose reads before publishing staged frame state.

[ ] Failure одного staged read не leaves partial transformed proxy state.

## Frame lifecycle

[ ] Legal order `PrepareFrame -> RenderFrame`.

[ ] Render without prepared frame rejected.

[ ] Repeated prepare/render invalid transitions controlled.

[ ] Submission order deterministic by documented layer/proxy key.

[ ] Command sink Begin/Submit/End failures/exceptions have explicit abort semantics.

[ ] Submit failure не leaves renderer in sticky corrupt state unless explicit terminal failure contract says so.

[ ] Dirty flags clear only after successful applicable frame commit.

## Shutdown

[ ] First Shutdown enters terminal lifecycle and new render work rejected.

[ ] All proxies/resource holds cleanup best-effort.

[ ] Failed releases remain retryable.

[ ] Repeated Shutdown completes remaining cleanup.

[ ] No resize/recreate checklist here: Renderer API такого contract не имеет.

## Exit 3.15

[ ] `EpidemicRuntimeRendererTests` проходит Debug/Release.

[ ] Renderer = `LOCAL_READY`.

---

# 3.16. Persistence

Обязательный confirmed fix: `G3-PERS-001`.

Фактическая роль: atomic snapshot transactions over persistent objects, tombstones, lazy rules, zone overrides и backend durability.

## Transaction lifecycle

[ ] Begin transaction captures base revision/version required for conflict detection.

[ ] Staged operations ordered deterministically.

[ ] Operation allocation failure не добавляет partial staged operation.

[ ] Rollback discards staged operations и terminal state explicit.

[ ] Commit success terminal.

[ ] Failed commit terminal/retry semantics explicit.

[ ] Repeated Commit/Rollback after terminal state follows fixed contract.

[ ] Concurrent transaction conflict rejected without publishing stale candidate.

[ ] Empty transaction semantics defined.

## Candidate validation

[ ] Upsert record full ID/location/kind/protection/payload/revision validation.

[ ] Delete active record atomically removes active state, creates tombstone and removes associated lazy rules according to contract.

[ ] Active record и tombstone cannot coexist.

[ ] Duplicate IDs inside candidate snapshot rejected.

[ ] Lazy rule target must exist unless specific administrative restore contract permits otherwise.

[ ] `UpdateLazyRule` missing ID rejected, not upsert.

[ ] Zone override contents validated and detached.

[ ] Persisted enum/protection mask domains validated.

[ ] Revision overflow rejected before backend call/publication.

[ ] Candidate build allocation failure leaves live store and backend untouched.

## Backend atomicity

[ ] Backend load invalid snapshot rejected before live publication.

[ ] Backend `CommitSnapshot` Result failure leaves previous durable snapshot и previous live state authoritative.

[ ] Backend exception contained.

[ ] `PersistenceDurability::MemoryOnly`, `SaveRequired` и `SaveAndFlushRequired` передаются в существующий `CommitSnapshot(snapshot, durability)` contract без изобретения отдельных `Save()`/`Flush()` APIs.

[ ] Для non-memory durability live store публикует candidate только после successful backend `CommitSnapshot`.

[ ] Atomic replacement semantics proven with backend fake that observes old/new snapshot и requested durability.

## Queries/snapshot

[ ] Missing record/lazy rule/tombstone/override behavior explicit.

[ ] Returned records/payloads detached from mutable internal storage.

[ ] Snapshot deterministic order.

[ ] Rich tombstones and zone overrides roundtrip through backend snapshot.

[ ] Administrative transaction APIs remain separated from ordinary save mutation permissions.

## Exit 3.16

[ ] `EpidemicRuntimePersistenceTests` проходит Debug/Release.

[ ] Persistence = `LOCAL_READY`.

---

# 3.17. Support

Обязательные confirmed fixes: `G3-SUP-001` ... `G3-SUP-007`.

Support является самым большим локальным Runtime audit unit и единственным стандартным cross-major composition layer. Его нельзя закрывать одним smoke test.

Разбить audit на отдельные sub-gates.

## 3.17.A. Preparation и aggregate commit

[ ] `PrepareEngineRuntime()` выполняет полный preflight dependencies/options до публикации в `Application`.

[ ] Failed prepare не меняет ServiceContainer/Application/frame handlers.

[ ] Все созданные majors/adapters принадлежат только `PreparedEngineRuntime` до commit.

[ ] `CommitPreparedRuntime()` регистрирует aggregate `EngineRuntimeServices` атомарно.

[ ] Duplicate aggregate registration controlled и pre-existing services не повреждаются.

[ ] `RegisterDefaultEngineRuntime()` не реализован как последовательность `RegisterXxx` с partial root mutation.

[ ] Allocation failure в final aggregate publication оставляет Application pre-state.

[ ] Каждый public `RegisterXxx` helper атомарен относительно полного service bundle, который он публикует: failure оставляет `Application`/`ServiceContainer` в pre-call state. Bootstrap-only status не используется как исключение из atomicity.

## 3.17.B. Profiles/dependency ownership

[ ] Reference profile создаёт только разрешённые reference backends.

[ ] Production profile preflight требует renderer command sink, physics backend, navigation backend, animation evaluator, audio backend, simulation commit target и обязательный streaming manifest source согласно current contract.

[ ] Missing production role rejected до создания/commit composition root.

[ ] Partial override composite roles rejected.

[ ] Один adapter object используется для всех объявленных interface roles, без hidden duplicate state owners.

[ ] `EngineRuntimeServices.registered_majors` точно соответствует фактически созданным majors и не используется как второй owner state.

## 3.17.C. Scene -> Renderer

[ ] Scene source adapter возвращает detached/valid transform data.

[ ] Missing scene node/resource maps to controlled renderer behavior.

[ ] Adapter exception boundary contained.

[ ] Нет обратной mutation Renderer -> Scene через этот adapter.

## 3.17.D. Resources -> Renderer

[ ] Render resource bridge получает отдельный `ResourceLease` на ownership proxy.

[ ] Publication failure после lease acquisition releases lease либо сохраняет retry ownership.

[ ] Proxy destroy releases exactly its own lease.

[ ] Failed release retry не double-releases completed prefix.

[ ] Mesh/material payload type mapping validates concrete payload kind before exposure.

## 3.17.E. Scene -> Physics

[ ] Physics reads source transforms через adapter.

[ ] Physics writes не mutates Scene немедленно: они staging-ятся в `ISceneProjectionQueue`.

[ ] Queue publication failure preserves physics retry semantics and Scene pre-state.

[ ] `Flush()` applies projections deterministic order.

[ ] Failed Scene write does not drop uncommitted queue entry.

[ ] Retry does not reapply already committed entries.

[ ] `DiscardPending()` semantics explicit.

## 3.17.F. Scene -> Audio

[ ] Audio transform source validates missing/stale scene transform.

[ ] Failed transform read does not partially update emitter/listener state.

[ ] No Scene ownership retained beyond documented detached transform result.

## 3.17.G. Resources -> Animation

[ ] Skeleton/clip mapping creates ResourceRequest with correct ResourceId/ResourceType.

[ ] Adapter owns lease only during load/copy lifetime.

[ ] Ready payload descriptor copied/detached before lease release.

[ ] Failure before copy releases lease.

[ ] Failure releasing lease preserves explicit cleanup ownership if release can fail.

[ ] Wrong payload type rejected.

## 3.17.H. Resources -> Audio

[ ] Закрыть `G3-SUP-004` и `G3-SUP-005`: lease acquisition/transfer/release не имеет allocation gap и destructor cleanup не теряет retry ownership.

[ ] Audio resource mapping validates ResourceId/type.

[ ] `IAudioClipResource` wrapper pins ResourceLease for exactly the clip/voice consumer lifetime.

[ ] Wrapper destruction/explicit cleanup releases exactly once.

[ ] Failed release remains retryable through owning adapter/lifecycle state, not silently lost.

[ ] Wrong payload/stream type rejected.

## 3.17.I. Animation -> Renderer

[ ] Закрыть `G3-SUP-007`: pose publication обновляет owner/animator indexes одной atomic transaction.

[ ] Pose cache key = `RuntimeObjectId owner`, not animator pointer/address.

[ ] Pose publication replaces/updates owner state atomically.

[ ] Renderer receives neutral `RenderPoseBuffer`, not Animation concrete types.

[ ] Missing/stale pose does not corrupt static proxy submission.

[ ] Pose cache cleanup bounded by animator/object lifecycle.

## 3.17.J. World + Resources + Persistence -> Streaming

[ ] Manifest source validation.

[ ] Standard adapter transitions only chunk state it actually owns.

[ ] `Unloaded -> Loading` occurs before resource/persistence preparation according to contract.

[ ] Persistence override read is detached.

[ ] Resource leases remain prepared, not active, until streaming commit.

[ ] Commit publishes prepared data/leases/chunk state exactly once.

[ ] Rollback only reverses adapter-owned prefix.

[ ] Unload order: `Unloading` -> lease release -> final `Unloaded`.

[ ] Failed lease release keeps ownership and blocks final Unloaded.

[ ] Failed World transition cleanup retained for retry.

[ ] Successor request cannot observe stale prepared persistence from predecessor.

[ ] `IStreamingPreparedChunkDataQuery` lifetime exactly tied to prepared/resident request.

## 3.17.K. Environment -> Navigation

[ ] Environment projection mapping validates region/revision.

[ ] Navigation sees consistent complete projection, not partial environment batch.

[ ] Adapter failure does not mutate either owner inconsistently.

[ ] No duplicate authoritative environment state stored in Navigation adapter beyond required checkpoint/mapping.

## 3.17.L. Time -> Simulation

[ ] Simulation clock adapter derives time only from authoritative Runtime Time.

[ ] No wall clock enters Simulation deterministic state.

[ ] One Runtime frame publishes one intended game delta/time observation.

[ ] Pause/time scale/skip mapping semantics explicit.

## 3.17.M. Coordinator Tick

Frozen update order:

```text
Time
MainThreadCommits
Resources
Streaming
Simulation
Navigation
Animation
Physics
SceneProjectionCommit
Audio
Renderer
DiagnosticsEvents
```

Проверить:

[ ] Exact order returned by `GetRuntimeUpdateOrder()` и observed execution совпадают.

[ ] `RuntimeTickResult.executed_steps` содержит только реально начатые/выполненные phases по contract.

[ ] Invalid frame input rejected before mutation.

[ ] Time failure semantics: определить, какие downstream phases могут выполняться без valid new game delta.

[ ] Recoverable failure одного independent major записывается в failures и не блокирует независимые следующие phases.

[ ] Dependency-sensitive phase не выполняется на заведомо invalid prerequisite state.

[ ] MainThreadCommits budget limits proposal commits.

[ ] SceneProjectionCommit расположен после Physics и до Audio/Renderer.

[ ] Diagnostics event batch detached.

[ ] Source event buffers очищаются только после успешного event sink publication.

[ ] Event sink failure/exception сохраняет source events/checkpoint для retry и не публикует false success.

[ ] Allocation failure при построении `RuntimeTickResult`, failures/events/executed_steps не оставляет coordinator/internal buffers в logically committed half-state. Если operation допускает prefix-progress, это должно быть явно отражено.

[ ] Tick после shutdown start rejected.

## 3.17.N. Coordinator Shutdown

[ ] Первый Shutdown переводит coordinator в terminal `shutdown started` до приёма следующего Tick.

[ ] Cleanup order точно совпадает с `GetRuntimeShutdownOrder()`.

[ ] Независимые cleanup steps продолжаются после первой ошибки.

[ ] Возвращается первая ошибка, diagnostic context не теряется.

[ ] Failed adapter/major cleanup сохраняет ownership/handle/lease для retry.

[ ] Повторный Shutdown пропускает completed steps и продолжает unfinished.

[ ] Simulation proposals завершаются согласно `ShutdownProposalPolicy`.

[ ] `IsShutdownComplete()` true только после полного owned cleanup.

[ ] Повторный Shutdown после complete идемпотентен.

## Exit 3.17

[ ] Каждый adapter имеет isolated fake-based tests. Реальные соседние majors не используются для маскировки локального adapter defect там, где достаточно fake.

[ ] Reference composition smoke.

[ ] Production preflight negative smoke.

[ ] Full tick с injected phase failures.

[ ] Full shutdown с injected cleanup failures и retry.

[ ] `EpidemicRuntimeSupportTests` проходит Debug/Release.

[ ] Support = `LOCAL_READY`.

---

# 3.18. Runtime-wide integration и regression gate

После получения `LOCAL_READY` каждым major выполнить отдельный Runtime gate. Этот gate не заменяет module audits.

## 3.18.1. Architecture

[ ] `EpidemicRuntimeArchitectureTests` проходит.

[ ] Runtime не зависит от Framework.

[ ] Majors не получили новые peer dependencies вместо Support adapters.

[ ] RuntimeFoundation остаётся neutral и не знает majors.

[ ] External SDK/platform includes не появились вне разрешённых Base boundaries.

## 3.18.2. Integration

[ ] Default Reference composition boot.

[ ] Production dependency preflight.

[ ] Time -> Simulation.

[ ] World/Resources/Persistence -> Streaming.

[ ] Scene -> Physics -> Scene projection.

[ ] Scene/Resources/Animation -> Renderer.

[ ] Scene/Resources -> Audio.

[ ] Environment -> Navigation.

[ ] Full coordinator tick order.

[ ] Recoverable one-phase failure не ломает independent later phases.

[ ] Terminal shutdown + retry cleanup.

## 3.18.3. Regression

[ ] Каждый defect, найденный в 3.1–3.17, имеет permanent regression.

[ ] Runtime regression executable содержит только cross-module/runtime regressions; локальные regressions остаются также в module suite, если defect module-local.

[ ] Никакой test не отключён/skip без explicit freeze exception.

---

# 3.19. Финальный exit criteria EngineRuntime

Goal 3 закрывается только одновременно при выполнении следующего.

## Module status

[ ] `17/17 EngineRuntime production modules = LOCAL_READY`:

```text
RuntimeFoundation
Time
Serialization
Resources
Assets
Streaming
Scene
World
Simulation
Physics
Navigation
Animation
Audio
Environment
Renderer
Persistence
Support
```

## Evidence

[ ] `local_ready_ledger.json` содержит PASS/N/A evidence для всех применимых 37 Goal 1 criteria по каждому из 17 modules.

[ ] Все Runtime public callables имеют актуальную classification и anchors.

[ ] Runtime dossiers имеют reviewed responsibility/state/lifecycle/persistence/threading notes без известных ложных auto-discovery claims.

[ ] `G3-DOC-001` закрыт: Runtime module docs не объявляют статус `frozen` в обход ledger; после module exit статус согласован с `LOCAL_READY`.

[ ] Все найденные Runtime defects имеют regression tests.

[ ] `UNCLASSIFIED = 0` сохраняется после API изменений.

## Build/test matrix

После всех Runtime fixes обязательно повторно прогнать:

[ ] Base Debug.

[ ] Base Release.

[ ] Runtime Debug с Framework OFF.

[ ] Runtime Release с Framework OFF.

[ ] Full Debug.

[ ] Full Release.

[ ] Public-header consumers.

[ ] Architecture/freeze validators.

[ ] Validator negative/self-tests.

[ ] Runtime module suites 17/17.

[ ] Runtime Architecture suite.

[ ] Runtime Integration suite.

[ ] Runtime Regression suite.

[ ] Runtime Support Reference composition smoke.

[ ] Production profile negative/preflight smoke.

Если число CTest cases изменилось, обновить configured manifests и зафиксировать новый baseline. Нельзя сохранять admission baseline `34/34` или `94/94` как ожидаемое число после добавления новых test cases без регенерации manifest.

## Publication

[ ] `git diff --check`.

[ ] Warnings-as-errors на engine-owned targets.

[ ] Commit Runtime Goal 3 baseline.

[ ] Remote CI зелёный на recorded SHA.

После этого:

```text
EngineBase    = 9/9 LOCAL_READY
EngineRuntime = 17/17 LOCAL_READY
Goal 3        = COMPLETE
```

Системный статус `SYSTEM_READY/FROZEN` здесь не присваивается. Persistence whole-engine, determinism, concurrency/lifetime, semantic clusters, load и final freeze остаются целями 5–9.

---

# Рекомендуемый порядок выполнения Goal 3

Порядок ниже уменьшает количество повторных переделок, сохраняя номера исходного плана.

```text
1.  3.1  RuntimeFoundation
2.  3.3  Serialization
3.  3.5  Assets
4.  3.2  Time
5.  3.7  Scene
6.  3.8  World
7.  3.14 Environment
8.  3.16 Persistence
9.  3.4  Resources
10. 3.6  Streaming
11. 3.9  Simulation
12. 3.11 Navigation
13. 3.12 Animation
14. 3.10 Physics
15. 3.13 Audio
16. 3.15 Renderer
17. 3.17 Support
18. 3.18 Runtime-wide gate
19. 3.19 Final exit
```

Причины порядка технические:

- Foundation фиксирует общие value contracts до остальных majors.
- Serialization/Assets/Time/Scene/World/Environment/Persistence имеют минимальное число cross-major dependencies и формируют contracts, которые используют adapters.
- Resources должен быть готов до Streaming и resource adapters.
- Simulation/Navigation/Animation/Physics/Audio/Renderer закрываются до Support, чтобы Support проверял уже стабильные ports.
- Support закрывается последним, потому что он соединяет почти весь Runtime и не должен использоваться как способ скрыть локальный defect major.

---

# Что сознательно не входит в Goal 3

Чтобы Goal 3 не расползался в последующие этапы:

- whole-engine ordered restore выполняется в Goal 5;
- repeated deterministic whole-engine replay выполняется в Goal 5;
- sanitizer/race/lifetime qualification выполняется в Goal 6;
- реальные Base -> Runtime -> Framework causal chains выполняются в Goal 7;
- полная A/B/C classification всей mutation surface и broad fault campaign выполняется в Goal 8;
- high-cardinality/load/degradation qualification выполняется в Goal 8;
- repeated flakiness matrix и окончательный `FROZEN` выполняются в Goal 9.

При этом локальная failure atomicity, stale identity, revision/generation exhaustion, external callback failure и retry cleanup нельзя откладывать из Goal 3, потому что они уже входят в общий критерий `LOCAL_READY` Goal 1.
