# Renderer

## Назначение

Renderer хранит render proxies и views, проверяет готовность resources, получает world transforms и pose snapshots, формирует deterministic backend-neutral submissions и владеет lifecycle подготовленного/backend frame. Он не является D3D11 implementation и не знает Scene, Animation или ResourceManager напрямую.

## Контракты

`IRenderScene` регистрирует proxies и их visibility/dirty state. `IViewSystem` управляет views. `IRendererRuntime::PrepareFrame()` выполняет все fallible transform/pose reads и формирует detached staged frame, не открывая backend frame. Повторный `PrepareFrame()` до `RenderFrame()` безопасно заменяет staged frame. `RenderFrame()` разрешён только после успешного prepare и отправляет уже подготовленные submissions в порядке `RenderLayer`, затем `RenderProxyId`. Dirty flags и staged transform cache коммитятся только после successful `EndFrame()`.

`IRenderResourceBridge` предоставляет mesh/material payloads. Failed/throwing `AcquirePayloads` означает отсутствие нового hold; failed/throwing `ReleasePayloads` сохраняет существующий hold за Renderer для retry. Temporary resource errors оставляют proxy в `Loading`, permanent errors и exceptions переводят readiness в `Failed`; ни один такой path не публикует proxy как `Ready`.

`IRenderSceneSource` предоставляет validated finite world transforms. Для анимированных объектов нейтральный `IRenderPoseSource` возвращает immutable `RenderPoseBuffer` по `RuntimeObjectId owner`; null pose является допустимым static path. Non-null pose обязан принадлежать requested owner и содержать только valid finite transforms. Scene и pose failures/exceptions завершают prepare до `IRenderCommandSink::BeginFrame()`, поэтому partial prepared transform state не публикуется.

## Backend frame ownership

`IRenderCommandSink::BeginFrame()` failure/exception означает, что backend frame не был открыт. После successful Begin failure/exception из `SubmitProxy()` или `EndFrame()` требует `AbortFrame()`. Successful Abort подтверждает закрытие frame. Failed/throwing Abort сохраняет private recovery ownership в Renderer; новый `BeginFrame()` запрещён, пока следующий `PrepareFrame()`, `RenderFrame()` или `Shutdown()` не выполнит successful reconciliation.

Prepared submissions сохраняются до successful frame completion либо замены новым prepare. Поэтому recovery после failed Abort не повторяет уже завершённый pose/transform staging.

## Identity, cleanup и shutdown

Proxy/view IDs consume значение только после successful local publication; exhaustion возвращается до mutation. Deferred proxy destroy сохраняет resource hold до confirmed `ReleasePayloads`. Drain выполняется deterministic по `RenderProxyId`; successful prefix удаляется окончательно, failed owner остаётся `DestroyPending` для retry.

Первый `Shutdown()` немедленно переводит Renderer в terminal lifecycle и запрещает новую render mutation. Если существует uncertain backend frame, сначала выполняется reconciliation. После этого resource holds освобождаются best-effort в deterministic proxy-ID order; failed releases остаются owned и повторный `Shutdown()` продолжает cleanup. Resize/recreate contract в Renderer отсутствует; presentation resize принадлежит Base RHI/integration layer.

## Factory и границы

`CreateRendererServices()` является strict production factory и требует `IRenderResourceBridge`, `IRenderSceneSource` и `IRenderCommandSink`; `IRenderPoseSource` optional. Он не подставляет mock dependencies даже при compatibility option `enable_mock_dependencies`. Reference behavior создаётся отдельно через `CreateMockRendererServices()`.

Scene/Resources/Animation подключаются к Renderer только adapters в Support. Renderer state transient и не имеет snapshot/restore persistence contract.

## Статус

Этот документ описывает локально проверяемый Goal 3 contract Renderer. Слово `FROZEN` не применяется до общесистемных Goals 5–9 и финального ledger/freeze gate.
