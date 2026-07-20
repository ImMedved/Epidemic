#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_location.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
// Small resolver interface that translates asset ids into concrete storage
// locations without exposing the full asset catalog implementation.
class IAssetLocationResolver
{
  public:
    virtual ~IAssetLocationResolver() = default;

    // Resolves the persisted location for an asset id.
    // Input: asset id previously indexed in an asset catalog.
    // Output: success with AssetLocation or a failure result when missing.
    // Relation: commonly implemented by the same catalog that stores metadata.
    [[nodiscard]] virtual foundation::Result<AssetLocation> Resolve(AssetId id) const = 0;
};
} 
