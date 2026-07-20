#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/archive_value.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

namespace epidemic::runtime
{
class IArchiveFactory;
class IMigrationRegistry;

[[nodiscard]] foundation::Result<SerializedDocument> ApplyMigrations(
    const SerializedDocument& document,
    SchemaVersion target,
    const IMigrationRegistry& migrations,
    const IArchiveFactory& archives);
} // namespace epidemic::runtime
