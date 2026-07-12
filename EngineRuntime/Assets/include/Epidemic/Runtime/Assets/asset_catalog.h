#pragma once

#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <optional>
#include <vector>

namespace epidemic::runtime
{
// File note:
// Read-only asset catalog contract used by runtime systems that need to inspect
// indexed asset metadata without being allowed to mutate the catalog.
class IAssetCatalog
{
  public:
    virtual ~IAssetCatalog() = default;

    // Looks up a single asset metadata snapshot by id.
    // Input: stable runtime asset id.
    // Output: copied metadata snapshot or nullopt when the id is unknown.
    // Relation: paired with RegisterAsset on IAssetCatalogWriter implementations.
    [[nodiscard]] virtual std::optional<AssetMetadata> FindById(AssetId id) const = 0;

    // Collects all assets of the requested type.
    // Input: semantic asset type such as mesh, texture or item definition.
    // Output: copied metadata snapshots for every matching record.
    // Relation: often used after type registration in writer-backed catalogs.
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByType(AssetType type) const = 0;

    // Collects all assets tagged with the requested marker.
    // Input: arbitrary tag id from metadata.tags.
    // Output: copied metadata snapshots for matching assets.
    // Relation: complements FindById and FindByType for coarse filtering.
    [[nodiscard]] virtual std::vector<AssetMetadata> FindByTag(foundation::StringId tag) const = 0;

    // Performs a cheap existence probe without materializing full metadata.
    // Input: stable runtime asset id.
    // Output: true when the catalog already contains the asset id.
    // Relation: convenience helper for callers that only need presence checks.
    [[nodiscard]] virtual bool Contains(AssetId id) const = 0;
};
} 
