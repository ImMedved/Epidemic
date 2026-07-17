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
- durability modes for memory-only, save-required and save-and-flush-required commits

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
- `PersistenceDurability`
- `PersistenceOperation`
- `PersistenceServices`, `CreatePersistenceServices`

## Forbidden Dependencies

No dependency on `World`, `Serialization` or `Assets`.

## States

Persistence state includes `Clean`, `Dirty`, `PendingSave`, `Saving`, `Saved`, `Deleted`, `Tombstoned`, `Conflict` and `Corrupted`.

## How To Use

Store persistent records and auxiliary save data in the persistence store, then open save transactions through the store when needed.

Transactions capture `base_revision` when opened. Commit fails with `persistence.conflict` when the store revision changed before commit. A transaction commit builds a candidate snapshot, validates that candidate, performs required backend `Save()` and `Flush()` operations, and only then publishes the candidate to the in-memory store. Backend failure leaves the current store unchanged and moves the transaction to `Failed`.

`DeleteObject(TombstoneRecord)` is the normal runtime deletion API. It is atomic: it verifies the active record exists, removes it, adds the tombstone and marks the id dirty in the same committed operation.

Raw object removal and tombstone-only insertion are administrative/import operations exposed as `AdminRemoveObject()` and `AdminAddTombstone()`. Normal runtime deletion should use `DeleteObject()`.

Lazy rules support `UpdateLazyRule()`, `RemoveLazyRule()` and `QueryDueLazyRules(GameTimePoint)`. Due queries return pending rules whose evaluation time is not greater than the query time, in deterministic order.

`IPersistenceBackend` is a backend-neutral port for loading, saving and flushing `PersistenceSnapshot`. Backend-loaded snapshots are validated before publication; invalid snapshots fail service creation with `persistence.invalid_snapshot`. The in-memory backend is the reference implementation; concrete file/database backends plug in through the same interface.

Durability policy:

- `MemoryOnly`: commit publishes only to memory.
- `SaveRequired`: commit calls backend `Save(candidate)` before publishing.
- `SaveAndFlushRequired`: commit calls `Save(candidate)` and then `Flush()` before publishing.

## Example

```cpp
auto services = CreatePersistenceServices({}).Value();
auto tx = services.store->OpenTransaction();
tx->UpsertObject(record);
auto commit = tx->Commit();
```

## Testing Strategy

Validate dirty tracking, tombstones, zone overrides, lazy rules, object storage, revision conflicts, atomic delete+tombstone, ordered operation semantics, staged validation, backend load validation, save/flush failure atomicity and save transaction transitions.
