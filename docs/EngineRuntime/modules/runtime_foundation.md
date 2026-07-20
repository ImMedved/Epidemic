# RuntimeFoundation

## Назначение

RuntimeFoundation содержит общий язык EngineRuntime: типобезопасные IDs, budgets, игровое и кадровое время, spatial values и несколько нейтральных состояний. Он не реализует systems и не знает об Assets, World, Renderer или gameplay.

## Публичная модель

`AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `RegionId`, `ChunkId`, `SurfaceId` и `SimulationZoneId` являются отдельными типами. `RuntimeBudget` ограничивает число операций, bytes и CPU time. `RuntimeFrameDuration` описывает реальное время кадра, а `GameDuration` и `GameTimePoint` — календарное время мира. `Vec3`, `Quat`, `Transform`, `Aabb` и `Sphere` образуют минимальный spatial contract.

Нейтральные enums `AsyncOperationStatus`, `ResidencyState`, `ObjectRealityLevel`, `PersistenceTier` и `SimulationLod` используются несколькими majors, но не содержат gameplay meaning.

## Инварианты

Нулевой ID невалиден. Арифметика game time имеет checked и saturating variants. Quaternion и transform валидируются перед записью authoritative state. TRS-модель не представляет shear от произвольной комбинации rotated child и non-uniform parent scale; вызывающий код должен соблюдать ограничение hierarchy.

## Стабильность

Модуль frozen. Добавлять сюда новый тип следует только тогда, когда он действительно нужен нескольким независимым majors и не принадлежит их domain.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `runtime_budget.h`: `RuntimeBudget`, `RuntimeBudgetConsumption`.
- `runtime_foundation.h`: factory-функции или backend implementation без отдельного публичного типа.
- `runtime_handles.h`: factory-функции или backend implementation без отдельного публичного типа.
- `runtime_ids.h`: `AssetIdTag`, `ResourceIdTag`, `RuntimeObjectIdTag`, `PersistentObjectIdTag`, `LazyRuleIdTag`, `RegionIdTag`, `ChunkIdTag`, `SurfaceIdTag`, `SimulationZoneIdTag`, `NumericRuntimeId`, `AssetId`, `ResourceId`, `RuntimeObjectId`, `PersistentObjectId`, `LazyRuleId`, `RegionId`, `ChunkId`, `SurfaceId`, `SimulationZoneId`.
- `runtime_operation.h`: `AsyncOperationStatus`.
- `runtime_states.h`: `ResidencyState`, `ObjectRealityLevel`, `PersistenceTier`, `SimulationLod`.
- `runtime_time.h`: `RuntimeFrameDuration`, `GameDuration`, `GameTimePoint`.
- `spatial.h`: `Vec3`, `Quat`, `Transform`, `Aabb`, `Sphere`.
