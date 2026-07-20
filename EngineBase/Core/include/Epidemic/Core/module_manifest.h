#pragma once

#include <string>
#include <vector>

namespace epidemic::core
{
// This file defines the static metadata used by ModuleRegistry.
// Dependencies are expressed by module id and are resolved into an execution order during bootstrap.

struct ModuleManifest
{
    std::string id;
    std::string name;
    std::vector<std::string> dependencies;
};
} // namespace epidemic::core