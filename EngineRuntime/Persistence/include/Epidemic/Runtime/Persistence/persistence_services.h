#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/persistence_backend.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <memory>

namespace epidemic::runtime
{
// Persistence backend/schema composition is fixed when services are created.
// Runtime object data remains mutable transactionally; there is no hot schema/migration registry in this module.
struct PersistenceOptions
{
    std::shared_ptr<IPersistenceBackend> backend;
    PersistenceDurability durability = PersistenceDurability::MemoryOnly;
};

struct PersistenceServices
{
    std::shared_ptr<IPersistenceStore> store;
    std::shared_ptr<IPersistenceQuery> query;
};

[[nodiscard]] foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options = {});
} // namespace epidemic::runtime
