#include "Epidemic/Runtime/Assets/asset_services.h"
#include "in_memory_asset_catalog.h"

#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::foundation::StringId;
using epidemic::runtime::AssetDependency;
using epidemic::runtime::AssetId;
using epidemic::runtime::AssetLocation;
using epidemic::runtime::AssetLocationKind;
using epidemic::runtime::AssetMetadata;
using epidemic::runtime::AssetState;
using epidemic::runtime::AssetType;
using epidemic::runtime::CreateAssetServices;
using epidemic::runtime::IAssetCatalog;
using epidemic::runtime::IAssetCatalogWriter;
using epidemic::runtime::IAssetLocationResolver;
using epidemic::runtime::InMemoryAssetCatalog;

AssetMetadata MakeMetadata(const char* asset_path, const char* asset_type, const char* tag, AssetLocationKind kind)
{
    AssetMetadata metadata{};
    metadata.id = AssetId::FromString(asset_path);
    metadata.type = AssetType{StringId::FromString(asset_type)};
    metadata.location = AssetLocation{kind, asset_path};
    metadata.state = AssetState::Indexed;
    metadata.tags.push_back(StringId::FromString(tag));
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

bool TestValidationFailures()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata invalid_id = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    invalid_id.id = {};
    AssetMetadata invalid_type = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    invalid_type.type = {};
    AssetMetadata invalid_location = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    invalid_location.location.path.clear();

    const auto id_result = catalog.RegisterAsset(invalid_id);
    const auto type_result = catalog.RegisterAsset(invalid_type);
    const auto location_result = catalog.RegisterAsset(invalid_location);

    return !id_result && id_result.GetError().HasCode("asset.invalid_id") && !type_result &&
           type_result.GetError().HasCode("asset.invalid_type") && !location_result &&
           location_result.GetError().HasCode("asset.invalid_location");
}

bool TestDuplicateAssetIdReturnsError()
{
    InMemoryAssetCatalog catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);

    const auto first = catalog.RegisterAsset(metadata);
    const auto duplicate = catalog.RegisterAsset(metadata);

    return first && !duplicate && duplicate.GetError().HasCode("asset.duplicate");
}

bool TestSelfAndDuplicateDependenciesFail()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata self = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    self.dependencies.push_back(AssetDependency{self.id, true});

    AssetMetadata duplicate = MakeMetadata("items/carrot.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetId dependency = AssetId::FromString("shared/root.mesh");
    duplicate.dependencies.push_back(AssetDependency{dependency, true});
    duplicate.dependencies.push_back(AssetDependency{dependency, false});

    const auto self_result = catalog.RegisterAsset(self);
    const auto duplicate_result = catalog.RegisterAsset(duplicate);
    return !self_result && self_result.GetError().HasCode("asset.self_dependency") && !duplicate_result &&
           duplicate_result.GetError().HasCode("asset.invalid_dependency");
}

bool TestCatalogSeal()
{
    InMemoryAssetCatalog catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    if (!catalog.RegisterAsset(metadata) || !catalog.Seal())
    {
        return false;
    }

    const auto after_seal = catalog.FindById(metadata.id);
    const auto rejected = catalog.RegisterAsset(MakeMetadata("items/carrot.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath));
    return catalog.IsSealed() && after_seal.has_value() && !rejected && rejected.GetError().HasCode("asset.catalog_sealed");
}

bool TestFindByIdReturnsSnapshot()
{
    InMemoryAssetCatalog catalog;
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    if (!catalog.RegisterAsset(metadata))
    {
        return false;
    }

    auto snapshot = catalog.FindById(metadata.id);
    if (!snapshot)
    {
        return false;
    }

    snapshot->version = 99;
    snapshot->location.path = "mutated/path.itemdef";

    const auto refetched = catalog.FindById(metadata.id);
    return refetched.has_value() && refetched->version == metadata.version && refetched->location.path == metadata.location.path;
}

bool TestFindByTypeAndTagWork()
{
    InMemoryAssetCatalog catalog;
    const AssetMetadata item_asset = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetMetadata mesh_asset = MakeMetadata("meshes/potato.mesh", "mesh", "food", AssetLocationKind::PackageEntry);
    const AssetMetadata ui_asset = MakeMetadata("ui/potato_icon.tex", "texture", "ui", AssetLocationKind::PackageEntry);

    if (!catalog.RegisterAsset(item_asset) || !catalog.RegisterAsset(mesh_asset) || !catalog.RegisterAsset(ui_asset))
    {
        return false;
    }

    const auto item_type_matches = catalog.FindByType(item_asset.type);
    const auto food_matches = catalog.FindByTag(StringId::FromString("food"));
    const auto missing_matches = catalog.FindByTag(StringId::FromString("missing"));

    return item_type_matches.size() == 1 && item_type_matches.front().id == item_asset.id && food_matches.size() == 2 &&
           missing_matches.empty();
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
    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    if (!catalog.RegisterAsset(metadata))
    {
        return false;
    }

    const auto resolved = catalog.Resolve(metadata.id);
    return resolved && resolved.Value() == metadata.location;
}

bool TestCanonicalPaths()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata metadata = MakeMetadata("items/./props/../potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    metadata.id = AssetId::FromString("items/potato.itemdef");
    if (!catalog.RegisterAsset(metadata))
    {
        return false;
    }

    const auto stored = catalog.FindById(metadata.id);
    return stored.has_value() && stored->location.path == "items/potato.itemdef";
}

bool TestDependencyManifestAndCycle()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata root = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    AssetMetadata mesh = MakeMetadata("meshes/potato.mesh", "mesh", "food", AssetLocationKind::VirtualPath);
    root.dependencies.push_back(AssetDependency{mesh.id, true});
    if (!catalog.RegisterAsset(root) || !catalog.RegisterAsset(mesh))
    {
        return false;
    }

    const auto manifest = catalog.BuildDependencyManifest(root.id);

    InMemoryAssetCatalog cyclic;
    AssetMetadata a = MakeMetadata("a.asset", "itemdef", "a", AssetLocationKind::VirtualPath);
    AssetMetadata b = MakeMetadata("b.asset", "itemdef", "b", AssetLocationKind::VirtualPath);
    a.dependencies.push_back(AssetDependency{b.id, true});
    b.dependencies.push_back(AssetDependency{a.id, true});
    const bool seeded_cycle = cyclic.RegisterAsset(a) && cyclic.RegisterAsset(b);
    const auto cycle_manifest = cyclic.BuildDependencyManifest(a.id);

    return manifest && manifest.Value().root == root.id && manifest.Value().dependencies.size() == 1 && seeded_cycle &&
           !cycle_manifest && cycle_manifest.GetError().HasCode("asset.dependency_cycle");
}

bool TestAssetServicesFactory()
{
    const auto services = CreateAssetServices();
    if (!services)
    {
        return false;
    }

    const AssetMetadata metadata = MakeMetadata("items/potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    return services.Value().catalog && services.Value().writer && services.Value().location_resolver &&
           services.Value().writer->RegisterAsset(metadata) && services.Value().catalog->Contains(metadata.id);
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(AssetMetadata{}.content_hash), std::uint64_t>);
    static_assert(std::is_same_v<decltype(AssetMetadata{}.version), std::uint32_t>);
    static_assert(std::has_virtual_destructor_v<IAssetCatalog>);
    static_assert(std::has_virtual_destructor_v<IAssetCatalogWriter>);
    static_assert(std::has_virtual_destructor_v<IAssetLocationResolver>);

    if (!TestDefaultMetadataState()) return 1;
    if (!TestRegisterAssetStoresMetadata()) return 2;
    if (!TestValidationFailures()) return 3;
    if (!TestDuplicateAssetIdReturnsError()) return 4;
    if (!TestSelfAndDuplicateDependenciesFail()) return 5;
    if (!TestCatalogSeal()) return 6;
    if (!TestFindByIdReturnsSnapshot()) return 7;
    if (!TestFindByTypeAndTagWork()) return 8;
    if (!TestMissingAssetReturnsNulloptAndResolveError()) return 9;
    if (!TestAssetLocationResolverReturnsRegisteredLocation()) return 10;
    if (!TestCanonicalPaths()) return 11;
    if (!TestDependencyManifestAndCycle()) return 12;
    if (!TestAssetServicesFactory()) return 13;

    return 0;
}
