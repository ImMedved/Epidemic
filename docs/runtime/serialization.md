# Serialization

## Purpose

Defines structured archive contracts, serializer registration and schema migration building blocks.

## Includes

- archive reader/writer interfaces
- opaque serialized documents
- serializer and serializer registry
- migration and migration registry
- migration executor
- schema version and serialization errors

## Excludes

- persistence storage orchestration
- asset/resource ownership
- world-specific formats

## Public Contracts

- `IArchiveReader`, `IArchiveWriter`
- `SerializedDocument`
- `ISerializer`
- `SerializerRegistry`
- `IMigration`
- `MigrationRegistry`
- `ApplyMigrations`
- `SchemaVersion`
- `SerializationServices`, `CreateSerializationServices`

## Forbidden Dependencies

No dependency on `Assets`, `Persistence` or `World`.

## States

Serialization state covers schema known/unknown, reading, writing, migrating, valid and failed paths.

## How To Use

Write record fields through an archive writer, read them through a reader and register serializers or migrations by stable type id.

`SerializedDocument` exposes only metadata getters: type id, schema version and format version. The in-memory object/array/value tree is an implementation detail and is not part of the public contract.

Serializer callers should use `SerializeObject()` and `DeserializeObject()`. These helpers verify the serializer C++ type before crossing into the protected type-erased implementation.

Archive writers reject duplicate fields and become immutable after `Finalize()`.

Migrations are applied with `ApplyMigrations(document, target, registry, archive_factory)`. The executor builds a migration path, validates metadata at each step, publishes a new document for every step and leaves the original document unchanged.

## Example

```cpp
registry.RegisterSerializer(serializer);
migrations.RegisterMigration(migration);

auto writer = services.archives->CreateWriter();
SerializeObject(*serializer, object, *writer);
auto document = writer->Finalize(serializer->GetTypeId(), serializer->GetSchemaVersion());

auto migrated = ApplyMigrations(document.Value(), target_version, *services.migrations, *services.archives);
```

## Testing Strategy

Validate primitive and nested archive behavior, bytes, null, malformed archives, duplicate field rejection, finalize immutability, typed serializer rejection, duplicate serializer rejection, migration chains, missing/cyclic/ambiguous migrations and migration executor immutability.
    
