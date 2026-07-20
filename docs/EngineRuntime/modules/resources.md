# Resources

## Назначение

Resources загружает immutable payloads по `ResourceId`, управляет dependencies, memory budget, retries, cancellation, eviction и владением consumer. В отличие от Assets, Resource существует в конкретном runtime state и может находиться в очереди, загрузке, Ready, Failed или Evicted.

## Владение

Consumer получает `ResourceLease`. Lease имеет собственный acquisition ID и освобождается один раз через `IResourceManager::Release()`. `ResourceHandle` используется для query и не дает права освободить чужое удержание. Dependency loads также принадлежат leases, поэтому rollback одной операции не снимает удержание другого consumer.

## Публичные контракты

`IResourceLoaderRegistry` связывает `ResourceType` с `IResourceLoader`. Loader возвращает `ResourceLoadArtifact` с immutable `IResourcePayload`. `IResourceManager` принимает requests, обрабатывает pending jobs под budget, возвращает state/payload/statistics и освобождает leases. `ResourceDependencyGraph` защищает от cycles и дублирования.

## Инварианты

Artifact ID и type должны совпадать с request. Null payload запрещен. Byte budget является soft budget текущего frame: начатая операция может завершить минимальную единицу работы, после чего новые jobs не берутся. Memory accounting использует overflow-safe arithmetic. Internal release failure записывается как invariant violation.

## Стабильность

Модуль frozen. Concrete decoders и loaders добавляются сверху без изменения manager API.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `resource_dependency_graph.h`: `ResourceDependency`, `ResourceDependencySet`.
- `resource_handle.h`: `ResourceHandle`, `ResourceLease`.
- `resource_loader.h`: `ResourceLoadArtifact`, `IResourceLoader`.
- `resource_loader_registry.h`: `IResourceLoaderRegistry`.
- `resource_manager.h`: `ResourceProcessingStats`, `ResourceMemoryStats`, `IResourceManager`.
- `resource_payload.h`: `IResourcePayload`, `ByteResourcePayload`.
- `resource_request.h`: `ResourceRequest`.
- `resource_result.h`: factory-функции или backend implementation без отдельного публичного типа.
- `resource_services.h`: `ResourceOptions`, `ResourceServices`.
- `resource_state.h`: `ResourceState`.
- `resource_type.h`: `ResourceType`.
