# Стабильность API EngineRuntime

## Граница freeze

EngineRuntime фиксирует source-level API для замороженных public contracts. ABI stability не объявляется.

Стабильная поверхность находится в public headers:

```text
EngineRuntime/*/include/Epidemic/Runtime/**
```

Private headers, файлы `src` и concrete in-memory classes не являются частью публичного контракта. Public headers не должны включать private headers другого major и не должны зависеть от `Support`.

`Support` пока не считается frozen major. Его текущая документация предварительная; окончательный контракт фиксируется отдельно после завершения composition, real adapters, coordinator Tick и retryable Shutdown.

## Стабильные сущности

После freeze callers могут полагаться на смысл public fields, state transitions и error codes для:

- typed ids, generation handles и явно задокументированных never-reuse ids;
- budgets, typed time values, `RuntimeFrameDuration` и spatial values;
- immutable snapshots с revisions для externally visible state;
- command, event, proposal, plan и payload records на границах modules;
- service bundles, public factories и backend extension interfaces;
- schema versions, migrations и persistence durability contracts.

Callers не должны полагаться на private container layout, allocation strategy, diagnostic text, mock backend internals или порядок internal queues, если он не описан как deterministic public behavior.

## Factories и profiles

Public factories являются стабильными entry points для ручной composition и tests. Production factories не должны скрыто включать mock-only behavior. Reference и mock behavior должны быть выбраны явно через reference/mock factory или profile.

`RegisterDefaultEngineRuntime` и `IEngineRuntimeCoordinator` относятся к `Support`; до завершения `Support` они не являются основанием считать весь composition contract frozen.

## Backend extension ports

Backend и integration interfaces являются public ports, а не private side calls. Новые реализации renderer, physics, navigation, animation, audio, streaming, persistence или simulation backend должны подключаться через эти ports и не обходить major ownership.

Runtime majors не командуют друг другом напрямую. Если один major нуждается в данных другого, данные проходят через immutable snapshot, projection, event buffer, command/proposal queue или adapter в composition layer.

## Save и schema guarantees

Serialization и Persistence изменения, влияющие на сохраненные данные, требуют одного из вариантов:

- migration path через public migration registry;
- schema-version bump с описанным compatibility limit;
- stable error code для явного отказа загрузить неподдерживаемый snapshot.

Persistence commit остается atomic с точки зрения caller: failed commit не публикует partial state как успешное durable состояние.

## Правила изменений после freeze

Breaking public API change требует:

- обновить этот документ и affected module doc;
- описать migration для callers и saved data, если она нужна;
- добавить focused tests для старого и нового state behavior;
- обновить architecture tests, если меняются dependency, handle, factory или snapshot rules;
- проверить отсутствие private cross-major includes и major-to-`Support` dependency.
