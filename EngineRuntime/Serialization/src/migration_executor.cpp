#include "Epidemic/Runtime/Serialization/migration_executor.h"

#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/migration.h"
#include "Epidemic/Runtime/Serialization/migration_registry.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"

#include <exception>
#include <memory>
#include <string_view>

namespace epidemic::runtime
{
namespace
{
template <typename TValue>
[[nodiscard]] foundation::Result<TValue> MigrationExecutorFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<TValue>::Failure(CreateSerializationError(code, message));
}

[[nodiscard]] foundation::Result<void> ValidateMigrationOutput(const SerializedDocument& document, const MigrationKey& key)
{
    if (!document.IsValid())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.invalid_output", "migration produced an invalid document"));
    }
    if (document.GetTypeId() != key.type_id)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.type_changed", "migration changed the document type id"));
    }
    if (!(document.GetSchemaVersion() == key.to))
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.version_mismatch", "migration produced an unexpected schema version"));
    }
    if (document.GetFormatVersion() != 1u)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.migration.invalid_format", "migration produced an unsupported archive format version"));
    }
    return foundation::Result<void>::Success();
}
} // namespace

foundation::Result<SerializedDocument> ApplyMigrations(
    const SerializedDocument& document,
    SchemaVersion target,
    const IMigrationRegistry& migrations,
    const IArchiveFactory& archives)
{
    if (!document.IsValid() || !document.GetTypeId().IsValid())
    {
        return MigrationExecutorFailure<SerializedDocument>("serialization.invalid_document",
                                                            "migration input document must be valid and typed");
    }
    if (document.GetSchemaVersion() == target)
    {
        return foundation::Result<SerializedDocument>::Success(document);
    }

    const auto path = migrations.FindMigrationPath(document.GetTypeId(), document.GetSchemaVersion(), target);
    if (!path)
    {
        return foundation::Result<SerializedDocument>::Failure(path.GetError());
    }

    SerializedDocument current = document;
    SchemaVersion current_version = document.GetSchemaVersion();
    for (const std::shared_ptr<const IMigration>& migration : path.Value())
    {
        if (!migration)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.null",
                                                                "migration path contains a null migration");
        }

        MigrationKey key{};
        try
        {
            key = migration->GetKey();
        }
        catch (const std::exception& error)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.exception", error.what());
        }
        catch (...)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.exception",
                                                                "migration metadata callback threw an unknown exception");
        }

        if (key.type_id != document.GetTypeId() || !(key.from == current_version))
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.metadata_mismatch",
                                                                "migration metadata does not match the current document");
        }

        foundation::Result<std::unique_ptr<IArchiveReader>> reader =
            foundation::Result<std::unique_ptr<IArchiveReader>>::Failure(CreateSerializationError("serialization.internal", "uninitialized reader"));
        try
        {
            reader = archives.CreateReader(current);
        }
        catch (const std::exception& error)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_exception", error.what());
        }
        catch (...)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_exception",
                                                                "archive factory threw an unknown exception creating a reader");
        }
        if (!reader)
        {
            return foundation::Result<SerializedDocument>::Failure(reader.GetError());
        }

        std::unique_ptr<IArchiveWriter> writer;
        try
        {
            writer = archives.CreateWriter();
        }
        catch (const std::exception& error)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_exception", error.what());
        }
        catch (...)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_exception",
                                                                "archive factory threw an unknown exception creating a writer");
        }
        if (!writer)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_failed",
                                                                "archive factory returned a null writer");
        }

        foundation::Result<void> applied = foundation::Result<void>::Failure(CreateSerializationError("serialization.internal", "uninitialized migration result"));
        try
        {
            applied = migration->Apply(*reader.Value(), *writer);
        }
        catch (const std::exception& error)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.exception", error.what());
        }
        catch (...)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.exception",
                                                                "migration body threw an unknown exception");
        }
        if (!applied)
        {
            return foundation::Result<SerializedDocument>::Failure(applied.GetError());
        }

        const auto finalized = writer->Finalize(key.type_id, key.to);
        if (!finalized)
        {
            return foundation::Result<SerializedDocument>::Failure(finalized.GetError());
        }
        const auto valid_output = ValidateMigrationOutput(finalized.Value(), key);
        if (!valid_output)
        {
            return foundation::Result<SerializedDocument>::Failure(valid_output.GetError());
        }

        current = finalized.Value();
        current_version = key.to;
    }

    if (!(current_version == target))
    {
        return MigrationExecutorFailure<SerializedDocument>("serialization.migration.metadata_mismatch",
                                                            "migration path did not produce the requested target version");
    }
    return foundation::Result<SerializedDocument>::Success(current);
}
} // namespace epidemic::runtime
