# Renderer

## Назначение

RendererFoundation хранит render proxies и views, отслеживает готовность resources, обновляет transforms и формирует backend-neutral submissions. Он не является готовым D3D renderer и не владеет asset loading.

## Контракты

`IRenderScene` регистрирует proxies, меняет visibility и dirty state. `IViewSystem` управляет views. `IRendererRuntime` готовит и отправляет frame, abort-ит незавершенный frame и выполняет explicit shutdown. `IRenderResourceBridge` предоставляет typed mesh/material resources, `IRenderSceneSource` — transforms, а `IRenderCommandSink` принимает submissions.

Proxy может быть Loading, Ready или Failed. Отсутствующий resource не останавливает весь frame. Submission order детерминирован по layer и ID.

## Владение

Renderer удерживает resource leases через bridge и освобождает их best-effort при удалении proxy или shutdown. Dependencies имеют shared ownership. Failed cleanup сохраняется для диагностики и retry согласно lifecycle.

## Ограничения и стабильность

Нет shaders, render graph, lighting, shadows или D3D command implementation. Это frozen foundation port для будущего Renderer backend.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `render_resource_bridge.h`: `RenderTransformSnapshot`, `IRenderResourceBridge`, `IRenderSceneSource`, `RenderProxySubmission`, `RenderFrameContext`, `IRenderCommandSink`.
- `render_scene.h`: `IRenderScene`.
- `render_types.h`: `RenderProxyId`, `ViewId`, `IRenderMeshResource`, `IRenderMaterialResource`, `RenderProxyLifecycle`, `RenderProxyReadiness`, `RenderProxyVisibility`, `RenderProxyDirtyFlags`, `RenderLayer`, `RenderFrameState`, `ViewLifecycle`, `RenderProxyDesc`, `ViewDesc`, `RenderResourcePayloads`.
- `renderer_runtime.h`: `IRendererRuntime`.
- `renderer_services.h`: `RendererOptions`, `RendererDependencies`, `RendererServices`.
- `view_system.h`: `IViewSystem`.
