# RHI_D3D11

## Назначение

RHI_D3D11 реализует публичные RHI contracts поверх Direct3D 11 и Win32 presentation surface. Детали COM, device context, render-target view и DXGI swap chain остаются внутри backend.

## Публичная граница

Модуль экспортирует factory создания D3D11 device. Все дальнейшее использование происходит через `IRhiDevice`, `IRhiCommandContext` и `IRhiSwapChain`. Upper code не должен выполнять downcast к D3D11 implementation.

## Поведение

Backend создает device и immediate context, формирует swap chain для окна, восстанавливает render target после resize и поддерживает clear/present smoke flow. Debug validation включается через descriptor composition root.

## Ограничения и стабильность

Модуль не содержит полноценный renderer и не обещает поддержку других платформ. D3D11 implementation может изменяться внутри, пока сохраняет RHI contract. RHI не зависит от RHI_D3D11.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `d3d11_rhi_device.h`: factory-функции или backend implementation без отдельного публичного типа.
