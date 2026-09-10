#include "Epidemic/Runtime/Assets/asset_services.h"
#include "in_memory_asset_catalog.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <string>
#include <vector>

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
    if (kind == AssetLocationKind::PackageEntry || kind == AssetLocationKind::VirtualPath)
    {
        metadata.location.mount_id = StringId::FromString("game");
    }
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

bool TestPathValidationRejectsTraversalAndRequiresMounts()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata windows_absolute = MakeMetadata("C:\\absolute\\path.asset", "itemdef", "path", AssetLocationKind::FilePath);
    AssetMetadata posix_absolute = MakeMetadata("/absolute/path.asset", "itemdef", "path", AssetLocationKind::FilePath);
    AssetMetadata outside = MakeMetadata("../outside.asset", "itemdef", "path", AssetLocationKind::VirtualPath);
    AssetMetadata escaped_mount = MakeMetadata("mount/../../outside.asset", "itemdef", "path", AssetLocationKind::PackageEntry);
    AssetMetadata missing_virtual_mount = MakeMetadata("items/potato.itemdef", "itemdef", "path", AssetLocationKind::VirtualPath);
    AssetMetadata missing_package_mount = MakeMetadata("packages/potato.mesh", "mesh", "path", AssetLocationKind::PackageEntry);
    missing_virtual_mount.location.mount_id = {};
    missing_package_mount.location.mount_id = {};

    const auto windows_result = catalog.RegisterAsset(windows_absolute);
    const auto posix_result = catalog.RegisterAsset(posix_absolute);
    const auto outside_result = catalog.RegisterAsset(outside);
    const auto escaped_result = catalog.RegisterAsset(escaped_mount);
    const auto missing_virtual_result = catalog.RegisterAsset(missing_virtual_mount);
    const auto missing_package_result = catalog.RegisterAsset(missing_package_mount);

    return !windows_result && windows_result.GetError().HasCode("asset.invalid_location") &&
           !posix_result && posix_result.GetError().HasCode("asset.invalid_location") &&
           !outside_result && outside_result.GetError().HasCode("asset.invalid_location") &&
           !escaped_result && escaped_result.GetError().HasCode("asset.invalid_location") &&
           !missing_virtual_result && missing_virtual_result.GetError().HasCode("asset.invalid_location") &&
           !missing_package_result && missing_package_result.GetError().HasCode("asset.invalid_location");
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
    const AssetMetadata item_asset = MakeMetadata("items/z_potato.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetMetadata carrot_asset = MakeMetadata("items/a_carrot.itemdef", "itemdef", "food", AssetLocationKind::VirtualPath);
    const AssetMetadata mesh_asset = MakeMetadata("meshes/m_potato.mesh", "mesh", "food", AssetLocationKind::PackageEntry);
    const AssetMetadata ui_asset = MakeMetadata("ui/potato_icon.tex", "texture", "ui", AssetLocationKind::PackageEntry);

    if (!catalog.RegisterAsset(item_asset) || !catalog.RegisterAsset(mesh_asset) || !catalog.RegisterAsset(ui_asset) ||
        !catalog.RegisterAsset(carrot_asset))
    {
        return false;
    }

    const auto item_type_matches = catalog.FindByType(item_asset.type);
    const auto food_matches = catalog.FindByTag(StringId::FromString("food"));
    const auto missing_matches = catalog.FindByTag(StringId::FromString("missing"));

    std::vector<AssetId> expected_item_type{item_asset.id, carrot_asset.id};
    std::vector<AssetId> expected_food{item_asset.id, carrot_asset.id, mesh_asset.id};
    std::sort(expected_item_type.begin(), expected_item_type.end(), [](AssetId lhs, AssetId rhs) {
        return lhs.Raw() < rhs.Raw();
    });
    std::sort(expected_food.begin(), expected_food.end(), [](AssetId lhs, AssetId rhs) {
        return lhs.Raw() < rhs.Raw();
    });

    return item_type_matches.size() == 2 && item_type_matches[0].id == expected_item_type[0] &&
           item_type_matches[1].id == expected_item_type[1] && food_matches.size() == 3 &&
           food_matches[0].id == expected_food[0] &&
           food_matches[1].id == expected_food[1] &&
           food_matches[2].id == expected_food[2] && missing_matches.empty();
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

bool TestDependencyManifestDeduplicatesDiamondAndMissingPolicy()
{
    InMemoryAssetCatalog catalog;
    AssetMetadata root = MakeMetadata("a/root.asset", "itemdef", "manifest", AssetLocationKind::VirtualPath);
    AssetMetadata left = MakeMetadata("b/left.asset", "itemdef", "manifest", AssetLocationKind::VirtualPath);
    AssetMetadata right = MakeMetadata("c/right.asset", "itemdef", "manifest", AssetLocationKind::VirtualPath);
    AssetMetadata shared = MakeMetadata("d/shared.asset", "mesh", "manifest", AssetLocationKind::VirtualPath);
    root.dependencies.push_back(AssetDependency{left.id, true});
    root.dependencies.push_back(AssetDependency{right.id, true});
    left.dependencies.push_back(AssetDependency{shared.id, true});
    right.dependencies.push_back(AssetDependency{shared.id, true});

    if (!catalog.RegisterAsset(root) || !catalog.RegisterAsset(left) || !catalog.RegisterAsset(right) ||
        !catalog.RegisterAsset(shared))
    {
        return false;
    }

    const auto manifest = catalog.BuildDependencyManifest(root.id);

    InMemoryAssetCatalog missing_required;
    AssetMetadata required_root = MakeMetadata("required/root.asset", "itemdef", "manifest", AssetLocationKind::VirtualPath);
    required_root.dependencies.push_back(AssetDependency{AssetId::FromString("missing/required.asset"), true});
    const bool required_seeded = missing_required.RegisterAsset(required_root).HasValue();
    const auto required_manifest = missing_required.BuildDependencyManifest(required_root.id);

    InMemoryAssetCatalog missing_optional;
    AssetMetadata optional_root = MakeMetadata("optional/root.asset", "itemdef", "manifest", AssetLocationKind::VirtualPath);
    const AssetId optional_id = AssetId::FromString("missing/optional.asset");
    optional_root.dependencies.push_back(AssetDependency{optional_id, false});
    const bool optional_seeded = missing_optional.RegisterAsset(optional_root).HasValue();
    const auto optional_manifest = missing_optional.BuildDependencyManifest(optional_root.id);

    std::vector<AssetId> expected{left.id, right.id, shared.id};
    std::sort(expected.begin(), expected.end(), [](AssetId lhs, AssetId rhs) {
        return lhs.Raw() < rhs.Raw();
    });
    return manifest && manifest.Value().dependencies.size() == 3 &&
           manifest.Value().dependencies[0].asset_id == expected[0] &&
           manifest.Value().dependencies[1].asset_id == expected[1] &&
           manifest.Value().dependencies[2].asset_id == expected[2] &&
           required_seeded && !required_manifest && required_manifest.GetError().HasCode("asset.missing_dependency") &&
           optional_seeded && optional_manifest && optional_manifest.Value().dependencies.size() == 1 &&
           optional_manifest.Value().dependencies.front().asset_id == optional_id;
}


bool TestEnumTagAndFailureAtomicity()
{
    InMemoryAssetCatalog catalog;
    const auto baseline = MakeMetadata("baseline.asset", "itemdef", "valid", AssetLocationKind::VirtualPath);
    if (!catalog.RegisterAsset(baseline)) return false;

    auto invalid_kind = MakeMetadata("bad-kind.asset", "itemdef", "valid", AssetLocationKind::VirtualPath);
    invalid_kind.location.kind = static_cast<AssetLocationKind>(255);
    auto invalid_state = MakeMetadata("bad-state.asset", "itemdef", "valid", AssetLocationKind::VirtualPath);
    invalid_state.state = static_cast<AssetState>(255);
    auto invalid_tag = MakeMetadata("bad-tag.asset", "itemdef", "valid", AssetLocationKind::VirtualPath);
    invalid_tag.tags = {StringId{}};
    auto mixed_tag = MakeMetadata("mixed-tag.asset", "itemdef", "valid", AssetLocationKind::VirtualPath);
    mixed_tag.tags.push_back(StringId{});

    const auto k = catalog.RegisterAsset(invalid_kind);
    const auto st = catalog.RegisterAsset(invalid_state);
    const auto t = catalog.RegisterAsset(invalid_tag);
    const auto mt = catalog.RegisterAsset(mixed_tag);
    return !k && k.GetError().HasCode("asset.invalid_location") &&
           !st && st.GetError().HasCode("asset.invalid_state") &&
           !t && t.GetError().HasCode("asset.invalid_tag") &&
           !mt && mt.GetError().HasCode("asset.invalid_tag") &&
           catalog.FindById(baseline.id).has_value() &&
           !catalog.FindById(invalid_kind.id).has_value() &&
           !catalog.FindById(invalid_state.id).has_value() &&
           !catalog.FindById(invalid_tag.id).has_value();
}

bool TestWindowsDriveRelativeAndHostRootPaths()
{
    InMemoryAssetCatalog catalog;
    const char* rejected[] = {"C:foo.asset", "C:", "C:/foo.asset", "C:\\foo.asset", "\\\\server\\share.asset", "../foo.asset", "a/../../foo.asset"};
    for (const char* path : rejected)
    {
        auto metadata = MakeMetadata(path, "itemdef", "path", AssetLocationKind::FilePath);
        const auto result = catalog.RegisterAsset(metadata);
        if (result || !result.GetError().HasCode("asset.invalid_location") || catalog.FindById(metadata.id).has_value())
            return false;
    }
    const auto valid = MakeMetadata("content/items/foo.asset", "itemdef", "path", AssetLocationKind::FilePath);
    return catalog.RegisterAsset(valid).HasValue() && catalog.FindById(valid.id).has_value();
}

bool TestSealIdempotenceAndEmptyCatalogContracts()
{
    InMemoryAssetCatalog catalog;
    const auto missing = AssetId::FromString("missing/root.asset");
    const auto empty_manifest = catalog.BuildDependencyManifest(missing);
    if (empty_manifest || !empty_manifest.GetError().HasCode("asset.not_found") ||
        !catalog.FindByType(AssetType{StringId::FromString("itemdef")}).empty() ||
        !catalog.FindByTag(StringId::FromString("tag")).empty())
        return false;
    const auto first = catalog.Seal();
    const auto second = catalog.Seal();
    return first && second && catalog.IsSealed();
}

bool TestDeepDependencyManifestIsIterative()
{
    constexpr int kDepth = 6000;
    InMemoryAssetCatalog catalog;
    std::vector<AssetMetadata> chain;
    chain.reserve(kDepth);
    for (int i = 0; i < kDepth; ++i)
    {
        const std::string path = "deep/" + std::to_string(i) + ".asset";
        chain.push_back(MakeMetadata(path.c_str(), "itemdef", "deep", AssetLocationKind::VirtualPath));
    }
    for (int i = 0; i + 1 < kDepth; ++i)
        chain[i].dependencies.push_back(AssetDependency{chain[i + 1].id, true});
    for (const auto& metadata : chain)
        if (!catalog.RegisterAsset(metadata)) return false;
    const auto manifest = catalog.BuildDependencyManifest(chain.front().id);
    return manifest && manifest.Value().dependencies.size() == static_cast<std::size_t>(kDepth - 1);
}

bool TestMetadataBoundsRejectBeforeMutation()
{
    InMemoryAssetCatalog catalog;
    auto too_long = MakeMetadata("bounded.asset", "itemdef", "tag", AssetLocationKind::VirtualPath);
    too_long.location.path.assign(epidemic::runtime::kMaxAssetPathBytes + 1, 'a');
    const auto path_result = catalog.RegisterAsset(too_long);

    auto too_many_tags = MakeMetadata("many-tags.asset", "itemdef", "tag", AssetLocationKind::VirtualPath);
    too_many_tags.tags.clear();
    too_many_tags.tags.reserve(epidemic::runtime::kMaxAssetTags + 1);
    for (std::size_t i = 0; i <= epidemic::runtime::kMaxAssetTags; ++i)
        too_many_tags.tags.push_back(StringId::FromString(("tag-" + std::to_string(i)).c_str()));
    const auto tags_result = catalog.RegisterAsset(too_many_tags);

    return !path_result && path_result.GetError().HasCode("asset.metadata_too_large") &&
           !tags_result && tags_result.GetError().HasCode("asset.metadata_too_large") &&
           !catalog.FindById(too_long.id).has_value() && !catalog.FindById(too_many_tags.id).has_value();
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
    if (!TestPathValidationRejectsTraversalAndRequiresMounts()) return 4;
    if (!TestDuplicateAssetIdReturnsError()) return 5;
    if (!TestSelfAndDuplicateDependenciesFail()) return 6;
    if (!TestCatalogSeal()) return 7;
    if (!TestFindByIdReturnsSnapshot()) return 8;
    if (!TestFindByTypeAndTagWork()) return 9;
    if (!TestMissingAssetReturnsNulloptAndResolveError()) return 10;
    if (!TestAssetLocationResolverReturnsRegisteredLocation()) return 11;
    if (!TestCanonicalPaths()) return 12;
    if (!TestDependencyManifestAndCycle()) return 13;
    if (!TestDependencyManifestDeduplicatesDiamondAndMissingPolicy()) return 14;
    if (!TestAssetServicesFactory()) return 15;
    if (!TestEnumTagAndFailureAtomicity()) return 16;
    if (!TestWindowsDriveRelativeAndHostRootPaths()) return 17;
    if (!TestSealIdempotenceAndEmptyCatalogContracts()) return 18;
    if (!TestDeepDependencyManifestIsIterative()) return 19;
    if (!TestMetadataBoundsRejectBeforeMutation()) return 20;

    return 0;
}
