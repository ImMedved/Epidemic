#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"

#include <memory>

namespace epidemic::runtime
{
struct PersistenceOptions
{
};

struct PersistenceServices
{
    std::shared_ptr<IPersistenceStore> store;
};

[[nodiscard]] foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options = {});
} // namespace epidemic::runtime
