# Streaming

## Назначение

Streaming координирует residency targets под byte/CPU budget. Он объединяет demands нескольких consumers, выполняет progressive load plan, commit/rollback и управляет жизненным циклом request от `Requested` до unload. Сам major не знает World, Resources или Persistence.

## Контракты

`IStreamingRuntime` принимает demands, выполняет Tick и предоставляет progress/statistics. `IStreamingQuery` является read-only view, `IStreamingController` отвечает за cancellation и terminal shutdown. `StreamingDependencies` содержит data/commit/priority/residency/world/persistence/resource ports; concrete Support adapter передается через обычный public factory.

Несколько consumers одного target получают отдельные demand handles, но разделяют request. Во время необратимого `Unloading` существует не более одного successor. Последний released demand отменяет waiting successor. Terminal history не удаляет mapping, если он уже принадлежит более новому request.

## Standard Support integration

Standard Support adapter связывает Streaming с World, Resources и Persistence через neutral manifest. Он начинает загрузку только тогда, когда сам переводит World chunk `Unloaded → Loading`, поэтому rollback не отменяет чужой transition. Подготовленные Resource leases становятся active только после commit. При unload сначала выполняется переход в `Unloading`, затем освобождаются active leases и только после этого World становится `Unloaded`.

Persistence override читается как detached data и доступен через Support `IStreamingPreparedChunkDataQuery`, пока chunk находится в подготовленном/загруженном lifetime. Streaming major не интерпретирует gameplay payload persistence records.

## Shutdown

После начала shutdown новые demands отклоняются. Failed rollback/unload сохраняет ownership для следующего вызова. Успешный shutdown означает отсутствие live demands/requests и временного/resident ownership.

## Стабильность

State machine и public dependencies считаются frozen. Cross-major policy остается в Support.
