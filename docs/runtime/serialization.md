# Serialization

## Purpose

Defines structured archive contracts, serializer registration and schema migration building blocks.

## Includes

- archive reader/writer interfaces
- in-memory archive implementation
- serializer and serializer registry
- migration and migration registry
- schema version and serialization errors

## Excludes

- persistence storage orchestration
- asset/resource ownership
- world-specific formats

## Public Contracts

- `IArchiveReader`, `IArchiveWriter`
- `ISerializer`
- `SerializerRegistry`
- `IMigration`
- `MigrationRegistry`
- `SchemaVersion`

## Forbidden Dependencies

No dependency on `Assets`, `Persistence` or `World`.

## States

Serialization state covers schema known/unknown, reading, writing, migrating, valid and failed paths.

## How To Use

Write record fields through an archive writer, read them through a reader and register serializers or migrations by stable type id.

## Example

```cpp
registry.RegisterSerializer(&serializer);
migrations.RegisterMigration(&migration);
```

## Testing Strategy

Validate primitive and nested archive behavior, duplicate serializer rejection and duplicate migration rejection.
    