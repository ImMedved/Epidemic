#include "Epidemic/Runtime/Streaming/residency_controller.h"
#include "Epidemic/Runtime/Streaming/streaming_priority_resolver.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_sources.h"
#include "Epidemic/Runtime/Streaming/streaming_types.h"

#include "streaming_runtime_impl.h"

// File note:
// Umbrella translation unit that anchors the public Streaming contracts in the build.

namespace epidemic::runtime::streaming
{
StreamingServices CreateStreamingServices()
{
    auto runtime = std::make_shared<StreamingRuntime>();

    StreamingServices services{};
    services.runtime = runtime;
    services.query = runtime;
    return services;
}
} // namespace epidemic::runtime::streaming
