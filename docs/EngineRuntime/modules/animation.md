# Animation

## Назначение

Animation управляет skeleton/clip metadata, animator instances, playback state, crossfade, LOD, pose publication и events. Реальная оценка pose вынесена в evaluator backend.

## Контракты

`ISkeletonRegistry` и `IAnimationClipRegistry` регистрируют данные. `IAnimationRuntime` создает generation handle, выполняет Play, Pause, Stop, Crossfade, Tick и queries. `IPoseProvider` читает immutable pose. `IAnimationResourceSource` загружает descriptors, `IAnimationEvaluatorBackend` оценивает pose, а `IAnimationPoseSink` публикует его.

## Playback

Frame time выражается `RuntimeFrameDuration`. Fractional playback rate сохраняет remainder. Loop выполняет modulo clip duration. Crossfade продвигает source/target time и weights. Invalid transition отклоняется; stale pose query возвращает error.

Pose buffer публикуется immutable snapshot. Event queue bounded; нулевая capacity использует зафиксированную default policy.

## Стабильность

Модуль frozen. Animation graph, IK, retargeting и motion matching будут верхними extensions или backends.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `animation.h`: factory-функции или backend implementation без отдельного публичного типа.
- `animation_runtime.h`: `ISkeletonRegistry`, `IAnimationClipRegistry`, `IAnimationRuntime`, `IPoseProvider`, `IAnimationResourceSource`, `IAnimationPoseSink`, `IAnimationEvaluatorBackend`, `IAnimationEventBuffer`, `AnimationDependencies`, `AnimationServices`.
- `animation_types.h`: `SkeletonId`, `AnimationClipId`, `AnimatorInstanceId`, `AnimatorLifecycle`, `AnimatorReadiness`, `AnimatorPlaybackState`, `PoseState`, `AnimationLodLevel`, `SkeletonDesc`, `AnimationClipDesc`, `AnimatorPlayback`, `CrossfadeState`, `AnimatorDesc`, `AnimationEvent`, `AnimatorHandle`, `PoseSnapshot`, `PoseBuffer`, `AnimatorSnapshot`, `AnimationEvaluationRequest`, `AnimationPlaybackCommand`, `AnimationOptions`.
