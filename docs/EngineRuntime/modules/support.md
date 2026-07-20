# Support

## Статус

Этот документ предварительный. Runtime Support еще не считается frozen и должен быть переписан после завершения atomic composition, реальных adapters, coordinator Tick и retryable Shutdown.

## Назначение

Support должен быть единственным официальным способом собрать полноценный EngineRuntime для приложения. Он создает service bundles, соединяет majors через typed adapters и предоставляет `IEngineRuntimeCoordinator`.

Support не является новым major и не хранит собственную копию World, Resources или Simulation. Его state ограничивается владением adapters, очередями межмодульных projections и состоянием lifecycle композиции.

## Ожидаемая модель

Готовый API должен принимать `EngineRuntimeOptions` и `EngineRuntimeDependencies`, сначала построить `PreparedEngineRuntime` без изменения `Application`, затем атомарно зарегистрировать один `EngineRuntimeServices`.

`EngineRuntimeServices` владеет всеми bundles, typed `RuntimeIntegrationServices` и coordinator. Один и тот же adapter instance хранится aggregate и передается consumer factory.

Coordinator выполняет реальный порядок Time, commits, Resources, Streaming, Simulation, Navigation, Animation, Physics, Scene projection, Audio, Renderer и diagnostics. Shutdown запрещает новую работу, закрывает все majors best-effort, сохраняет failed ownership для retry и становится complete только после полного освобождения.

## Документирование

После реализации этот файл должен перечислить точные dependencies, adapters, update phases, shutdown phases, reference/production profiles и end-to-end guarantees. До этого его нельзя использовать как описание готового API.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `runtime_support.h`: `RuntimeFoundationRegistration`, `RuntimeProfile`, `RuntimeAdapterKind`, `RuntimeUpdateStep`, `RuntimeShutdownStep`, `EngineRuntimeOptions`, `RuntimeFrameInput`, `RuntimeIntegrationServices`, `IEngineRuntimeCoordinator`, `EngineRuntimeServices`.
