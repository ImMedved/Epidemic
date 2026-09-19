# RuntimeFoundation

## Назначение

RuntimeFoundation содержит общий язык EngineRuntime: типобезопасные IDs, budgets, игровое и кадровое время, spatial values и несколько нейтральных состояний. Он не реализует systems и не знает об Assets, World, Renderer или gameplay.

## Публичная модель

`AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `LazyRuleId`, `RegionId`, `ChunkId`, `SurfaceId` и `SimulationZoneId` являются отдельными типами. `RuntimeBudget` ограничивает число операций, bytes и CPU time. `RuntimeFrameDuration` описывает реальное время кадра, а `GameDuration` и `GameTimePoint` — календарное время мира. `Vec3`, `Quat`, `Transform`, `Aabb` и `Sphere` образуют минимальный spatial contract.

Нейтральные enums `AsyncOperationStatus`, `ResidencyState`, `ObjectRealityLevel`, `PersistenceTier` и `SimulationLod` используются несколькими majors, но не содержат gameplay meaning.

## Инварианты

Нулевой numeric Runtime ID невалиден; для `AssetId` и `ResourceId` тем же единственным invalid state является raw `StringId == 0`. `RuntimeBudget` является только value contract: нулевые item/byte/time limits означают unlimited, а hard/soft/defer semantics определяет consuming major. Арифметика game time имеет checked и saturating variants и не выполняет signed overflow.

`Normalize()` возвращает identity quaternion для нулевой, non-finite либо численно переполненной длины. Это convenience fallback, а не authoritative validation: перед записью состояния вызывающий код обязан использовать `IsFinite`, `IsNormalized` и `IsValidTransform` по своему boundary contract. `IsNormalized` использует tolerance `0.001` по squared length.

`ComposeTransform(parent, local)` фиксирует deterministic approximate TRS contract: position вычисляется через `TransformPoint(parent, local.position)`, rotation через normalized quaternion multiplication, scale через component-wise multiplication. Такая модель намеренно не представляет shear, возникающий при произвольной комбинации non-uniform parent scale и rotated child, и не обещает эквивалентность общему affine matrix multiplication. Публичного matrix decomposition API в RuntimeFoundation нет. Поэтому результат нельзя трактовать как lossless compose/decompose общего affine transform.

## Стабильность

Модуль имеет статус `LOCAL_READY` в рамках Goal 3.1. Это не означает whole-engine `SYSTEM_READY` или `FROZEN`. Добавлять сюда новый тип следует только тогда, когда он действительно нужен нескольким независимым majors и не принадлежит их domain.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `runtime_budget.h`: `RuntimeBudget`, `RuntimeBudgetConsumption`.
- `runtime_foundation.h`: umbrella header, который собирает общий RuntimeFoundation contract.
- `runtime_handles.h`: alias template `RuntimeHandle<Tag>` поверх `foundation::Handle<Tag>`.
- `runtime_ids.h`: `AssetIdTag`, `ResourceIdTag`, `RuntimeObjectIdTag`, `PersistentObjectIdTag`, `LazyRuleIdTag`, `RegionIdTag`, `ChunkIdTag`, `SurfaceIdTag`, `SimulationZoneIdTag`, `NumericRuntimeId`, `AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `LazyRuleId`, `RegionId`, `ChunkId`, `SurfaceId`, `SimulationZoneId`.
- `runtime_operation.h`: `AsyncOperationStatus`.
- `runtime_states.h`: `ResidencyState`, `ObjectRealityLevel`, `PersistenceTier`, `SimulationLod`.
- `runtime_time.h`: `RuntimeFrameDuration`, `GameDuration`, `GameTimePoint`.
- `spatial.h`: `Vec3`, `Quat`, `Transform`, `Aabb`, `Sphere`.
