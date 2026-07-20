# Navigation

## Назначение

Navigation выполняет budgeted path queries по данным tiles, costs и dynamic obstacles. Он не определяет поведение NPC и не хранит gameplay goals.

## Контракты

`INavTileRegistry` управляет состояниями tiles. `INavigationRuntime` создает handle-based query, отменяет, выполняет Tick и читает state/result. `INavigationBackend` рассчитывает путь, `INavigationDataSource` предоставляет geometry/revision, `INavCostProvider` — нейтральную стоимость, а `INavigationObstacleSource` — obstacles.

Path result содержит revision источника. Изменение region/tile делает старый результат stale. Released и TTL-expired records удаляются.

## Инварианты

Raw query IDs не используются для lifetime operations. Backend result проверяет finite points, начало/конец и revision. Tile transitions валидируются. Reference backend является тестовым foundation, а не navmesh.

## Стабильность

Модуль frozen. Recast или другой backend подключается без изменения runtime API.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `navigation.h`: factory-функции или backend implementation без отдельного публичного типа.
- `navigation_runtime.h`: `INavCostProvider`, `INavigationBackend`, `INavigationDataSource`, `INavigationObstacleSource`, `INavTileRegistry`, `INavigationRuntime`, `NavigationServices`, `NavigationDependencies`.
- `navigation_types.h`: `NavTileId`, `PathQueryId`, `PathQueryHandle`, `DynamicObstacleId`, `NavTileState`, `PathQueryState`, `PathRequest`, `PathResult`, `NavigationRevision`, `NavCostQuery`, `DynamicObstacle`, `NavigationOptions`.
