#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

namespace epidemic::runtime
{
class IAssetCatalogWriter
{
  public:
    virtual ~IAssetCatalogWriter() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(AssetMetadata metadata) = 0;
};
} // namespace epidemic::runtime
