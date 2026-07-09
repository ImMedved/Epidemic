#include "Epidemic/Runtime/Assets/asset_catalog_writer.h"
#include "Epidemic/Runtime/Assets/asset_dependency_manifest.h"
#include "in_memory_asset_catalog.h"

#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::runtime::AssetDependency;
using epidemic::runtime::AssetDependencyManifest;
using epidemic::runtime::AssetId;
using epidemic::runtime::AssetLocation;
using epidemic::runtime::AssetLocationKind;
using epidemic::runtime::AssetMetadata;
using epidemic::runtime::AssetState;
using epidemic::runtime::AssetType;
using epidemic::runtime::IAssetCatalog;
using epidemic::runtime::IAssetCatalogWriter;
using epidemic::runtime::IAssetLocationResolver;
using epidemic::runtime::InMemoryAssetCatalog;

AssetMetadata MakeMetadata(const char* asset_path, const char* asset_type, const char* tag, AssetLocationKind kind)
{
    AssetMetadata metadata{};
    metadata.id = AssetId::FromString(asset_path);
    metadata.type = AssetType{epidemic::foundation::StringId::FromString(asset_type)};
    metadata.location = AssetLocation{kind, asset_path};
    metadata.state = AssetState::Indexed;
    metadata.tags.push_back(epidemic::foundation::StringId::FromString(tag));
    metadata.content_hash = 1234;
    metadata.version = 1;
    return metadata;
}

bool TestDefaultMetadataState()
{
    const AssetMetadata metadata{};
    return !metadata.id.IsValid() && !metadata.type.IsValid() && metadata.location.kind == AssetLocationKind::FilePath &&
           metadata.location.Empty() && metadata.state == AssetState::Unknown && metadata.dependencies.empty() &&
           metadata.tags.empty() && metadata.content_hash == 0 && metadata.version == 0;
}

bool TestRegisterAssetStoresMetadata()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);

    const auto result = writer.RegisterAsset(metadata);
    const auto stored = catalog.FindById(metadata.id);

    return result && stored.has_value() && stored->id == metadata.id && stored->location == metadata.location;
}

bool TestDuplicateAssetIdReturnsError()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);

    const auto first = writer.RegisterAsset(metadata);
    const auto duplicate = writer.RegisterAsset(metadata);

    return first && !duplicate && duplicate.GetError().HasCode("asset.duplicate_id");
}

bool TestFindByIdReturnsSnapshot()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    if (!writer.RegisterAsset(metadata))
    {
        return false;
    }

    auto snapshot = catalog.FindById(metadata.id);
    if (!snapshot)
    {
        return false;
    }

    snapshot->version = 99;
    snapshot->location.value = "mutated/path.itemdef";

    const auto refetched = catalog.FindById(metadata.id);
    return refetched.has_value() && refetched->version == metadata.version && refetched->location.value == metadata.location.value;
}

bool TestFindByTypeWorks()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata item_asset = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetMetadata mesh_asset = MakeMetadata("meshes/potato.mesh", "mesh", "food", AssetLocationKind::PackageEntry);

    if (!writer.RegisterAsset(item_asset) || !writer.RegisterAsset(mesh_asset))
    {
        return false;
    }

    const auto item_type_matches = catalog.FindByType(item_asset.type);
    return item_type_matches.size() == 1 && item_type_matches.front().id == item_asset.id;
}

bool TestFindByTagWorks()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata food_asset = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetMetadata ui_asset = MakeMetadata("ui/potato_icon.tex", "texture", "ui", AssetLocationKind::PackageEntry);

    if (!writer.RegisterAsset(food_asset) || !writer.RegisterAsset(ui_asset))
    {
        return false;
    }

    const auto food_matches = catalog.FindByTag(epidemic::foundation::StringId::FromString("food"));
    const auto missing_matches = catalog.FindByTag(epidemic::foundation::StringId::FromString("missing"));

    return food_matches.size() == 1 && food_matches.front().id == food_asset.id && missing_matches.empty();
}

bool TestMissingAssetReturnsNulloptAndResolveError()
{
    InMemoryAssetCatalog catalog;
    const AssetId missing_id = AssetId::FromString("items/missing.itemdef");

    const auto missing_snapshot = catalog.FindById(missing_id);
    const auto missing_location = catalog.Resolve(missing_id);

    return !missing_snapshot.has_value() && !missing_location && missing_location.GetError().HasCode("asset.not_found");
}

bool TestAssetLocationResolverReturnsRegisteredLocation()
{
    InMemoryAssetCatalog catalog;
    IAssetCatalogWriter& writer = catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    if (!writer.RegisterAsset(metadata))
    {
        return false;
    }

    const auto resolved = catalog.Resolve(metadata.id);
    return resolved && resolved.Value() == metadata.location;
}

bool TestDependencyManifestIsImmediateSnapshot()
{
    AssetDependencyManifest manifest{};
    manifest.root = AssetId::FromString("items/potato.itemdef");
    manifest.dependencies.push_back(AssetDependency{AssetId::FromString("shared/potato.mesh"), true});
    manifest.dependencies.push_back(AssetDependency{AssetId::FromString("shared/potato_icon.tex"), false});

    return manifest.root.IsValid() && manifest.dependencies.size() == 2 && manifest.dependencies[0].required &&
           !manifest.dependencies[1].required;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(AssetMetadata{}.content_hash), std::uint64_t>);
    static_assert(std::is_same_v<decltype(AssetMetadata{}.version), std::uint32_t>);
    static_assert(std::has_virtual_destructor_v<IAssetCatalog>);
    static_assert(std::has_virtual_destructor_v<IAssetCatalogWriter>);
    static_assert(std::has_virtual_destructor_v<IAssetLocationResolver>);

    if (!TestDefaultMetadataState())
    {
        return 1;
    }

    if (!TestRegisterAssetStoresMetadata())
    {
        return 2;
    }

    if (!TestDuplicateAssetIdReturnsError())
    {
        return 3;
    }

    if (!TestFindByIdReturnsSnapshot())
    {
        return 4;
    }

    if (!TestFindByTypeWorks())
    {
        return 5;
    }

    if (!TestFindByTagWorks())
    {
        return 6;
    }

    if (!TestMissingAssetReturnsNulloptAndResolveError())
    {
        return 7;
    }

    if (!TestAssetLocationResolverReturnsRegisteredLocation())
    {
        return 8;
    }

    if (!TestDependencyManifestIsImmediateSnapshot())
    {
        return 9;
    }

    return 0;
}
