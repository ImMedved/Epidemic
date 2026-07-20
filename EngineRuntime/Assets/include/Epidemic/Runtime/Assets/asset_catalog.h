#pragma once

#include "Epidemic/Foundation/result.h"

#include "Epidemic/Runtime/Assets/asset_dependency_manifest.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
// Read-only asset catalog contract. Mutation is intentionally split into IAssetCatalogWriter
// so runtime consumers do not need write access after bootstrap.
class IAssetCatalog
{
  public:
    virtual ~IAssetCatalog() = default;

    [[nodiscard]] virtual std::optional<AssetMetadata> FindById(AssetId id) const = 0;
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByType(AssetType type) const = 0;
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const = 0;
    [[nodiscard]] virtual bool Contains(AssetId id) const = 0;
    [[nodiscard]] virtual bool IsSealed() const = 0;
    [[nodiscard]] virtual foundation::Result<AssetDependencyManifest> BuildDependencyManifest(AssetId root) const = 0;
};
} // namespace epidemic::runtime

