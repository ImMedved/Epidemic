#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_location.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
// Small resolver interface that returns the registered logical location for an
// asset id without exposing the full asset catalog implementation.
class IAssetLocationResolver
{
  public:
    virtual ~IAssetLocationResolver() = default;

    // Resolves the stored logical location for an asset id without opening files,
    // mounting packages, or acquiring resources. Invalid id -> asset.invalid_id;
    // valid-but-unregistered id -> asset.not_found. Success returns a detached copy.
    // Allocation failure may propagate and cannot mutate catalog state.
    [[nodiscard]] virtual foundation::Result<AssetLocation> Resolve(AssetId id) const = 0;
};
}
