#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/persistence_backend.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <memory>

namespace epidemic::runtime
{
struct PersistenceOptions
{
    std::shared_ptr<IPersistenceBackend> backend;
};

struct PersistenceServices
{
    std::shared_ptr<IPersistenceStore> store;
    std::shared_ptr<IPersistenceStore> query;
};

[[nodiscard]] foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options = {});
} // namespace epidemic::runtime
