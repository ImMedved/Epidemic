# Использование EngineRuntime

## Общая модель

До завершения `Runtime Support` каждый major можно создавать отдельно через public factory. Это удобно для unit tests, tools и ручной composition. В готовой игре composition root должен получать один `EngineRuntimeServices`, подготовленный атомарно Support.

Service bundle содержит interfaces, а не concrete implementation. Код хранит только те interfaces, которые реально нужны.

```cpp
auto scene_result =
    epidemic::runtime::CreateSceneServices();

if (!scene_result)
{
    return scene_result.GetError();
}

epidemic::runtime::SceneServices scene =
    std::move(scene_result.Value());
```

## Assets и Resources

Assets отвечает на вопрос «что это за данные и где они находятся». Resources отвечает на вопрос «загружены ли они сейчас и кто удерживает payload».

Сначала metadata регистрируется через `IAssetCatalogWriter`. Затем resource loader связывает `ResourceType` с чтением artifact, а consumer получает lease:

```cpp
epidemic::runtime::ResourceRequest request;
request.resource_id = resource_id;
request.type = resource_type;

auto lease_result =
    resources.manager->RequestLease(request);

if (!lease_result)
{
    return lease_result.GetError();
}

auto lease = lease_result.Value();

auto processed =
    resources.manager->ProcessPendingLoads(frame_budget);

if (!processed)
{
    return processed.GetError();
}

auto state =
    resources.manager->GetState(lease.handle);

if (state == epidemic::runtime::ResourceState::Ready)
{
    auto payload =
        resources.manager->GetPayload(lease.handle);
}

resources.manager->Release(lease);
```

Handle можно использовать для query, но release выполняется только исходным `ResourceLease`.

## Scene и World

Scene хранит пространственное представление, hierarchy и bounds. World хранит смысл объекта: region/chunk, placement, reality level, residency и persistence tier. Один игровой объект обычно имеет `RuntimeObjectId`, а его визуальное/физическое представление связывается с `SceneNodeId` через adapter верхнего слоя.

World изменяется командами с ожидаемой revision:

```cpp
epidemic::runtime::WorldObjectRecord record;
record.runtime_id = object_id;
record.asset_id = asset_id;
record.reality =
    epidemic::runtime::ObjectRealityLevel::Logical;
record.residency =
    epidemic::runtime::ResidencyState::Resident;
record.persistence_tier =
    epidemic::runtime::PersistenceTier::Disposable;
record.placement = epidemic::runtime::WorldSurfacePlacement{
    .region = region_id,
    .chunk = chunk_id,
    .transform = initial_transform,
};
record.revision = 1;

auto created = world.writer->Apply(
    epidemic::runtime::CreateObjectCommand{
        .record = record,
    });
```

Следующая команда использует revision из `WorldCommandResult`. Значение `0` не является публичным wildcard.

Scene transform изменяется через registry. Когда внешний backend сообщает world transform, применяется `SetWorldTransform()`, а Scene самостоятельно вычисляет local transform относительно parent.

## Persistence

Изменения долговечного состояния группируются в transaction:

```cpp
auto transaction =
    persistence.store->OpenTransaction();

transaction->UpsertObject(record);
transaction->UpsertLazyRule(rule);

auto committed =
    transaction->Commit();

if (!committed)
{
    transaction->Rollback();
}
```

Backend получает атомарный `CommitSnapshot`. Query interface не открывает transaction и безопасен для read-only consumers. Administrative transaction используется только restore, import и migration code.

## Time и Environment

Time получает real frame delta и возвращает authoritative game-time result:

```cpp
auto advanced =
    time.runtime->Advance(real_delta);

if (!advanced)
{
    return advanced.GetError();
}

const auto& time_result = advanced.Value();
```

`TimeAdvanceResult` содержит предыдущий и текущий snapshot и события перехода дня, фазы или сезона. Upper code не рассчитывает второй независимый game delta.

Environment регистрирует регион целиком: weather, season и climate. Current weather отделена от климатических средних. Surface state принадлежит одному region и не переносится неявно.

## Streaming

Consumer создает demand и хранит его, пока chunk действительно нужен:

```cpp
auto demand =
    streaming.runtime->Request(
        epidemic::runtime::streaming::ChunkStreamingTarget{chunk},
        epidemic::runtime::streaming::StreamingPriorityClass::High);

streaming.runtime->SetBudget(streaming_budget);
auto tick = streaming.runtime->Tick();

streaming.runtime->ReleaseDemand(demand.Value());
```

Несколько consumers одного target получают разные demand handles, но могут разделять один request. Controller API используется composition/shutdown code, а не обычным consumer.

## Physics, Animation и Audio

Эти modules не читают Scene или Resources напрямую. Их dependencies реализуются adapters.

Physics получает world transforms через `IPhysicsTransformSource` и публикует новые transforms через sink. Animation получает skeletons и clips через `IAnimationResourceSource` и публикует immutable pose buffer. Audio получает typed clip resource и transform source, а backend владеет voices.

Кадровая длительность во всех трех модулях — `RuntimeFrameDuration`, а не игровое календарное время.

## Simulation

GameFramework передает конкретный `ISimulationJob`, который выполняет ограниченную часть работы:

```cpp
auto job =
    std::make_shared<MyPopulationSimulationJob>();

auto handle =
    simulation.scheduler->SubmitJob(
        job,
        metadata);

simulation.scheduler->SetBudget(simulation_budget);

auto tick =
    simulation.scheduler->Tick();
```

Job не изменяет World или gameplay domain напрямую. Он возвращает `SimulationProposalBatch`. Main-thread phase публикует и commit-ит proposals через `ISimulationCommitTarget`.

Scheduled task привязывает уже существующий job к `SimulationTime`. World memory хранит события, abstract facts — обобщенные утверждения, а Attention/Relevance выбирают необходимую детализацию.

## Ошибки и владение

Borrowed values действуют только во время вызова. Immutable snapshots и payloads можно передавать между systems через `shared_ptr<const T>`. Long-lived external backend и adapter передаются через `shared_ptr`.

Любой handle нужно считать stale после destruction или generation mismatch. Query methods возвращают `Result` или `optional` согласно contract; sentinel state не используется вместо ошибки stale handle.

Shutdown начинается с запрета новой работы. Failed cleanup не должен терять backend handles или leases; повторный shutdown завершает оставшиеся операции.

## Будущий GameFramework

GameFramework строит поверх Runtime конкретные systems: управление персонажем и кораблем, инвентарь, владение, преступления, фракции, NPC, экономика, квесты и диалоги. Он связывает `RuntimeObjectId` с собственными domain records, но не добавляет gameplay fields в World, Scene или Resources.

Правильная зависимость:

```text
EngineBase
    <- EngineRuntime
        <- GameFramework
            <- Game
```
