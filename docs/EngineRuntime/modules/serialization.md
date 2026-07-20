# Serialization

## Назначение

Serialization создает versioned immutable documents, регистрирует serializers и выполняет последовательные schema migrations. Он не решает, где хранится файл и какие world records должны сохраняться.

## Модель

`IArchiveWriter` строит tree из scalar values, objects, arrays, bytes и null. После finalize получается opaque `SerializedDocument`. `IArchiveReader` читает его без доступа к внутренней variant implementation. `ISerializer` связывает C++ type и schema, `ISerializerRegistry` хранит serializers, а migration registry/executor переводит документ между версиями.

## Инварианты

Document имеет valid type ID, поддерживаемый format version и non-null root. Duplicate object fields запрещены. Каждый migration step обязан сохранить type ID, вернуть точную целевую schema version и valid document. Migration chain должен быть непрерывным.

## Граница и стабильность

Serialization не выполняет persistence transactions и не знает World. Он frozen; новые serializers и migrations регистрируются через существующие ports.

## Карта публичных заголовков

Этот раздел служит быстрым индексом объявлений. Семантика и инварианты описаны выше; точные сигнатуры остаются источником истины в public headers.

- `archive_reader.h`: `IArchiveReader`.
- `archive_value.h`: `SerializedDocument`, `InMemoryArchiveReader`, `InMemoryArchiveWriter`, `InMemoryArchiveFactory`.
- `archive_writer.h`: `IArchiveWriter`.
- `migration.h`: `MigrationKey`, `IMigration`.
- `migration_executor.h`: `IArchiveFactory`, `IMigrationRegistry`.
- `migration_registry.h`: `IMigrationRegistry`.
- `schema_version.h`: `SchemaVersion`.
- `serialization_error.h`: factory-функции или backend implementation без отдельного публичного типа.
- `serialization_services.h`: `SerializationOptions`, `IArchiveFactory`, `SerializationServices`.
- `serializer.h`: `ISerializer`.
- `serializer_registry.h`: `ISerializerRegistry`.
