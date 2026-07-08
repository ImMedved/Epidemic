#include "Epidemic/Runtime/Assets/in_memory_asset_catalog.h"

namespace epidemic::runtime
{
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

std::optional<AssetMetadata> InMemoryAssetCatalog::FindById(AssetId id) const
{
    const auto iterator = assets_.find(id);
    if (iterator == assets_.end())
    {
        return std::nullopt;
    }

    return iterator->second;
}

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

bool InMemoryAssetCatalog::Contains(AssetId id) const
{
    return assets_.contains(id);
}

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
