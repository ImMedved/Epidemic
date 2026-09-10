#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/migration.h"

#include <memory>
#include <vector>

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IMigrationRegistry
{
  public:
    virtual ~IMigrationRegistry() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterMigration(std::shared_ptr<const IMigration> migration) = 0;
    [[nodiscard]] virtual foundation::Result<void> Freeze() = 0;
    [[nodiscard]] virtual bool IsFrozen() const noexcept = 0;
    [[nodiscard]] virtual std::shared_ptr<const IMigration> FindMigration(const MigrationKey& key) const = 0;
    [[nodiscard]] virtual bool HasMigration(const MigrationKey& key) const = 0;
    [[nodiscard]] virtual foundation::Result<std::vector<std::shared_ptr<const IMigration>>> FindMigrationPath(
        foundation::StringId type_id,
        SchemaVersion from,
        SchemaVersion to) const = 0;
};
} // namespace epidemic::runtime
