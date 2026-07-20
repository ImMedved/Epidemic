# Scene

## Назначение

Scene хранит пространственное представление runtime-объектов: nodes, hierarchy, local/world transforms, bounds, visibility, mobility и spatial queries. Он не хранит gameplay placement и долговечную историю объекта.

## Контракты

`ISceneNodeRegistry` создает, связывает и уничтожает nodes. `ITransformRegistry` управляет local и world transform, включая `SetWorldTransform()`. `ISpatialIndex` выполняет пространственные queries, `ISceneQuery` читает node snapshots, а snapshot provider формирует detached состояние сцены.

`ReparentMode::KeepWorld` сохраняет world transform, `KeepLocal` сохраняет local. При уничтожении parent children отсоединяются с сохранением world transform.

## Инварианты

Hierarchy не содержит cycles. Dirty flags являются внутренним transient bookkeeping и не входят в authoritative snapshot. Clean operation не увеличивает revision. World transform write отклоняется при необратимом parent transform.

Scene использует TRS без полного shear representation.

## Стабильность

Модуль frozen. Physics и Animation взаимодействуют с ним только через Support adapters.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `bounds.h`: factory-функции или backend implementation без отдельного публичного типа.
- `scene_node.h`: `SceneNodeId`, `SceneNode`.
- `scene_node_registry.h`: `ReparentMode`, `ISceneNodeRegistry`.
- `scene_query.h`: `SceneNodeSnapshot`, `SceneSnapshot`, `ISceneQuery`, `ISceneSnapshotProvider`.
- `scene_services.h`: `SceneOptions`, `SceneServices`.
- `scene_state.h`: `SceneAttachmentState`, `SceneMobility`, `SceneVisibilityState`, `SceneDirtyFlags`.
- `spatial_index.h`: `ISpatialIndex`.
- `transform.h`: factory-функции или backend implementation без отдельного публичного типа.
- `transform_registry.h`: `WorldTransformWriteMode`, `ITransformRegistry`.
