# Streaming

## Назначение

Streaming координирует residency targets под byte/CPU budget. Он объединяет demands нескольких consumers, выполняет progressive load plan, commit/rollback и управляет жизненным циклом request от `Requested` до unload. Сам major не знает World, Resources или Persistence.

## Контракты

`IStreamingRuntime` принимает demands, выполняет Tick и предоставляет progress/statistics. `IStreamingQuery` является read-only view, `IStreamingController` отвечает за cancellation и terminal shutdown. `StreamingDependencies` содержит data/commit/priority/residency/world/persistence/resource ports; concrete Support adapter передается через обычный public factory.

Несколько consumers одного target получают отдельные demand handles, но разделяют request. Во время необратимого `Unloading` существует не более одного successor. Последний released demand отменяет waiting successor. Terminal history не удаляет mapping, если он уже принадлежит более новому request.

`ProgressiveLoadPlan` начинает выполнение с cursor zero и не принимает pre-processed steps. `Commit` может отсутствовать, но если присутствует, он обязан быть единственным и последним шагом. Это гарантирует, что external commit не произойдёт до полного завершения progressive plan.

`IStreamingDataSource::ExecuteStep` имеет явную acceptance semantics. Failure/exception означает, что logical step unit не был принят. Success принимается Runtime ровно один раз. Принятый `StreamingStepResult` сначала сохраняется во внутреннем fixed-size pending state вместе с cursor и budget identity. Если после этого локальная publication не завершилась, следующий Tick продолжает suffix из pending result и не вызывает `ExecuteStep` повторно. `completed=false` разрешает следующий callback того же cursor только после успешного accounting предыдущего partial result.

Runtime-owned allocation failure при подготовке Tick, включая work list и result capacity, пробрасывается как `std::bad_alloc` до mutation request/statistics. Такой fault не конвертируется в `streaming.runtime_exception` и не увеличивает semantic `failed`. External port exceptions по-прежнему содержатся на своих узких callback boundaries.

## Standard Support integration

Standard Support adapter связывает Streaming с World, Resources и Persistence через neutral manifest. Он начинает загрузку только тогда, когда сам переводит World chunk `Unloaded → Loading`, поэтому rollback не отменяет чужой transition. Подготовленные Resource leases становятся active только после commit. При unload сначала выполняется переход в `Unloading`, затем освобождаются active leases и только после этого World становится `Unloaded`.

Persistence override читается как detached data и доступен через Support `IStreamingPreparedChunkDataQuery`, пока chunk находится в подготовленном/загруженном lifetime. Streaming major не интерпретирует gameplay payload persistence records.

## Shutdown

После начала shutdown новые demands отклоняются. Failed rollback/unload сохраняет ownership для следующего вызова. Успешный shutdown означает отсутствие live demands/requests и временного/resident ownership. Resource release и residency unload имеют durable phase markers, поэтому успешно завершённый cleanup prefix не выполняется повторно после более позднего failure.

## Стабильность

Модуль прошёл локальный Goal 3 Streaming audit и canonical evidence convergence и имеет статус `LOCAL_READY` для перехода к системным Goals 5–9. Статус `FROZEN` здесь не утверждается до whole-engine qualification. Cross-major policy остаётся в Support.
