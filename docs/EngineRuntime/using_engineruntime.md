# Использование EngineRuntime

## Полный Runtime

В обычном приложении Runtime собирается через Support. Сначала создается конфигурация и внешние production/reference dependencies, затем весь Runtime подготавливается без изменения `Application`:

```cpp
using namespace epidemic::runtime;

EngineRuntimeOptions options{};
EngineRuntimeDependencies dependencies{};

auto prepared = PrepareEngineRuntime(options, dependencies);
if (!prepared)
{
    return prepared.GetError();
}

auto committed = CommitPreparedRuntime(
    application,
    std::move(prepared.Value()));

if (!committed)
{
    return committed.GetError();
}

EngineRuntimeServices runtime = std::move(committed.Value());
```

`RegisterDefaultEngineRuntime()` выполняет те же два шага одной функцией. Раздельная форма полезна, когда composition root хочет проверить или дополнить конфигурацию до регистрации aggregate.

Верхний слой хранит `EngineRuntimeServices` либо только необходимые service bundles. Он не ищет concrete implementations и не соединяет Runtime majors вручную.

## Кадр

Игровой executable передает coordinator только данные текущего кадра и budgets:

```cpp
RuntimeFrameInput frame{};
frame.real_delta = real_frame_delta;
frame.resource_budget = resource_budget;
frame.streaming_budget = streaming_budget;
frame.navigation_budget = navigation_budget;
frame.max_animators = animation_budget;

auto tick = runtime.coordinator->Tick(frame);
if (!tick)
{
    return tick.GetError();
}

for (const RuntimePhaseFailure& failure : tick.Value().failures)
{
    // Diagnostics / recovery policy upper layer.
}
```

Локальная recoverable ошибка одного major отражается в `failures`; coordinator продолжает независимые последующие phases. Fatal `Result` означает ошибку самого coordinator contract, например кадр после начала shutdown или некорректный frame input.

## Assets и Resources

Assets отвечает за identity/location/metadata, Resources — за фактически загруженный payload и ownership.

```cpp
ResourceRequest request{};
request.resource_id = resource_id;
request.type = resource_type;

auto lease_result = runtime.resources->manager->RequestLease(request);
if (!lease_result)
{
    return lease_result.GetError();
}

ResourceLease lease = lease_result.Value();

auto processed = runtime.resources->manager->ProcessPendingLoads(frame_budget);
if (!processed)
{
    return processed.GetError();
}

if (runtime.resources->manager->GetState(lease.resource) == ResourceState::Ready)
{
    auto payload = runtime.resources->manager->GetPayload(lease.resource);
    // Использовать immutable payload.
}

auto released = runtime.resources->manager->Release(lease);
```

`ResourceHandle` используется для query, но не выражает ownership. Освободить ресурс имеет право только владелец соответствующего `ResourceLease`.

## Scene и World

Scene хранит пространственное представление и hierarchy. World хранит логическое состояние объекта, region/chunk placement, reality, residency и persistence tier. Gameplay record верхнего слоя обычно связывается с Runtime через `RuntimeObjectId`, но Runtime не получает gameplay fields.

World меняется revisioned commands. Следующая команда использует revision из предыдущего `WorldCommandResult`; `expected_revision == 0` не является wildcard.

Physics записывает world-space transforms не напрямую в Scene во время simulation step, а через Support projection queue. Scene применяет их в отдельной frame phase и самостоятельно вычисляет local transform относительно parent.

## Persistence

Долговечные изменения группируются в transaction:

```cpp
auto transaction = runtime.persistence->store->OpenTransaction();
transaction->UpsertObject(record);
transaction->UpsertLazyRule(rule);

auto committed = transaction->Commit();
if (!committed)
{
    transaction->Rollback();
}
```

Read-only consumers используют `IPersistenceQuery`. Administrative transaction предназначен только для restore/import/migration operations.

## Time и Environment

`Time` является единственным authoritative источником game time. Upper code не вычисляет параллельный game delta. `Environment` хранит нейтральное состояние region/weather/season/surfaces и revisions; gameplay-реакция на это состояние реализуется выше Runtime.

## Streaming

Consumer создает demand и хранит его ровно пока target нужен:

```cpp
auto demand = runtime.streaming->runtime->Request(
    streaming::StreamingTarget{streaming::ChunkStreamingTarget{chunk}},
    streaming::StreamingPriorityClass::High);

// Coordinator выполняет progressive loading каждый кадр.

runtime.streaming->runtime->ReleaseDemand(demand.Value());
```

Несколько consumers имеют разные demand handles, но могут разделять один request. Standard Support adapter использует World, Resources и Persistence. Подготовленный detached persistence snapshot можно прочитать через `runtime.integrations->streaming_prepared_data` до unload/rollback chunk.

## Animation и Renderer

Animation получает skeleton/clip через resource adapter и публикует immutable pose. Pose содержит `RuntimeObjectId owner`; Support преобразует его в renderer-neutral pose, поэтому Renderer может добавить pose к submission proxy того же owner, не завися от Animation headers.

GameFramework создает animator и render proxy для одного domain object, но не переносит bone data между majors вручную.

## Audio

Audio получает typed `IAudioClipResource` через Support. Resource lease передается вместе с clip и остается активным, пока реальный voice/clip consumer существует. После уничтожения voice lease освобождается, поэтому ResourceManager может evict неиспользуемый звук.

## Simulation

GameFramework передает конкретный `ISimulationJob`, а Runtime только планирует его выполнение:

```cpp
auto handle = runtime.simulation->scheduler->SubmitJob(job, metadata);
runtime.simulation->scheduler->SetBudget(simulation_budget);
```

Job не изменяет World или gameplay state напрямую. Результат оформляется как `SimulationProposalBatch`, который публикуется и commit-ится main-thread phase через `ISimulationCommitTarget`. Scheduled tasks привязывают уже существующие jobs к `SimulationTime`.

## Production profile

В Production application передает настоящие внешние backends через `EngineRuntimeDependencies`: Renderer command sink, Physics backend, Navigation backend, Animation evaluator, Audio backend и Simulation commit target. Standard cross-major wiring остается обязанностью Support.

Для Streaming достаточно передать настоящий `IChunkStreamingManifestSource`, если используется стандартный World/Resources/Persistence adapter. Полная custom Streaming integration разрешена только когда переданы все core roles одновременно.

## Изолированное использование major

Unit tests, tools и subsystem experiments могут создавать отдельный major напрямую:

```cpp
auto scene = CreateSceneServices();
auto resources = CreateResourceServices();
```

Это специальный сценарий. Обычная игра использует `EngineRuntimeServices`, чтобы lifecycle и cross-major ownership оставались согласованными.

## Shutdown

Завершение Runtime выполняется через coordinator:

```cpp
auto shutdown = runtime.coordinator->Shutdown();
if (!shutdown)
{
    // После устранения внешней причины Shutdown можно повторить.
}
```

После начала shutdown coordinator больше не принимает кадры. Failed cleanup не теряет owned handles или leases; повторный вызов продолжает незавершенное освобождение. Успешный повторный shutdown идемпотентен.
