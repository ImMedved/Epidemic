#include "Epidemic/Runtime/Environment/environment_services.h"

#include "environment_runtime_impl.h"

namespace epidemic::runtime
{
foundation::Result<EnvironmentServices> CreateEnvironmentServices(const EnvironmentOptions& options)
{
    auto runtime = std::make_shared<EnvironmentRuntime>();
    runtime->SetUpdatePolicy(options.update_policy);

    EnvironmentServices services{};
    services.runtime = runtime;
    services.query = runtime;
    services.writer = runtime;
    return foundation::Result<EnvironmentServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
