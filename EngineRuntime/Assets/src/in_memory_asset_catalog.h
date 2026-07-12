#pragma once

#include "Epidemic/Runtime/Assets/asset_catalog.h"
#include "Epidemic/Runtime/Assets/asset_catalog_writer.h"
#include "Epidemic/Runtime/Assets/asset_location_resolver.h"

#include <unordered_map>

namespace epidemic::runtime
{
// File note:
// In-memory reference implementation of the asset contracts. It is intentionally
// simple and primarily exists to support runtime tests and early integration.
class InMemoryAssetCatalog final : public IAssetCatalog, public IAssetCatalogWriter, public IAssetLocationResolver
{
  public:
    // Stores a new metadata snapshot under metadata.id.
    // Input: asset metadata value object.
    // Output: success on insertion, failure on invalid or duplicate ids.
    // Relation: feeds every read path in this class.
    [[nodiscard]] foundation::Result<void> RegisterAsset(AssetMetadata metadata) override;

    // Reads one metadata snapshot by id.
    // Input: asset id key.
    // Output: copied metadata or nullopt.
    [[nodiscard]] std::optional<AssetMetadata> FindById(AssetId id) const override;

    // Reads all metadata snapshots that share the same asset type.
    // Input: asset type filter.
    // Output: vector of copied metadata values.
    [[nodiscard]] std::vector<AssetMetadata> FindByType(AssetType type) const override;

    // Reads all metadata snapshots that contain the requested tag.
    // Input: tag identifier.
    // Output: vector of copied metadata values.
    [[nodiscard]] std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const override;

    // Checks whether the catalog already has an entry for the id.
    // Input: asset id key.
    // Output: boolean presence flag.
    [[nodiscard]] bool Contains(AssetId id) const override;

    // Resolves only the location portion of a stored metadata record.
    // Input: asset id key.
    // Output: success with location or failure when the record is absent.
    // Relation: implemented in terms of FindById.
    [[nodiscard]] foundation::Result<AssetLocation> Resolve(AssetId id) const override;

  private:
    std::unordered_map<AssetId, AssetMetadata> assets_;
};
} // namespace epidemic::runtime
