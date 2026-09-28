# Physics

## Назначение

Physics управляет collision shapes, bodies, fixed-step simulation, spatial queries и contact events через внешний или reference backend. Runtime handles отделены от backend handles.

## Контракты

`ICollisionShapeRegistry` регистрирует shapes. `IPhysicsScene` создает и уничтожает bodies. `IPhysicsStepper` выполняет fixed steps. `IPhysicsQuery` предоставляет snapshots, raycast и overlap. `IPhysicsEventBuffer` хранит текущий contact batch. `IPhysicsRuntimeLifecycle` выполняет retryable shutdown.

`IPhysicsBackend` работает только с `BackendBodyHandle` и `BackendShapeHandle`, возвращает `BackendBodySnapshot`, `BackendRaycastHit` и `BackendContactEvent`. Runtime преобразует их в public handles. Successful `CreateShape`/`CreateBody` передает ownership нового backend object с valid handle, уникальным среди live objects этого типа; alias существующего handle является backend contract violation и не уничтожается Runtime как rollback ownership.

## Step

`Tick(delta)` использует prefix-progress semantics. Ошибки validation и time-arithmetic preflight происходят до acceptance и сохраняют timing state. После успешного preflight `delta` принимается в accumulator ровно один раз. Если поздний fixed step завершается ошибкой, уже завершенный prefix остается committed, а оставшееся принятое время остается в accumulator. Для продолжения принятой работы используется `Tick(0)` либо следующий вызов только с новым frame delta; delta не передается повторно как retry. Если `SimulateFixed` уже завершился успешно, а snapshot/contact synchronization завершилась ошибкой, retry завершает тот же backend step без повторного `SimulateFixed`.

Backend snapshots сначала собираются и валидируются batch-first, затем authoritative body records меняются и только после этого публикуются sink writes. Invalid transform/velocity не повреждает state.

Contact payload проходит validation finite values и handles; invalid events фильтруются. Valid events сохраняют backend order, включая duplicate events: Physics не выполняет implicit deduplication.

Raycast принимает finite direction только если его magnitude больше `float epsilon`. Нормализация выполняется без overflow для всего finite `float` range, поэтому accepted large finite direction не может превратиться в zero или non-finite payload. Public external-backend path и reference `RaycastBackend()` используют одну numeric policy.

## Shutdown

Bodies уничтожаются раньше связанных с ними shapes. Failure одного body сохраняет его backend handle, запрещает уничтожение используемой им shape и не блокирует best-effort cleanup независимых bodies и свободных shapes. Pending rollback body с неизвестной опубликованной shape relation временно удерживает все shapes до успешного cleanup. Повторный shutdown продолжает оставшийся cleanup и после полного завершения является idempotent.

## Стабильность

Документ описывает проверенный локальный contract. После прохождения Goal 3 audit и ledger gates модуль имеет статус `LOCAL_READY`; системный `FROZEN` остаётся за Goals 5–9. Production SDK подключается через существующий backend port.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `physics_event_buffer.h`: `IPhysicsEventBuffer`.
- `physics_query.h`: `IPhysicsQuery`.
- `physics_scene.h`: `ICollisionShapeRegistry`, `IPhysicsScene`, `IPhysicsStepper`, `IPhysicsRuntimeLifecycle`, `IPhysicsBackend`, `IPhysicsTransformSource`, `IPhysicsTransformSink`, `PhysicsServices`, `PhysicsDependencies`.
- `physics_types.h`: `PhysicsBodyId`, `PhysicsBodyHandle`, `CollisionShapeId`, `BackendShapeHandle`, `BackendBodyHandle`, `PhysicsBodyType`, `PhysicsActivityState`, `PhysicsEventState`, `PhysicsDirtyFlags`, `PhysicsBodyDesc`, `PhysicsBackendOptions`, `PhysicsOptions`, `CollisionShapeDesc`, `PhysicsMaterial`, `ContactEvent`, `BackendContactEvent`, `RaycastQuery`, `RaycastHit`, `BackendRaycastHit`, `OverlapQuery`, `OverlapResult`, `PhysicsStepResult`, `PhysicsBodySnapshot`, `BackendBodySnapshot`.
