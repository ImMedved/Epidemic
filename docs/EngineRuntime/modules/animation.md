# Animation

## Назначение

Animation управляет skeleton/clip metadata, animator instances, playback, crossfade, LOD, pose publication и events. Реальная оценка pose вынесена в evaluator backend; Animation не знает Renderer.

## Контракты

`ISkeletonRegistry` и `IAnimationClipRegistry` регистрируют descriptors. `IAnimationRuntime` создает generation handle, выполняет Play/Pause/Stop/Crossfade/Tick и возвращает snapshots. `IAnimationResourceSource` загружает skeleton/clip descriptors, `IAnimationEvaluatorBackend` оценивает pose, а `IAnimationPoseSink` публикует immutable `PoseBuffer`.

`PoseBuffer` содержит `AnimatorHandle`, `RuntimeObjectId owner`, bone transforms и revision. Owner является нейтральной связью с runtime object и позволяет Support передать pose Renderer без прямой зависимости Animation → Renderer.

## Playback и владение

Frame time выражается `RuntimeFrameDuration`. Crossfade и playback state принадлежат animator. Pose после публикации является immutable snapshot. Resource adapter может временно удерживать ResourceLease во время загрузки, но после копирования готового skeleton/clip descriptor payload больше не обязан оставаться pinned.

## Граница

Animation не содержит animation graph gameplay rules, actor state, Renderer contracts или конкретный resource manager. Retargeting, IK, motion matching и сложный graph могут появляться выше либо за отдельными backend/extension contracts.

## Стабильность

После локального аудита Goal 3 и canonical evidence convergence модуль имеет статус `LOCAL_READY`. Это не означает `FROZEN`: общесистемные integration/fault/load gates и финальный freeze выполняются в последующих Goals 5–9. Cross-major pose wiring реализуется только в Support.
