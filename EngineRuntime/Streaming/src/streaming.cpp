#include "Epidemic/Runtime/Streaming/residency_controller.h"
#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "Epidemic/Runtime/Streaming/streaming_types.h"

#include "streaming_runtime_impl.h"

#include <utility>

// Umbrella translation unit that anchors the public Streaming contracts in the build.

namespace epidemic::runtime::streaming
{
foundation::Result<StreamingServices> CreateStreamingServices(const StreamingDependencies& dependencies)
{
    StreamingDependencies resolved = dependencies;
    if (resolved.residency_controller == nullptr)
    {
        resolved.residency_controller = std::make_shared<InMemoryResidencyController>();
    }
    auto runtime = std::make_shared<StreamingRuntime>(std::move(resolved));

    StreamingServices services{};
    services.runtime = runtime;
    services.query = runtime;
    services.controller = runtime;
    return foundation::Result<StreamingServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::streaming
