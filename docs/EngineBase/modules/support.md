# Support

## Назначение

`EpidemicEngineBaseSupport` — официальный слой композиции EngineBase. Он объединяет повторяющуюся регистрацию сервисов и frame handlers, чтобы приложения и EngineRuntime не копировали Win32/RHI wiring.

## Публичные функции

`RegisterEngineBase()` создает базовые services. `RegisterWindowsRuntime()` и `RegisterInputRuntime()` добавляют platform и raw input. `RegisterGraphicsRuntime()` выбирает Null или D3D11 backend. `CreateMainWindow()` и `RegisterMainSwapChain()` связывают окно с presentation. Frame-loop helpers добавляют platform pump, input publication, clear/present и optional throttle.

Support возвращает созданные services или `Result`, а не скрывает их в global state. Объекты по-прежнему принадлежат service container и явным shared owners.

## Граница

Support может зависеть от всех нижних EngineBase modules, но ни один из них не зависит от Support. Здесь не размещаются resources, renderer, world, gameplay или общая логика приложения. Если функция становится самостоятельной runtime-системой, она должна перейти в EngineRuntime.

## Стабильность

EngineBase Support считается стабильным. Новые convenience helpers допустимы, но текущие функции не должны превращаться в неявный service locator или монолитный engine bootstrap.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `engine_base_support.h`: `ILogger`, `EngineBaseOptions`, `GraphicsRuntimeOptions`, `GraphicsRuntimeServices`, `FramePlatformEvents`.
- `graphics_backend.h`: `GraphicsBackend`.
