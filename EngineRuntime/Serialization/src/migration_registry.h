#pragma once

#include "Epidemic/Runtime/Serialization/migration_registry.h"

#include <unordered_map>

namespace epidemic::runtime
{
class MigrationRegistry final : public IMigrationRegistry
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterMigration(IMigration& migration) override;
    [[nodiscard]] IMigration* FindMigration(const MigrationKey& key) override;
    [[nodiscard]] const IMigration* FindMigration(const MigrationKey& key) const override;
    [[nodiscard]] bool HasMigration(const MigrationKey& key) const override;

  private:
    std::unordered_map<MigrationKey, IMigration*> migrations_;
};
} // namespace epidemic::runtime
