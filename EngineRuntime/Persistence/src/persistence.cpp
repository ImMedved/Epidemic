#include "Epidemic/Runtime/Persistence/persistence_services.h"

#include "in_memory_persistence_support.h"

#include <new>

namespace epidemic::runtime
{
foundation::Result<PersistenceServices> CreatePersistenceServices(const PersistenceOptions& options)
{
    PersistenceSnapshot snapshot{};
    if (options.backend)
    {
        try
        {
            const auto loaded = options.backend->Load();
            if (!loaded)
            {
                return foundation::Result<PersistenceServices>::Failure(loaded.GetError());
            }
            const auto valid = ValidatePersistenceSnapshot(loaded.Value());
            if (!valid)
            {
                return foundation::Result<PersistenceServices>::Failure(valid.GetError());
            }
            snapshot = loaded.Value();
        }
        catch (...)
        {
            return foundation::Result<PersistenceServices>::Failure(
                foundation::Error::Create("persistence.backend_exception", "persistence backend threw while loading snapshot"));
        }
    }

    PersistenceServices services{};
    try
    {
        services.store = std::make_shared<InMemoryPersistenceStore>(std::move(snapshot), options.backend, options.durability);
        services.query = services.store;
    }
    catch (const std::bad_alloc&)
    {
        return foundation::Result<PersistenceServices>::Failure(
            foundation::Error::Create("persistence.allocation_failed", "failed to allocate persistence services"));
    }
    return foundation::Result<PersistenceServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
