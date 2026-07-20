# Physics

## Назначение

Physics управляет collision shapes, bodies, fixed-step simulation, spatial queries и contact events через внешний или reference backend. Runtime handles отделены от backend handles.

## Контракты

`ICollisionShapeRegistry` регистрирует shapes. `IPhysicsScene` создает и уничтожает bodies. `IPhysicsStepper` выполняет fixed steps. `IPhysicsQuery` предоставляет snapshots, raycast и overlap. `IPhysicsEventBuffer` хранит текущий contact batch. `IPhysicsRuntimeLifecycle` выполняет retryable shutdown.

`IPhysicsBackend` работает только с `BackendBodyHandle` и `BackendShapeHandle`, возвращает `BackendBodySnapshot`, `BackendRaycastHit` и `BackendContactEvent`. Runtime преобразует их в public handles.

## Step

После успешного backend simulation интервал считается потребленным, даже если synchronization или Scene sink завершились ошибкой. Backend snapshots сначала собираются и валидируются batch-first, затем authoritative body records меняются и только после этого публикуются sink writes. Invalid transform/velocity не повреждает state.

Contact payload проходит validation finite values и handles; invalid events фильтруются, valid batch публикуется согласованно.

## Shutdown

Bodies уничтожаются раньше shapes. Failed body сохраняет backend handle и не позволяет уничтожить используемую shape. Повторный shutdown завершает оставшийся cleanup.

## Стабильность

После финальной validation модуль frozen. Production SDK подключается через существующий backend port.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `physics_event_buffer.h`: `IPhysicsEventBuffer`.
- `physics_query.h`: `IPhysicsQuery`.
- `physics_scene.h`: `ICollisionShapeRegistry`, `IPhysicsScene`, `IPhysicsStepper`, `IPhysicsRuntimeLifecycle`, `IPhysicsBackend`, `IPhysicsTransformSource`, `IPhysicsTransformSink`, `PhysicsServices`, `PhysicsDependencies`.
- `physics_types.h`: `PhysicsBodyId`, `PhysicsBodyHandle`, `CollisionShapeId`, `BackendShapeHandle`, `BackendBodyHandle`, `PhysicsBodyType`, `PhysicsActivityState`, `PhysicsEventState`, `PhysicsDirtyFlags`, `PhysicsBodyDesc`, `PhysicsBackendOptions`, `PhysicsOptions`, `CollisionShapeDesc`, `PhysicsMaterial`, `ContactEvent`, `BackendContactEvent`, `RaycastQuery`, `RaycastHit`, `BackendRaycastHit`, `OverlapQuery`, `OverlapResult`, `PhysicsStepResult`, `PhysicsBodySnapshot`, `BackendBodySnapshot`.
