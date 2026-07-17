#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/persistence_backend.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <memory>

namespace epidemic::runtime
{
enum class PersistenceDurability
{
    MemoryOnly,
    SaveRequired,
    SaveAndFlushRequired,
};

struct PersistenceOptions
{
    std::shared_ptr<IPersistenceBackend> backend;
    PersistenceDurability durability = PersistenceDurability::MemoryOnly;
};

struct PersistenceServices
{
    std::shared_ptr<IPersistenceStore> store;
    std::shared_ptr<IPersistenceStore> query;
};

[[nodiscard]] foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options = {});
} // namespace epidemic::runtime
