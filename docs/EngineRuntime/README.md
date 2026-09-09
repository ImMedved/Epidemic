# EngineRuntime

`EngineRuntime` — стабильный слой систем движка между `EngineBase` и будущим `GameFramework`. Он предоставляет механизмы большой бесшовной игры, но не содержит конкретных gameplay-правил и контента.

Runtime состоит из независимых CMake-библиотек. `RuntimeFoundation` задает общие IDs, budgets, frame/game time и spatial values. `Assets` описывает идентичность и расположение данных, `Resources` управляет загрузкой payloads и leases. `Serialization` и `Persistence` отвечают за versioned данные и долговечное состояние. `Time` и `Environment` формируют время и нейтральное состояние мира. `Scene` хранит пространственное представление, `World` — логическое положение и reality/residency/persistence state, а `Streaming` управляет residency chunks. Renderer, Physics, Navigation, Animation и Audio предоставляют backend-neutral boundaries. `Simulation` выполняет budgeted jobs и публикует proposals вместо прямого изменения чужих domains.

Один Runtime-major не зависит от другого major. Межмодульные технические связи находятся только в `Support`. Благодаря этому Physics можно развивать без Renderer, Audio без World, Navigation без Simulation и так далее. Полная игра получает связанную конфигурацию через `EngineRuntimeServices`.

## Support и композиция

`EngineRuntime/Support` является официальным composition layer. `PrepareEngineRuntime()` сначала создает все service bundles и adapters во временном aggregate, не меняя `Application`. `CommitPreparedRuntime()` регистрирует готовый `EngineRuntimeServices` только после успешной подготовки. Поэтому late factory failure не оставляет приложение частично собранным.

Reference profile предоставляет нейтральные reference implementations там, где они нужны для полного runtime baseline. Production profile требует внешние production backends для Renderer, Physics, Navigation, Animation, Audio и Simulation commit target, но стандартные технические связи между Runtime majors по-прежнему собирает Support. Для Streaming Production может использовать стандартный World/Resources/Persistence adapter с переданным `IChunkStreamingManifestSource` либо полный custom override всех Streaming integration roles.

Coordinator выполняет кадр по фиксированным фазам, собирает recoverable phase failures и не прекращает независимые последующие phases из-за локальной ошибки одного major. Shutdown сначала запрещает новую работу, затем best-effort освобождает все owned lifetimes; failed cleanup сохраняется для повторного `Shutdown()`.

## Граница слоя

Runtime не содержит actors, inventory, factions, crime, quests, dialogue, конкретных NPC, корабли, экономические правила или сюжет. Такие понятия должны появляться в `GameFramework` или `Game` и использовать Runtime через его нейтральные contracts.

Публичные contracts Runtime считаются frozen после финального validation. Допустимы исправления воспроизводимых ошибок, новые backend implementations и обратно совместимые расширения. Не следует менять Runtime только ради удобства конкретной gameplay-системы.

Практические сценарии находятся в [`using_engineruntime.md`](using_engineruntime.md), а документация каждого major — в [`modules/`](modules/).
