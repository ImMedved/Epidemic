#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Environment/environment_runtime.h"

#include <memory>

namespace epidemic::runtime
{
struct EnvironmentOptions
{
    std::shared_ptr<const IEnvironmentUpdatePolicy> update_policy;
};

struct EnvironmentServices
{
    std::shared_ptr<IEnvironmentRuntime> runtime;
    std::shared_ptr<IEnvironmentQuery> query;
    std::shared_ptr<IEnvironmentWriter> writer;
};

[[nodiscard]] foundation::Result<EnvironmentServices> CreateEnvironmentServices(const EnvironmentOptions& options = {});
} // namespace epidemic::runtime
