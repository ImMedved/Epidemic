# Support

## Назначение

`EngineRuntime/Support` — официальный composition layer Runtime. Он создает полный набор service bundles, соединяет независимые majors через typed adapters и предоставляет `IEngineRuntimeCoordinator`. Support не является еще одним major и не хранит собственную копию World, Scene, Resources или Simulation state.

## Подготовка и регистрация

Основной вход — `PrepareEngineRuntime(options, dependencies)`. Функция проверяет зависимости, создает majors, adapters и coordinator во временном `PreparedEngineRuntime`. На этом этапе `Application` не меняется. После успешной подготовки `CommitPreparedRuntime()` атомарно регистрирует один aggregate `EngineRuntimeServices`.

`RegisterDefaultEngineRuntime()` объединяет эти два шага. Для инструментов и изолированных тестов отдельные majors по-прежнему можно создавать напрямую через их factories.

`EngineRuntimeServices` владеет bundles всех включенных majors, `RuntimeIntegrationServices` и coordinator. Один adapter instance передается всем своим interface roles и одновременно удерживается integration aggregate, поэтому wiring не имеет скрытых дубликатов.

## Profiles и dependencies

`RuntimeProfile::Reference` создает reference implementations тех backend ports, которые позволяют поднять полный нейтральный runtime для разработки и тестов. `RuntimeProfile::Production` требует внешние production implementations там, где Runtime не должен подменять настоящий backend: renderer command sink, physics backend, navigation backend, animation evaluator, audio backend и simulation commit target.

Streaming является исключением только в смысле wiring: стандартная связь `World + Resources + Persistence → Streaming` принадлежит Support и используется в обоих profiles. В Production для нее необходимо передать настоящий `IChunkStreamingManifestSource`. Application может полностью заменить Streaming integration, но core roles передаются только полным комплектом; partial override отклоняется.

## Основные adapters

Support соединяет Scene с Renderer, Resources с Renderer, Scene с Physics и Audio, Resources с Animation и Audio, Animation с Renderer, World/Resources/Persistence со Streaming, Environment с Navigation и Time с Simulation.

Physics world-transform writes сначала попадают в `ISceneProjectionQueue` и применяются в отдельной `SceneProjectionCommit` phase. Animation публикует pose с `RuntimeObjectId owner`; pose bridge преобразует его в нейтральный `renderer::RenderPoseBuffer`, который Renderer запрашивает по owner. Static proxy может не иметь pose.

Standard Streaming adapter получает chunk manifest, переводит принадлежащий ему chunk `Unloaded → Loading`, читает detached Persistence override, постепенно получает Resource leases и после успешной подготовки commit-ит `Resident/Active`. `IStreamingPreparedChunkDataQuery` предоставляет подготовленный persistence snapshot следующему слою, пока chunk остается загруженным. При unload adapter сначала переводит chunk в `Unloading`, затем освобождает active leases и только после этого завершает `Unloaded`. Failed cleanup сохраняет ownership для retry.

Animation resource adapter удерживает ResourceLease только пока skeleton/clip загружается; после копирования готового descriptor lease освобождается. Audio передает lease в `IAudioClipResource` wrapper, поэтому sound resource остается pinned ровно пока живет реальный clip/voice consumer.

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

`DiagnosticsEvents` собирает frame events и phase failures в detached batch. Source buffers очищаются только после успешной публикации event sink.

## Shutdown

Первый вызов coordinator shutdown переводит Runtime в terminal lifecycle: новые кадры больше не принимаются. Cleanup выполняется best-effort по всем системам и adapters. Первая ошибка возвращается вызывающему коду, но независимые cleanup steps продолжаются. Объекты, handles и leases, которые не удалось освободить, не забываются. Повторный `Shutdown()` продолжает незавершенный cleanup. `IsShutdownComplete()` становится true только после полного освобождения owned Runtime state.

Simulation proposals обрабатываются согласно `ShutdownProposalPolicy`; они не удаляются молча. После успешного shutdown повторный вызов идемпотентен.

## Стабильность

Support является единственным местом для стандартного технического cross-major wiring. Gameplay-связи сюда не добавляются. Новая связь между majors должна появляться в Support только тогда, когда она выражает нейтральную инфраструктуру; связь с игровым смыслом принадлежит `GameFramework`.
