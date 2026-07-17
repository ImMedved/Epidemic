# Persistence

## Purpose

Stores persistent object records and lightweight persistence bookkeeping primitives.

## Includes

- persistent object records
- dirty tracker
- tombstone store
- zone override store
- lazy rule records
- save transaction and persistence store contracts
- ordered persistence operation log
- backend load/save/flush port

## Excludes

- world placement authority
- serialization file format
- gameplay save rules

## Public Contracts

- `PersistentObjectRecord`
- `IDirtyTracker`
- `ITombstoneStore`
- `IZoneOverrideStore`
- `IPersistentObjectStore`
- `ISaveTransaction`
- `IPersistenceStore`
- `IPersistenceBackend`
- `PersistenceSnapshot`
- `PersistenceOperation`
- `PersistenceServices`, `CreatePersistenceServices`

## Forbidden Dependencies

No dependency on `World`, `Serialization` or `Assets`.

## States

Persistence state includes `Clean`, `Dirty`, `PendingSave`, `Saving`, `Saved`, `Deleted`, `Tombstoned`, `Conflict` and `Corrupted`.

## How To Use

Store persistent records and auxiliary save data in the persistence store, then open save transactions through the store when needed.

Transactions capture `base_revision` when opened. Commit fails with `persistence.conflict` when the store revision changed before commit. A transaction commit validates every operation before applying anything, then increments store revision once and stamps affected records with that revision.

`DeleteObject(TombstoneRecord)` is atomic: it removes the active record, adds the tombstone and marks the id dirty in the same committed operation. `AddTombstone()` remains available for tombstone-only imports or zone data.

Lazy rules support `UpdateLazyRule()`, `RemoveLazyRule()` and `QueryDueLazyRules(GameTimePoint)`. Due queries return pending rules whose evaluation time is not greater than the query time, in deterministic order.

`IPersistenceBackend` is a backend-neutral port for loading, saving and flushing `PersistenceSnapshot`. The in-memory backend is the reference implementation; concrete file/database backends plug in through the same interface.

## Example

```cpp
auto services = CreatePersistenceServices({}).Value();
auto tx = services.store->OpenTransaction();
tx->UpsertObject(record);
auto commit = tx->Commit();
```

## Testing Strategy

Validate dirty tracking, tombstones, zone overrides, lazy rules, object storage, revision conflicts, atomic delete+tombstone, ordered operations, backend load/save/flush and save transaction transitions.
