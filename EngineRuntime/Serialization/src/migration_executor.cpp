#include "Epidemic/Runtime/Serialization/migration_executor.h"

#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/migration.h"
#include "Epidemic/Runtime/Serialization/migration_registry.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"

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

        const MigrationKey key = migration->GetKey();
        if (key.type_id != document.GetTypeId() || !(key.from == current_version))
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.migration.metadata_mismatch",
                                                                "migration metadata does not match the current document");
        }

        auto reader = archives.CreateReader(current);
        if (!reader)
        {
            return foundation::Result<SerializedDocument>::Failure(reader.GetError());
        }

        auto writer = archives.CreateWriter();
        if (!writer)
        {
            return MigrationExecutorFailure<SerializedDocument>("serialization.archive_factory_failed",
                                                                "archive factory returned a null writer");
        }

        const auto applied = migration->Apply(*reader.Value(), *writer);
        if (!applied)
        {
            return foundation::Result<SerializedDocument>::Failure(applied.GetError());
        }

        const auto finalized = writer->Finalize(key.type_id, key.to);
        if (!finalized)
        {
            return foundation::Result<SerializedDocument>::Failure(finalized.GetError());
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
