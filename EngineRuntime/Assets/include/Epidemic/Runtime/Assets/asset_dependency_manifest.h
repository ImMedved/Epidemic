#pragma once

#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <vector>

namespace epidemic::runtime
{
struct AssetDependencyManifest
{
    AssetId root{};
    std::vector<AssetDependency> dependencies;
};
} // namespace epidemic::runtime
