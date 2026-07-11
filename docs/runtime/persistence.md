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

## Forbidden Dependencies

No dependency on `World`, `Serialization` or `Assets`.

## States

Persistence state includes `Clean`, `Dirty`, `PendingSave`, `Saving`, `Saved`, `Deleted`, `Tombstoned`, `Conflict` and `Corrupted`.

## How To Use

Store persistent records and auxiliary save data in the persistence store, then open save transactions through the store when needed.

## Example

```cpp
auto tx = persistence_store.OpenTransaction();
persistence_store.Objects().Upsert(record);
```

## Testing Strategy

Validate dirty tracking, tombstones, zone overrides, lazy rules, object storage and save transaction transitions.
