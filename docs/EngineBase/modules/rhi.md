# RHI

## Назначение

RHI задает минимальную backend neutral границу presentation lifecycle EngineBase. Модуль владеет контрактами device, command context и swap chain, а Null backend используется как исполнимая reference implementation для headless/test composition. Renderer grade resources, shaders, pipelines и render graph в этот слой не входят.

## Публичная модель

`IRhiDevice` является фабрикой command contexts и swap chains. `RhiDeviceDesc`, `RhiSwapChainDesc` и `RhiClearDesc` проходят controlled validation через `Result`. Allocation failure следует общей policy движка и может распространяться как `std::bad_alloc`, при этом уже опубликованное RHI state не меняется.

`IRhiCommandContext` имеет ровно два логических состояния: inactive и active frame. `BeginFrame()` переводит inactive в active. Повторный `BeginFrame()` отклоняется и сохраняет исходный active frame. `Clear()` допустим только внутри active frame и при наличии живого active presentation target. Rejected clear не завершает frame. `EndFrame()` переводит active в inactive, а вызов из inactive отклоняется.

`IRhiSwapChain` хранит presentation descriptor state. `Present()` допустим до первого resize и после успешного resize. `Resize(width, height)` требует обе ненулевые координаты. Нулевой client area, характерный для minimized window, не является новым swap-chain размером: такой resize отклоняется, последние usable dimensions сохраняются. Повторный resize в уже текущий размер является допустимым semantic no-op.

## Null backend contract

Null backend обязан быть lifecycle strict, а не permissive заглушкой. Последний успешно созданный swap chain становится active presentation target для command contexts того же device state. Failed replacement creation не меняет active target. Уничтожение active swap chain инвалидирует presentation binding, поэтому последующий `Clear()` получает controlled `rhi.no_swap_chain`, а stale target не сохраняется скрытой strong reference.

Command frame принадлежит самому command context. Потеря presentation target не завершает caller owned frame автоматически: caller может получить controlled failure на `Clear()` и затем закрыть frame через `EndFrame()`. Уничтожение active command context уничтожает и его frame state; новый context всегда создается inactive. Device wrapper не является shutdown token: уже созданные shared command context/swap chain удерживают необходимое backend state и могут корректно завершить свой lifetime после уничтожения wrapper.

## Descriptor validation

`RhiDeviceDesc::debug_name` не может быть пустым. `RhiSwapChainDesc` требует valid surface handle, ненулевые dimensions, ненулевой buffer count и только явно поддерживаемый `RhiPixelFormat`. `Unknown` и произвольные out-of-range enum values отклоняются с `rhi.invalid_color_format`.

`RhiClearDesc` должен запрашивать хотя бы color clear, а все компоненты цвета должны быть finite. Rejected descriptor не публикует partial state.

## Ownership, atomicity и no-op semantics

RHI не хранит persistent state, revisions, journals, IDs/generations или secondary indexes. `PresentationSurfaceHandle` является opaque borrowed native pointer wrapper, а не engine identity space.

Null device state содержит только weak active presentation binding. Swap-chain descriptor и command-context frame flag являются primary local state соответствующих объектов. Swap-chain replacement публикуется только после полной validation и успешного создания объекта. Failed resize сохраняет обе предыдущие dimensions. No-op resize в тот же размер возвращает success без изменения observable state.

Модуль не вызывает внешние callbacks/providers/backend APIs. Реальный backend boundary принадлежит отдельному `EngineBase/RHI_D3D11` и квалифицируется пунктом 2.8. До Goal 6 mutable RHI operations считаются externally serialized.

## Persistence и cleanup

RHI state является transient и не участвует в save/restore. У модуля нет shutdown API или fallible cleanup contract. Resource cleanup выполняется lifetime ownership соответствующих shared objects. Null backend не удерживает swap chain сильной ссылкой из device state, поэтому уничтоженный presentation target не остается скрыто живым.

## Карта публичных заголовков

- `descriptors.h`: `RhiColor`, `RhiClearDesc`, `RhiDeviceDesc`, `RhiSwapChainDesc` и validation helpers.
- `irhi_command_context.h`: `IRhiCommandContext`.
- `irhi_device.h`: `IRhiDevice`.
- `irhi_swap_chain.h`: `IRhiSwapChain`.
- `null_rhi_device.h`: `CreateNullRhiDevice`.
- `pixel_format.h`: `RhiPixelFormat`, `ToString`.
- `presentation_surface_handle.h`: `PresentationSurfaceHandle`.
