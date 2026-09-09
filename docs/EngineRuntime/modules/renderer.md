# Renderer

## Назначение

Renderer хранит render proxies и views, проверяет готовность resources, получает world transforms и формирует backend-neutral submissions. Он не является D3D11 implementation и не знает Scene, Animation или ResourceManager напрямую.

## Контракты

`IRenderScene` регистрирует proxies и их visibility/dirty state. `IViewSystem` управляет views. `IRendererRuntime` готовит и отправляет frame. `IRenderResourceBridge` предоставляет mesh/material payloads, `IRenderSceneSource` — transforms, `IRenderCommandSink` принимает готовые submissions.

Для анимированных объектов существует нейтральный `IRenderPoseSource`. Renderer запрашивает `RenderPoseBuffer` по `RuntimeObjectId owner`. `RenderPoseBuffer` содержит только owner, bone transforms и revision и не зависит от Animation types. `RenderProxySubmission::pose` может быть пустым для static object.

## Владение

Renderer удерживает resource ownership через bridge только пока proxy действительно нуждается в payloads. Deferred destroy и shutdown освобождают retained resources best-effort с сохранением failed cleanup для retry согласно runtime lifecycle.

## Граница

Renderer не содержит gameplay visibility, actor animation state или конкретный D3D backend. Scene/Resources/Animation подключаются только adapters в Support.

## Стабильность

Foundation contract Renderer считается frozen. Production rendering backend реализуется поверх существующих ports.
