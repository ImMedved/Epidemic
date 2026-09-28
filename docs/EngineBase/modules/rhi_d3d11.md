# RHI_D3D11

## Назначение

RHI_D3D11 реализует публичные RHI contracts поверх Direct3D 11 и Win32 presentation surface. Детали COM, device context, render-target view и DXGI swap chain остаются внутри backend.

## Публичная граница

Модуль экспортирует только `CreateD3D11RhiDevice`. Успешный factory возвращает `std::shared_ptr<IRhiDevice>`. Все дальнейшее использование происходит через `IRhiDevice`, `IRhiCommandContext` и `IRhiSwapChain`; concrete D3D11 classes остаются private и upper code не должен выполнять downcast.

## Ownership и lifetime

`D3D11DeviceState` совместно удерживается device wrapper, command contexts и swap chains. Поэтому уже созданные child objects сохраняют native device/context lifetime после уничтожения wrapper. Active presentation target хранится как `weak_ptr`, поэтому уничтожение swap chain не оставляет dangling binding.

COM resources хранятся только в `Microsoft::WRL::ComPtr`. Factory и swap-chain construction публикуют объект наружу только после полного успешного создания. Ошибка после частичного native acquisition уничтожает локальный candidate и освобождает уже полученные COM references через RAII.

## Resize, minimize и render target

Zero-area resize отклоняется до `IDXGISwapChain::ResizeBuffers` с `rhi.invalid_swap_chain_size`. Это является baseline contract для minimized surface. После успешного `ResizeBuffers` старые back-buffer/RTV references уже освобождены, а новый RTV создаётся заново. Пока recreation не завершена, `Present` и `Clear` возвращают `rhi.d3d11.recreate_required`; silent clear без render target запрещён. Повторный валидный `Resize` является recovery path.

## Device lost и ошибки backend

DXGI device-loss результаты (`DXGI_ERROR_DEVICE_HUNG`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, `DXGI_ERROR_DRIVER_INTERNAL_ERROR`) переводятся в стабильный `rhi.d3d11.device_lost`. После этого новая native work не публикуется через device/context/swap-chain APIs. Остальные native failures возвращаются через operation-specific `Result` errors с HRESULT diagnostics.

При `enable_debug_validation=true` backend запрашивает `D3D11_CREATE_DEVICE_DEBUG`. Отсутствующий SDK debug layer возвращается как отдельный controlled error `rhi.d3d11.debug_layer_unavailable`; backend не молча откатывается на non-debug device.

## Поведение

Backend создаёт D3D11 device и immediate context, формирует DXGI swap chain для live Win32 window, создаёт back buffer и RTV, поддерживает begin/clear/end/present flow и атомарный recovery после resize/recreation failures. Non-window native handle отклоняется до вызова DXGI `CreateSwapChain`.

## Threading

Для Goal 2 внутренней thread-safety гарантии нет. Mutable D3D11 operations требуют внешней сериализации. Полная concurrency qualification относится к Goal 6.

## Карта публичных заголовков

- `d3d11_rhi_device.h`: `CreateD3D11RhiDevice`.
