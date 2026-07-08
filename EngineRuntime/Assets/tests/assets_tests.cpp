#include "Epidemic/Runtime/Assets/asset_metadata.h"

#include <type_traits>

namespace
{
using epidemic::runtime::AssetDependency;
using epidemic::runtime::AssetLocation;
using epidemic::runtime::AssetLocationKind;
using epidemic::runtime::AssetMetadata;
using epidemic::runtime::AssetState;
using epidemic::runtime::AssetType;

bool TestDefaultMetadataState()
{
    const AssetMetadata metadata{};
    return !metadata.id.IsValid() && !metadata.type.IsValid() && metadata.location.kind == AssetLocationKind::FilePath &&
           metadata.location.Empty() && metadata.state == AssetState::Unknown && metadata.dependencies.empty() &&
           metadata.tags.empty() && metadata.content_hash == 0 && metadata.version == 0;
}

bool TestMetadataCarriesDependenciesAndTags()
{
    AssetMetadata metadata{};
    metadata.id = epidemic::runtime::AssetId::FromString("items/potato.itemdef");
    metadata.type = AssetType{epidemic::foundation::StringId::FromString("itemdef")};
    metadata.location = AssetLocation{AssetLocationKind::VirtualPath, "items/potato.itemdef"};
    metadata.state = AssetState::Discovered;
    metadata.dependencies.push_back(AssetDependency{epidemic::runtime::AssetId::FromString("shared/potato.mesh"), true});
    metadata.tags.push_back(epidemic::foundation::StringId::FromString("food"));
    metadata.content_hash = 1234;
    metadata.version = 2;

    return metadata.id.IsValid() && metadata.type.IsValid() && !metadata.location.Empty() &&
           metadata.location.kind == AssetLocationKind::VirtualPath && metadata.state == AssetState::Discovered &&
           metadata.dependencies.size() == 1 && metadata.dependencies.front().asset_id.IsValid() &&
           metadata.dependencies.front().required && metadata.tags.size() == 1 && metadata.content_hash == 1234 &&
           metadata.version == 2;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(AssetMetadata{}.content_hash), std::uint64_t>);
    static_assert(std::is_same_v<decltype(AssetMetadata{}.version), std::uint32_t>);

    if (!TestDefaultMetadataState())
    {
        return 1;
    }

    if (!TestMetadataCarriesDependenciesAndTags())
    {
        return 2;
    }

    return 0;
}
