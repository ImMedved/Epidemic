# Support

## Назначение

`EngineRuntime/Support` — официальный composition layer Runtime. Он создает полный набор service bundles, соединяет независимые majors через typed adapters и предоставляет `IEngineRuntimeCoordinator`. Support не является еще одним major и не хранит собственную копию World, Scene, Resources или Simulation state.

## Подготовка и регистрация

Основной вход — `PrepareEngineRuntime(options, dependencies)`. Функция проверяет зависимости, создает majors, adapters и coordinator во временном `PreparedEngineRuntime`. На этом этапе `Application` не меняется. После успешной подготовки `CommitPreparedRuntime()` атомарно регистрирует один aggregate `EngineRuntimeServices`.

`RegisterDefaultEngineRuntime()` объединяет эти два шага. Для инструментов и изолированных тестов отдельные majors по-прежнему можно создавать напрямую через их factories.

`EngineRuntimeServices` владеет bundles всех включенных majors, `RuntimeIntegrationServices` и coordinator. Один adapter instance передается всем своим interface roles и одновременно удерживается integration aggregate, поэтому wiring не имеет скрытых дубликатов.

## Profiles и dependencies

`RuntimeProfile::Reference` создает reference implementations тех backend ports, которые позволяют поднять полный нейтральный runtime для разработки и тестов. `RuntimeProfile::Production` требует внешние production implementations там, где Runtime не должен подменять настоящий backend: renderer command sink, physics backend, navigation backend, animation evaluator, audio backend и simulation commit target.

Streaming является исключением только в смысле wiring: стандартная связь `World + Resources + Persistence → Streaming` принадлежит Support и используется в обоих profiles. В Production для нее необходимо передать настоящий `IChunkStreamingManifestSource`. Application может полностью заменить Streaming integration, но core roles передаются только полным комплектом; partial override отклоняется. То же правило применяется к составным стандартным projections: Navigation `data_source + cost_provider + obstacle_source` и Physics `transform_source + transform_sink` заменяются только целиком. Это исключает смешивание внешней и reference ownership внутри одного logical adapter.

## Основные adapters

Support соединяет Scene с Renderer, Resources с Renderer, Scene с Physics и Audio, Resources с Animation и Audio, Animation с Renderer, World/Resources/Persistence со Streaming, Environment с Navigation и Time с Simulation.

Physics world-transform writes сначала попадают в `ISceneProjectionQueue` и применяются в отдельной `SceneProjectionCommit` phase. `Flush()` сортирует writes deterministically. После каждого successful Scene write ownership этой записи считается переданным окончательно; late failure или exception сохраняет для retry только неуспешный suffix, поэтому уже принятый prefix не применяется повторно.

Animation публикует pose с `RuntimeObjectId owner`; pose bridge преобразует его в нейтральный `renderer::RenderPoseBuffer`, который Renderer запрашивает по owner. Два pose indexes, по animator и owner, готовятся как единая candidate transaction и публикуются одновременно. Ошибка подготовки не меняет ни один index. Static proxy может не иметь pose.

Standard Streaming adapter получает chunk manifest, переводит принадлежащий ему chunk `Unloaded → Loading`, читает detached Persistence override, постепенно получает Resource leases и после успешной подготовки commit-ит `Resident/Active`. `IStreamingPreparedChunkDataQuery` предоставляет подготовленный persistence snapshot следующему слою, пока chunk остается загруженным. При unload adapter сначала переводит chunk в `Unloading`, затем освобождает active leases и только после этого завершает `Unloaded`. Failed cleanup сохраняет ownership для retry.

Animation resource adapter сначала подготавливает local ownership slot и только затем запрашивает `ResourceLease`. После копирования готового skeleton/clip descriptor lease освобождается; failed release сохраняется в adapter ownership до lifecycle cleanup. Между successful acquisition и публикацией owner нет allocation gap. Exceptions ResourceManager не пересекают adapter boundary.

Audio использует тот же pre-acquire ownership rule, затем передает lease в `IAudioClipResource` wrapper. Durable lease entry создается до relinquish source owner. Destructor wrapper не аллоцирует и не бросает: он только помечает уже существующий entry для release и выполняет contained best-effort cleanup. Failed release остается у lease state и повторяется через `Shutdown()`. Sound resource поэтому остается pinned ровно пока живет реальный clip/voice consumer или незавершенный cleanup owner.

## Coordinator Tick

Coordinator использует следующий порядок:

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

В `RuntimeTickResult.executed_steps` записываются реально выполненные phases. Recoverable ошибка major попадает в `RuntimeTickResult.failures`, но не останавливает независимые последующие phases. Simulation proposal commits ограничиваются per-frame budget. Game delta берется только из результата Time.

`DiagnosticsEvents` собирает frame events и phase failures в detached batch. `IRuntimeEventSink` имеет atomic publication contract: Result failure или exception означает, что batch не был externally committed. Reference sink сначала полностью строит candidate batch и только затем публикует его no-fail swap. Source buffers очищаются только после confirmed success.

Coordinator хранит private pending-frame state. После принятия phase её successful result и phase cursor получают durable ownership до перехода к следующей phase. Если поздняя Runtime-local allocation прерывает Tick, повтор с тем же `RuntimeFrameInput` продолжает незавершенный suffix и не повторяет уже accepted phases; другой input до reconciliation отклоняется с `runtime_support.frame_retry_required`. В частности, successful `Time::Advance`, Streaming и Simulation results сохраняются до fallible result materialization.

## Shutdown

Первый вызов coordinator shutdown переводит Runtime в terminal lifecycle: новые кадры больше не принимаются. Cleanup выполняется best-effort по всем системам и adapters. Первая ошибка возвращается вызывающему коду, но независимые cleanup steps продолжаются. Completion каждого shutdown step и каждого составного adapter cleanup хранится durably. Повторный `Shutdown()` выполняет только незавершенные steps и не повторяет уже confirmed cleanup prefix. Объекты, handles и leases, которые не удалось освободить, не забываются. `IsShutdownComplete()` становится true только после полного освобождения owned Runtime state.

Simulation proposals обрабатываются согласно `ShutdownProposalPolicy`; они не удаляются молча. После успешного shutdown повторный вызов идемпотентен.

## Стабильность

Support является единственным местом для стандартного технического cross-major wiring и после Goal 3/canonical evidence convergence имеет статус `LOCAL_READY`; это не означает системный `FROZEN` до Goals 5–9. Gameplay-связи сюда не добавляются. Новая связь между majors должна появляться в Support только тогда, когда она выражает нейтральную инфраструктуру; связь с игровым смыслом принадлежит `GameFramework`.
