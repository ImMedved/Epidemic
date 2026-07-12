#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

namespace epidemic::runtime
{
// File note:
// Write-side contract for asset catalog population. The split keeps read-only
// consumers from depending on mutation capabilities by default.
class IAssetCatalogWriter
{
  public:
    virtual ~IAssetCatalogWriter() = default;

    // Inserts a new asset metadata record into the catalog.
    // Input: fully prepared metadata with valid id, type and location.
    // Output: success when stored, failure when validation or uniqueness checks fail.
    // Relation: writes data later consumed through IAssetCatalog read APIs.
    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(AssetMetadata metadata) = 0;
};
} // namespace epidemic::runtime
