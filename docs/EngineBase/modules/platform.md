# Platform

## Назначение

Platform изолирует Win32 и предоставляет движку окна, системное время, process information, dynamic libraries и поток platform events. Остальные модули не включают Windows headers и не обрабатывают `HWND` напрямую.

## Публичная модель

`IPlatformRuntime` описывает runtime платформы, high-resolution clock, event pump, exit request и загрузку dynamic library. `IWindowSystem` создает `IWindow` и выдает события. `NativeWindowHandle` переносит native handle через типобезопасную границу. `PlatformEvent` описывает resize, focus, keyboard, mouse и закрытие окна.

`WindowsPlatformRuntime` является официальной реализацией. Window lifetime выражается `shared_ptr<IWindow>`; закрытие native window отражается в объекте и event stream.

## Использование

Composition root вызывает `RegisterWindowsRuntime()`, затем `CreateMainWindow()`. Каждый кадр platform phase вызывает `PumpEvents()` и передает drained events Input и другим consumers.

`LoadDynamicLibrary()` возвращает `Result`, а `IDynamicLibrary::FindSymbol()` не раскрывает конкретный Win32 module handle.

## Инварианты и ограничения

Platform не знает о gameplay input actions, renderer, swap chain или UI. Он сообщает размер и native surface, но не выполняет graphics resize самостоятельно. Модуль Windows-only и считается стабильным.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `idynamic_library.h`: `IDynamicLibrary`.
- `iplatform_runtime.h`: `ProcessInfo`, `IPlatformRuntime`.
- `iwindow.h`: `IWindow`.
- `iwindow_system.h`: `WindowCreateInfo`, `IWindowSystem`.
- `native_window_handle.h`: `NativeWindowHandle`.
- `platform_event.h`: `PlatformEventType`, `PlatformEvent`.
- `windows_platform_runtime.h`: `WindowsPlatformRuntime`.
