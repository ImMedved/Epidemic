#include "in_memory_asset_catalog.h"

namespace epidemic::runtime
{
// File note:
// Implements the lightweight in-memory asset catalog used by the runtime tests.
// The implementation favors snapshot-style reads over references to internal storage.

// Validates metadata identity and inserts it into the backing map.
// Input: metadata value that should already contain a valid id and type.
// Output: success on insert, failure on invalid or duplicate ids.
// Relation: establishes records consumed by FindById/FindByType/FindByTag/Resolve.
// TODO: No normalization is performed for tags or paths, so equivalent logical assets
// may still be indexed twice under different serialized metadata.
foundation::Result<void> InMemoryAssetCatalog::RegisterAsset(AssetMetadata metadata)
{
    if (!metadata.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("asset.invalid_id", "asset id must be valid before registration"));
    }

    if (assets_.contains(metadata.id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("asset.duplicate_id", "asset id is already registered"));
    }

    assets_.emplace(metadata.id, std::move(metadata));
    return foundation::Result<void>::Success();
}

// Returns a detached metadata snapshot for a single id lookup.
// Input: asset id key.
// Output: optional copied metadata.
// Relation: Resolve reuses this function to avoid duplicating not-found logic.
std::optional<AssetMetadata> InMemoryAssetCatalog::FindById(AssetId id) const
{
    const auto iterator = assets_.find(id);
    if (iterator == assets_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

// Scans the catalog and collects entries whose type matches the query.
// Input: desired asset type.
// Output: vector of copied metadata snapshots.
// Relation: linear search is acceptable here because this class is a simple reference implementation.
// TODO: If this catalog grows beyond test or bootstrap usage, add a secondary index by type.
std::vector<AssetMetadata> InMemoryAssetCatalog::FindByType(AssetType type) const
{
    std::vector<AssetMetadata> matches;
    for (const auto& [asset_id, metadata] : assets_)
    {
        (void)asset_id;
        if (metadata.type == type)
        {
            matches.push_back(metadata);
        }
    }

    return matches;
}

// Scans the catalog and returns entries containing the requested tag.
// Input: tag identifier stored inside AssetMetadata::tags.
// Output: vector of copied metadata snapshots.
// Relation: complements FindByType for broader content grouping queries.
// TODO: Repeated tag scans are O(N * tag_count); consider a tag index if query volume increases.
std::vector<AssetMetadata> InMemoryAssetCatalog::FindByTag(foundation::StringId tag) const
{
    std::vector<AssetMetadata> matches;
    for (const auto& [asset_id, metadata] : assets_)
    {
        (void)asset_id;
        for (const auto metadata_tag : metadata.tags)
        {
            if (metadata_tag == tag)
            {
                matches.push_back(metadata);
                break;
            }
        }
    }

    return matches;
}

// Performs a direct map lookup to test whether an asset id is registered.
// Input: asset id key.
// Output: true when the backing map already contains the id.
// Relation: cheap helper for callers that do not need the metadata payload.
bool InMemoryAssetCatalog::Contains(AssetId id) const
{
    return assets_.contains(id);
}

// Resolves only the location field for an indexed asset.
// Input: asset id key.
// Output: success with AssetLocation or failure if the asset is missing.
// Relation: implemented via FindById so missing-id semantics stay aligned.
foundation::Result<AssetLocation> InMemoryAssetCatalog::Resolve(AssetId id) const
{
    const auto metadata = FindById(id);
    if (!metadata)
    {
        return foundation::Result<AssetLocation>::Failure(
            foundation::Error::Create("asset.not_found", "asset location not registered"));
    }

    return foundation::Result<AssetLocation>::Success(metadata->location);
}
} // namespace epidemic::runtime
