#include "Epidemic/Runtime/Persistence/persistence_services.h"

#include "in_memory_persistence_support.h"

namespace epidemic::runtime
{
foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options)
{
    (void)options;

    PersistenceSnapshot snapshot{};
    if (options.backend)
    {
        const auto loaded = options.backend->Load();
        if (!loaded)
        {
            return foundation::Result<PersistenceServices>::Failure(loaded.GetError());
        }
        snapshot = loaded.Value();
    }

    PersistenceServices services{};
    services.store = std::make_shared<InMemoryPersistenceStore>(std::move(snapshot));
    services.query = services.store;
    return foundation::Result<PersistenceServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
