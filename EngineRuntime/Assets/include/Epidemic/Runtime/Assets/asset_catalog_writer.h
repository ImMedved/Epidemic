#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IAssetCatalogWriter
{
  public:
    virtual ~IAssetCatalogWriter() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(const AssetMetadata& metadata) = 0;
    [[nodiscard]] virtual foundation::Result<void> Seal() = 0;
};
} // namespace epidemic::runtime
