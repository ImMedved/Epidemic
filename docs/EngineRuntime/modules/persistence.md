# Persistence

## Назначение

Persistence хранит долговечное состояние объектов, tombstones, lazy rules и zone overrides. Он обеспечивает атомарный commit snapshot и отделяет write transaction от read-only query.

## Модель

`IPersistenceStore` открывает `ISaveTransaction`. Transaction накапливает ordered operations и строит candidate snapshot. Backend получает `CommitSnapshot(snapshot, durability)`; при failure прежний durable snapshot остается authoritative, а in-memory store не публикует candidate.

`IPersistenceQuery` читает records без возможности открыть transaction. `IPersistenceAdministrativeTransaction` предназначен только restore, import и migration code. `IDirtyTracker` отражает unsaved changes.

## Данные

`PersistentObjectRecord` хранит payload, location, kind, protection и revision. Tombstone фиксирует уничтожение. `LazyRuleRecord` описывает отложенное изменение. `ZoneOverrideSnapshot` хранит локальные records и tombstones региона.

## Инварианты

Active record не может одновременно быть tombstoned. Lazy rule не ссылается на отсутствующий target. Delete удаляет связанные lazy rules. `UpdateLazyRule` не выполняет upsert. Snapshots сортируются детерминированно и проходят полную validation.

## Стабильность

Модуль frozen. Backend может быть memory, file или database implementation при сохранении atomic commit contract.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `dirty_tracker.h`: `IDirtyTracker`.
- `lazy_rule_record.h`: `LazyRuleKind`, `LazyRuleState`, `LazyRuleRecord`.
- `persistence_backend.h`: `PersistenceSnapshot`, `PersistenceDurability`, `IPersistenceBackend`.
- `persistence_location.h`: `PersistenceLocation`.
- `persistence_operation.h`: `UpsertObjectOperation`, `AdminRemoveObjectOperation`, `DeleteObjectOperation`, `AdminAddTombstoneOperation`, `UpsertLazyRuleOperation`, `UpdateLazyRuleOperation`, `RemoveLazyRuleOperation`, `UpsertZoneOverrideOperation`, `RemoveZoneOverrideOperation`.
- `persistence_payload.h`: `PersistencePayload`.
- `persistence_policy.h`: `ObjectProtectionFlags`, `ObjectProtectionMask`.
- `persistence_services.h`: `PersistenceOptions`, `PersistenceServices`.
- `persistence_state.h`: `PersistenceState`, `PersistentObjectKind`.
- `persistence_store.h`: `IPersistenceQuery`, `IPersistenceStore`.
- `persistent_object_store.h`: `IPersistentObjectStore`.
- `persistent_record.h`: `PersistentObjectRecord`.
- `save_transaction.h`: `SaveTransactionState`, `ISaveTransaction`, `IPersistenceAdministrativeTransaction`.
- `tombstone_store.h`: `TombstoneRecord`, `ITombstoneStore`.
- `zone_override_store.h`: `ZoneOverrideSnapshot`, `IZoneOverrideStore`.
