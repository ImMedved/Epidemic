#pragma once

#include <string>
#include <vector>

namespace epidemic::core
{
struct ModuleManifest
{
    std::string id;
    std::string name;
    std::vector<std::string> dependencies;
};
} // namespace epidemic::core