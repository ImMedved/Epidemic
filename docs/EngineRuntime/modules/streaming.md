# Streaming

## Назначение

Streaming координирует residency targets под byte/CPU budget. Он объединяет demands нескольких consumers, выполняет progressive plan, commit/rollback и управляет жизненным циклом chunk от запроса до unload.

## Контракты

`IStreamingRuntime` принимает `Request`, освобождает `StreamingDemandHandle`, выполняет `Tick` и выдает progress/statistics. `IStreamingQuery` предоставляет read-only view. `IStreamingController` выполняет административную отмену и terminal shutdown.

`StreamingDependencies` содержит `IStreamingDataSource`, commit target, priority provider, residency controller и источники World, Persistence и Resources. Все long-lived dependencies передаются через `shared_ptr`.

## Demands и requests

Каждый consumer владеет demand, но один target может иметь общий request. Во время необратимого Unloading создается не более одного successor; все новые demands присоединяются к нему. Successor не начинает загрузку до завершения predecessor. Освобождение последнего waiting demand отменяет successor.

Terminal history удаляет mapping только если он все еще указывает на удаляемый request.

## Progressive execution

Data source получает доступный budget и возвращает `processed_bytes` и `completed`. Cursor меняется только после completed step. Commit выполняется один раз после completed commit-step. Byte counters и IDs защищены от overflow.

## Shutdown

После начала shutdown новые demands запрещены. Cleanup дренирует reversible work, unload и rollback; failed external cleanup сохраняется для повторной попытки. Success означает отсутствие demands, live requests и resident ownership.

## Стабильность

После перечисленных финальных fixes модуль считается frozen. Support реализует реальные sources и commit target через public dependencies.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `residency_controller.h`: `IResidencyController`.
- `streaming_priority_resolver.h`: `IStreamingPriorityResolver`.
- `streaming_runtime.h`: `IStreamingRuntime`, `IStreamingQuery`, `IStreamingController`, `StreamingServices`, `StreamingDependencies`.
- `streaming_sources.h`: `IStreamingWorldSource`, `IStreamingPersistenceSource`, `IStreamingResourceSource`, `IStreamingDataSource`, `IStreamingCommitTarget`, `IStreamingPriorityProvider`.
- `streaming_types.h`: `StreamingState`, `StreamingPriorityClass`, `StreamingRequestId`, `StreamingRequestHandle`, `StreamingDemandId`, `StreamingDemandHandle`, `ChunkStreamingTarget`, `RegionStreamingTarget`, `AssetStreamingTarget`, `ObjectStreamingTarget`, `ResourceGroupStreamingTarget`, `StreamingPlanStep`, `StreamingPlanStepRecord`, `StreamingStepResult`, `ProgressiveLoadPlan`, `StreamingCancellationToken`, `StreamingRequest`, `StreamingBudget`, `StreamingProgress`, `StreamingTickFailure`, `StreamingTickResult`, `StreamingStatistics`.
