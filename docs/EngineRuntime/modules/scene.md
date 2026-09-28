# Scene

## Назначение

Scene хранит пространственное представление runtime-объектов: nodes, hierarchy, local/world transforms, bounds, visibility, mobility и spatial queries. Он не хранит gameplay placement, долговечную историю объекта или authoritative persistence state.

## Контракты

`ISceneNodeRegistry` создает, связывает и уничтожает nodes. `ITransformRegistry` управляет local и world transform, включая `SetWorldTransform()`. `ISpatialIndex` хранит per-node local bounds и вычисляет world bounds, `ISceneQuery` выполняет spatial queries, а snapshot provider формирует detached value snapshot сцены.

`ReparentMode::KeepWorld` сохраняет observable world TRS в рамках deterministic approximate TRS модели RuntimeFoundation. World-to-local conversion дополнительно проходит roundtrip preflight: повторная композиция должна совпадать с requested world transform с absolute tolerance `0.001` и float-relative tolerance `32 * epsilon * magnitude`. Если parent transform численно плохо обусловлен и это условие не выполняется, операция возвращает `scene.unrepresentable_world_transform` без mutation. `KeepLocal` сохраняет local transform. При уничтожении parent его прямые children отсоединяются с `KeepWorld` semantics, поэтому их observable world transform и world transform их descendants сохраняются. Hierarchy traversal выполняется iteratively.

Invalid/stale node IDs, invalid enum values, non-finite/degenerate transforms и invalid bounds отвергаются до mutation. World-to-local conversion отвергается, если parent scale не является invertible по `kSpatialEpsilon`. Любая hierarchy/transform mutation до commit проверяет candidate world TRS всего затрагиваемого subtree: overflow, underflow до degenerate scale и `Inf/NaN` дают `scene.world_transform_out_of_range`. Для nodes с bounds отдельно проверяются все восемь transformed corners; непредставимые world bounds дают `scene.world_bounds_out_of_range`. Multi-record reparent/detach/destroy paths полностью готовят child lists, subtree state и revision range до публикации live mutations.

## No-op и revision semantics

Повторный attach к уже текущему parent и detach уже detached node являются no-op и не меняют revision. `SetMobility`/`SetVisibility` с уже установленным значением также являются no-op. `MarkTransformClean` и `MarkBoundsClean` меняют только transient dirty bookkeeping и не повышают authoritative revision.

`SetLocalTransform`, `SetWorldTransform` и `SetLocalBounds` являются explicit authored writes. Успешная повторная запись того же значения считается новой accepted mutation, повышает revision и выставляет соответствующие dirty flags. Это позволяет явно переопубликовать transform/bounds без отдельного invalidate API.

## Bounds и queries

World AABB вычисляется из всех восьми corners после применения deterministic TRS и корректно поддерживает rotation, non-uniform scale и negative scale. `QueryAabb` и `QuerySphere` возвращают только visible nodes с bounds в возрастающем `SceneNodeId` order. Invalid query geometry возвращает пустой result и не меняет Scene state.

## Snapshot и persistence scope

`SceneSnapshot` является deterministic detached value snapshot: nodes сортируются по `SceneNodeId`, internal vectors/maps/aliases наружу не публикуются, dirty flags в snapshot не входят. Snapshot предназначен для observation/system-level materialization evidence, а не является локальным persistence checkpoint.

Scene официально является transient runtime materialization state. Локальный `Restore()` для Scene в Goal 3 не требуется и не добавляется. System-level восстановление выполняется через rematerialization/reconciliation в Goals 5 и 7.

## Scope

Scene локально владеет только nodes, hierarchy, transforms, per-node bounds, visibility/mobility metadata, dirty bookkeeping и spatial query/snapshot behavior. `ISceneProjectionQueue` не принадлежит Scene и проверяется в Runtime Support. Physics, Animation и другие majors взаимодействуют со Scene через Support adapters, а не через peer dependencies.

## Стабильность

После прохождения Goal 3.7 и canonical evidence convergence модуль имеет статус `LOCAL_READY`. `SYSTEM_READY` и `FROZEN` присваиваются только после обязательных системных gates Goals 5–9.

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
