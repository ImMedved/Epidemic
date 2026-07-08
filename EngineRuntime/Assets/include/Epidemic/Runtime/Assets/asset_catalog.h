#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
class IAssetCatalog
{
  public:
    virtual ~IAssetCatalog() = default;

    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(AssetMetadata metadata) = 0;
    [[nodiscard]] virtual std::optional<AssetMetadata> FindById(AssetId id) const = 0;
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByType(AssetType type) const = 0;
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const = 0;
    [[nodiscard]] virtual bool Contains(AssetId id) const = 0;
};
} // namespace epidemic::runtime
