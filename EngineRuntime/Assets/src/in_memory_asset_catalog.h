#pragma once

#include "Epidemic/Runtime/Assets/asset_catalog.h"
#include "Epidemic/Runtime/Assets/asset_catalog_writer.h"
#include "Epidemic/Runtime/Assets/asset_location_resolver.h"

#include <unordered_map>
#include <unordered_set>

namespace epidemic::runtime
{
struct InMemoryAssetCatalogTestAccess;

// In-memory bootstrap/runtime catalog. It becomes read-only after Seal().
class InMemoryAssetCatalog final : public IAssetCatalog, public IAssetCatalogWriter, public IAssetLocationResolver
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterAsset(const AssetMetadata& metadata) override;
    [[nodiscard]] foundation::Result<void> Seal() override;

    [[nodiscard]] std::optional<AssetMetadata> FindById(AssetId id) const override;
    [[nodiscard]] std::vector<AssetMetadata> FindByType(AssetType type) const override;
    [[nodiscard]] std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const override;
    [[nodiscard]] bool Contains(AssetId id) const override;
    [[nodiscard]] bool IsSealed() const override;
    [[nodiscard]] foundation::Result<AssetDependencyManifest> BuildDependencyManifest(AssetId root) const override;
    [[nodiscard]] foundation::Result<AssetLocation> Resolve(AssetId id) const override;

  private:
    friend struct InMemoryAssetCatalogTestAccess;

    [[nodiscard]] foundation::Result<AssetMetadata> ValidateAndNormalize(const AssetMetadata& metadata) const;

    std::unordered_map<AssetId, AssetMetadata> assets_;
    bool sealed_ = false;
    mutable bool fail_next_metadata_candidate_build_for_testing_ = false;
    bool fail_next_catalog_publication_for_testing_ = false;
};
} // namespace epidemic::runtime
