# RHI

## Назначение

RHI задает минимальную границу к presentation hardware. Он позволяет создать device, command context и swap chain, начать кадр, очистить back buffer, завершить кадр, выполнить present и resize.

## Модель

`IRhiDevice` создает `IRhiCommandContext` и `IRhiSwapChain`. `RhiDeviceDesc` и `RhiSwapChainDesc` являются backend-neutral descriptors. `PresentationSurfaceHandle` переносит native window surface. `IRhiCommandContext` имеет явное состояние активного кадра.

`NullRhiDevice` используется там, где graphics API не нужен, но composition ожидает RHI service.

## Инварианты

`BeginFrame()` и `EndFrame()` образуют пару. `Clear()` выполняется только внутри активного кадра. Swap-chain resize принимает ненулевой размер и не должен выполняться для minimized surface до восстановления окна.

## Граница ответственности

Это не renderer-grade abstraction. Здесь нет meshes, buffers, textures, shaders, pipelines, materials, cameras, lights или render graph. Renderer из EngineRuntime строит собственную модель и использует RHI/backend port для фактической отправки команд.

## Стабильность

Текущий presentation contract заморожен. Расширять его следует только когда реальный renderer backend доказал отсутствие необходимой низкоуровневой возможности.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `descriptors.h`: `RhiColor`, `RhiClearDesc`, `RhiDeviceDesc`, `RhiSwapChainDesc`.
- `irhi_command_context.h`: `IRhiCommandContext`.
- `irhi_device.h`: `IRhiDevice`.
- `irhi_swap_chain.h`: `IRhiSwapChain`.
- `null_rhi_device.h`: factory-функции или backend implementation без отдельного публичного типа.
- `pixel_format.h`: `RhiPixelFormat`.
- `presentation_surface_handle.h`: `PresentationSurfaceHandle`.
