#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_catalog.h"
#include "Epidemic/Runtime/Assets/asset_catalog_writer.h"
#include "Epidemic/Runtime/Assets/asset_location_resolver.h"

#include <memory>

namespace epidemic::runtime
{
struct AssetsOptions
{
};

struct AssetServices
{
    std::shared_ptr<IAssetCatalog> catalog;
    std::shared_ptr<IAssetCatalogWriter> writer;
    std::shared_ptr<IAssetLocationResolver> location_resolver;
};

[[nodiscard]] foundation::Result<AssetServices> CreateAssetServices(const AssetsOptions& options = {});
} // namespace epidemic::runtime
