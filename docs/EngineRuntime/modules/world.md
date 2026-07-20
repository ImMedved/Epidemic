# World

## Назначение

World хранит логическую модель объектов и пространства мира независимо от их текущего render/physics representation. Он описывает regions, chunks, placement, reality level, residency, persistence tier и materialization.

## Объекты

`IWorldObjectRegistry` применяет revisioned commands создания, перемещения, изменения residency, продвижения persistence tier и уничтожения. `IWorldQuery` выполняет detached queries. Placement является variant: world surface, container, inventory, equipped, hidden или destroyed.

Destroyed object terminal. Обычная команда не может выставить `DestroyedPlacement` и не может воскресить запись. Объекты уровня `PlayerTouched` и выше требуют уникальный `PersistentObjectId`.

## Chunks и regions

`IRegionRegistry` и `IChunkRegistry` регистрируют географию. Chunk state меняется через `ChangeChunkStateCommand` с expected revision и разрешенной state transition. Unknown chunk не маскируется состоянием Unloaded.

## Materialization

`IObjectMaterializer` создает concrete runtime representation только для совместимого placement. Demotion требует runtime-issued `DemotionCommitToken`; tokens можно revoke и они инвалидируются при изменении object revision.

## Стабильность

World frozen. Он не содержит inventory rules, корабли, NPC или quests; GameFramework строит их поверх neutral records.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `chunk.h`: `ChunkDescriptor`, `ChangeChunkStateCommand`, `ChunkSnapshot`, `IChunkRegistry`.
- `object_materialization.h`: `MaterializationState`, `MaterializationRequest`, `DemotionCommitToken`, `DemotionSnapshot`, `DemotionRequest`, `IObjectMaterializer`, `IDemotionCommitAuthority`.
- `object_placement.h`: `WorldSurfacePlacement`, `ContainerPlacement`, `InventoryPlacement`, `EquippedPlacement`, `HiddenPlacement`, `DestroyedPlacement`.
- `region.h`: `RegionDescriptor`, `IRegionRegistry`.
- `world_commands.h`: `WorldCommandResult`, `CreateObjectCommand`, `ChangePlacementCommand`, `ChangeResidencyCommand`, `PromotePersistenceTierCommand`, `MaterializeObjectCommand`, `DemoteObjectCommand`, `DestroyObjectCommand`.
- `world_invariants.h`: factory-функции или backend implementation без отдельного публичного типа.
- `world_location.h`: `WorldLocation`.
- `world_object.h`: `WorldObjectRecord`.
- `world_object_registry.h`: `IWorldObjectRegistry`.
- `world_query.h`: `IWorldQuery`.
- `world_services.h`: `WorldOptions`, `WorldServices`.
- `world_state.h`: `ChunkState`.
