#include "Epidemic/Runtime/Persistence/persistence_services.h"

#include "in_memory_persistence_support.h"

namespace epidemic::runtime
{
foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options)
{
    (void)options;

    PersistenceServices services{};
    services.store = std::make_shared<InMemoryPersistenceStore>();
    return foundation::Result<PersistenceServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
