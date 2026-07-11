#pragma once

#include "Epidemic/Runtime/Assets/asset_catalog.h"
#include "Epidemic/Runtime/Assets/asset_catalog_writer.h"
#include "Epidemic/Runtime/Assets/asset_location_resolver.h"

#include <unordered_map>

namespace epidemic::runtime
{
class InMemoryAssetCatalog final : public IAssetCatalog, public IAssetCatalogWriter, public IAssetLocationResolver
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterAsset(AssetMetadata metadata) override;
    [[nodiscard]] std::optional<AssetMetadata> FindById(AssetId id) const override;
    [[nodiscard]] std::vector<AssetMetadata> FindByType(AssetType type) const override;
    [[nodiscard]] std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const override;
    [[nodiscard]] bool Contains(AssetId id) const override;

    [[nodiscard]] foundation::Result<AssetLocation> Resolve(AssetId id) const override;

  private:
    std::unordered_map<AssetId, AssetMetadata> assets_;
};
} // namespace epidemic::runtime
