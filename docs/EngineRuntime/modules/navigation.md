# Navigation

## Назначение

Navigation выполняет budgeted path queries по данным tiles, costs и dynamic obstacles. Он не определяет поведение NPC, не хранит gameplay goals и не владеет Environment -> Navigation projection adapter. Этот adapter проверяется в Runtime Support.

## Tiles и revisions

`INavTileRegistry` принимает только valid `NavTileId`. Публичная регистрация допускает начальные состояния `Queued`, `Loading`, `Ready` и `Unloaded`; `Missing`, `Dirty`, `Rebuilding`, `Failed` и неизвестные enum values не являются допустимым initial state. Duplicate tile rejected.

`MarkTileDirty()` переводит `Ready -> Dirty`. Повторный `MarkTileDirty()` для уже `Dirty` является defined no-op и не увеличивает navigation revision. Rebuild выполняется детерминированно по возрастанию `NavTileId`: `Dirty -> Rebuilding -> Ready`. `max_items` ограничивает число state transitions за вызов. Tile rebuild не материализует byte payload, поэтому `max_bytes` сам по себе не ограничивает эти transitions. Navigation revision exhaustion проверяется до publication; при exhaustion tile state не меняется.

## Path query identity и lifecycle

`INavigationRuntime` создаёт только handle-based queries. `PathQueryId` и generation являются monotonic non-zero identities; exhaustion rejected до публикации record. Неверная generation, released handle и уже удалённый TTL record являются stale handle.

Query проходит `Pending -> Running -> PartiallyComplete`, после чего становится `Completed`, `Failed`, `Cancelled` либо `Stale`. Cancel разрешён для non-terminal query; повторный cancel уже `Cancelled` является no-op. Другие terminal states не отменяются повторно. `Failed`, `Cancelled` и сохранённый `Stale` читаются локально без повторных external callbacks.

Completed result может иметь TTL. После expiration state читается как `Stale`; физическое удаление record происходит в `Tick()`. Tick сначала полностью готовит purge list и processing list. Если подготовка любого списка получает allocation failure, live query set остаётся без изменений.

## Providers и backend boundary

`INavigationDataSource::CurrentRevision`, `INavCostProvider`, `INavigationObstacleSource` и `INavigationBackend` являются внешними neutral ports. Их exceptions не выходят как semantic success и не повреждают другие queries. Cost должен быть finite и non-negative. Reference path использует один подготовленный shape plan: blocking obstacle задаёт detour midpoint; если blocker отсутствует, cost > 1 добавляет deterministic midpoint. Один и тот же plan используется и для byte-budget decision, и для последующей публикации.

`INavigationBackend::BuildPath()` является observational/read-replay-safe query callback. Backend success принимается Runtime ровно один раз и сохраняется как durable pending result до локального commit. Поэтому недостаточный byte budget или Runtime-local allocation failure после callback не вызывают повторный `BuildPath()` для того же logical result. Backend failure/exception означает отсутствие принятого path result.

Backend result публикуется только если state = `Completed`, source revision совпадает с expected revision, path содержит минимум start и target, все points finite, первый point равен request start, последний point равен request target. Runtime переписывает handle и result revision своей authoritative identity.

## Budget semantics

`Tick()` обрабатывает queries детерминированно по возрастанию `PathQueryId`. `max_items` ограничивает accepted state transitions. `max_bytes` применяется к фактически подготовленному path shape. Для reference path это сохранённый point count. Для backend path используется фактическое число возвращённых points. Если path не помещается в текущий byte budget, query остаётся `PartiallyComplete`, а подготовленный provider/backend result сохраняется для последующего Tick без повторного external callback.

`PathQueryState::PartiallyComplete` означает deferred local completion. Отдельного `NoPath` enum нет: backend/provider semantic inability to produce a valid path приводит к `Failed`. Изменение captured source revision делает старый query/result `Stale`.

## Failure atomicity и scope

Runtime-owned allocation failure до semantic commit не превращается в `provider_exception` и не переводит query в ложный `Failed`. Подготовленный successful backend result или reference plan сохраняет ownership до publication. Result revision exhaustion проверяется до внешнего callback и не допускает side effect, который невозможно локально commit.

Navigation не имеет local `Shutdown()` API. Lifetime backend/providers принадлежит переданным dependencies. Environment -> Navigation projection и cross-major reconciliation относятся к `3.17 Support`, а не к локальному Navigation audit.

## Статус freeze

Этот документ описывает target contract для локального Goal 3 audit. После прохождения `EpidemicRuntimeNavigationTests` и freeze evidence module может получить `LOCAL_READY`. `SYSTEM_READY` и `FROZEN` присваиваются только после последующих system-wide Goals 5–9.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `navigation.h`: factory-функции или backend implementation без отдельного публичного типа.
- `navigation_runtime.h`: `INavCostProvider`, `INavigationBackend`, `INavigationDataSource`, `INavigationObstacleSource`, `INavTileRegistry`, `INavigationRuntime`, `NavigationServices`, `NavigationDependencies`.
- `navigation_types.h`: `NavTileId`, `PathQueryId`, `PathQueryHandle`, `DynamicObstacleId`, `NavTileState`, `PathQueryState`, `PathRequest`, `PathResult`, `NavigationRevision`, `NavCostQuery`, `DynamicObstacle`, `NavigationOptions`.
