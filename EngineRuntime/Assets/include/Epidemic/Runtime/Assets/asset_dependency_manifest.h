#pragma once

#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <vector>

namespace epidemic::runtime
{
struct AssetDependencyManifest
{
    AssetId root{};
    // Deterministically sorted transitive dependencies. Each AssetId appears at
    // most once. required is true when any reachable dependency edge requires it.
    // Optional missing dependencies remain present for diagnostics/planning.
    std::vector<AssetDependency> dependencies;
};
} // namespace epidemic::runtime
