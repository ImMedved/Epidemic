# Core

## Назначение

Core — microkernel EngineBase. Он определяет жизненный цикл приложения, контейнер сервисов, реестр модулей, frame phases, event bus, worker scheduler и возврат результатов на main thread. Core координирует работу, но не знает о Windows, input, graphics, world или gameplay.

## Application

`Application` проходит состояния Constructed, Bootstrapped, Initialized, Running и ShutDown. `Bootstrap()` дает модулям возможность зарегистрировать сервисы. `Initialize()` проверяет зависимости и закрывает `ServiceContainer`. `Tick()` выполняет один кадр, `Run()` повторяет его до stop request, а `Shutdown()` завершает модули.

Frame handlers регистрируются до запуска и исполняются в фиксированных `FramePhase`. `FrameContext` содержит индекс и время кадра, но не хранит данные верхних systems.

## Сервисы и модули

`ServiceContainer` хранит long-lived services по interface type. Duplicate registration считается ошибкой composition root. `ModuleRegistry` упорядочивает `IModule` по `ModuleManifest`, проверяет отсутствующие и циклические зависимости и вызывает lifecycle methods.

`IEventBus` поддерживает синхронную доставку и queued events. Он подходит для небольших notifications, но не заменяет authoritative command API. `ITaskScheduler` выполняет CPU tasks и позволяет ждать handle или group. `IMainThreadDispatcher` переносит commit обратно в основной поток.

## Инварианты

Core не зависит от Platform, Input, RHI или EngineRuntime. Background tasks не меняют main-thread-only services напрямую. Service container не открывается повторно после Initialize. Event handlers и frame callbacks не должны хранить borrowed references дольше вызова.

## Ограничения и стабильность

Core не является ECS, renderer loop или gameplay framework. Его lifecycle, service lookup и task contracts заморожены. Upper layer подключается через composition root и frame handler, не модифицируя Core.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `application.h`: `ILogger`, `ApplicationOptions`, `Application`.
- `basic_configuration.h`: `BasicConfiguration`.
- `configuration.h`: `IConfiguration`.
- `event_bus.h`: `EventDispatchMode`, `IEventBus`, `EventBus`.
- `frame_context.h`: `FrameContext`.
- `frame_phase.h`: `FramePhase`.
- `imodule.h`: `IModule`.
- `main_thread_dispatcher.h`: `IMainThreadDispatcher`, `MainThreadDispatcher`.
- `module_manifest.h`: `ModuleManifest`.
- `module_registry.h`: `ModuleRegistry`, `LifecycleState`.
- `service_container.h`: `ServiceContainer`.
- `task_scheduler.h`: `TaskDiagnostics`, `TaskHandle`, `SimpleTaskScheduler`, `TaskGroup`, `ITaskScheduler`.
