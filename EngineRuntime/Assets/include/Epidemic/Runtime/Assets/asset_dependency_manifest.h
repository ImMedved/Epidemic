#pragma once

#include "Epidemic/Runtime/Assets/asset_metadata.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <vector>

namespace epidemic::runtime
{
struct AssetDependencyManifest
{
    AssetId root{};
    std::vector<AssetDependency> dependencies;
};
} // namespace epidemic::runtime
