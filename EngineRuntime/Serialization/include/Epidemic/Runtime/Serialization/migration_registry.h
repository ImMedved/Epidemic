#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/migration.h"

namespace epidemic::runtime
{
class IMigrationRegistry
{
  public:
    virtual ~IMigrationRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterMigration(IMigration& migration) = 0;
    [[nodiscard]] virtual IMigration* FindMigration(const MigrationKey& key) = 0;
    [[nodiscard]] virtual const IMigration* FindMigration(const MigrationKey& key) const = 0;
    [[nodiscard]] virtual bool HasMigration(const MigrationKey& key) const = 0;
};
} // namespace epidemic::runtime
