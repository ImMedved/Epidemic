#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/object_materialization.h"
#include "Epidemic/Runtime/World/object_placement.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_location.h"
#include "Epidemic/Runtime/World/world_object.h"
#include "Epidemic/Runtime/World/world_object_registry.h"
#include "Epidemic/Runtime/World/world_query.h"
#include "Epidemic/Runtime/World/world_state.h"
#include "Epidemic/Runtime/World/world_services.h"

#include "world_runtime_impl.h"

#include <memory>

namespace epidemic::runtime
{
foundation::Result<WorldServices> CreateWorldServices(const WorldOptions& options)
{
    (void)options;
    auto runtime = std::make_shared<WorldRuntime>();
    WorldServices services{};
    services.regions = runtime;
    services.chunks = runtime;
    services.query = runtime;
    services.writer = runtime;
    services.materialization = runtime;
    return foundation::Result<WorldServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
