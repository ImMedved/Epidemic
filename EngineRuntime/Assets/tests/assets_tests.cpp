#include "Epidemic/Runtime/Assets/asset_catalog.h"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

namespace
{
using epidemic::runtime::AssetDependency;
using epidemic::runtime::AssetId;
using epidemic::runtime::AssetLocation;
using epidemic::runtime::AssetLocationKind;
using epidemic::runtime::AssetMetadata;
using epidemic::runtime::AssetState;
using epidemic::runtime::AssetType;
using epidemic::runtime::IAssetCatalog;

class StubAssetCatalog final : public IAssetCatalog
{
  public:
    [[nodiscard]] epidemic::foundation::Result<void> RegisterAsset(AssetMetadata metadata) override
    {
        last_registered_ = std::move(metadata);
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] std::optional<AssetMetadata> FindById(AssetId id) const override
    {
        if (last_registered_ && last_registered_->id == id)
        {
            return last_registered_;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::vector<AssetMetadata> FindByType(AssetType type) const override
    {
        if (last_registered_ && last_registered_->type == type)
        {
            return {*last_registered_};
        }

        return {};
    }

    [[nodiscard]] std::vector<AssetMetadata> FindByTag(epidemic::foundation::StringId tag) const override
    {
        if (last_registered_)
        {
            for (const auto existing_tag : last_registered_->tags)
            {
                if (existing_tag == tag)
                {
                    return {*last_registered_};
                }
            }
        }

        return {};
    }

    [[nodiscard]] bool Contains(AssetId id) const override
    {
        return last_registered_ && last_registered_->id == id;
    }

  private:
    std::optional<AssetMetadata> last_registered_;
};

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
    metadata.id = AssetId::FromString("items/potato.itemdef");
    metadata.type = AssetType{epidemic::foundation::StringId::FromString("itemdef")};
    metadata.location = AssetLocation{AssetLocationKind::VirtualPath, "items/potato.itemdef"};
    metadata.state = AssetState::Discovered;
    metadata.dependencies.push_back(AssetDependency{AssetId::FromString("shared/potato.mesh"), true});
    metadata.tags.push_back(epidemic::foundation::StringId::FromString("food"));
    metadata.content_hash = 1234;
    metadata.version = 2;

    return metadata.id.IsValid() && metadata.type.IsValid() && !metadata.location.Empty() &&
           metadata.location.kind == AssetLocationKind::VirtualPath && metadata.state == AssetState::Discovered &&
           metadata.dependencies.size() == 1 && metadata.dependencies.front().asset_id.IsValid() &&
           metadata.dependencies.front().required && metadata.tags.size() == 1 && metadata.content_hash == 1234 &&
           metadata.version == 2;
}

bool TestAssetCatalogContractCompilesAndReturnsSnapshots()
{
    StubAssetCatalog catalog;

    AssetMetadata metadata{};
    metadata.id = AssetId::FromString("items/potato.itemdef");
    metadata.type = AssetType{epidemic::foundation::StringId::FromString("itemdef")};
    metadata.location = AssetLocation{AssetLocationKind::VirtualPath, "items/potato.itemdef"};
    metadata.tags.push_back(epidemic::foundation::StringId::FromString("food"));

    const auto register_result = catalog.RegisterAsset(metadata);
    if (!register_result)
    {
        return false;
    }

    const auto by_id = catalog.FindById(metadata.id);
    const auto by_type = catalog.FindByType(metadata.type);
    const auto by_tag = catalog.FindByTag(metadata.tags.front());

    return catalog.Contains(metadata.id) && by_id.has_value() && by_id->id == metadata.id && by_type.size() == 1 &&
           by_type.front().id == metadata.id && by_tag.size() == 1 && by_tag.front().id == metadata.id;
}
} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(AssetMetadata{}.content_hash), std::uint64_t>);
    static_assert(std::is_same_v<decltype(AssetMetadata{}.version), std::uint32_t>);
    static_assert(std::has_virtual_destructor_v<IAssetCatalog>);

    if (!TestDefaultMetadataState())
    {
        return 1;
    }

    if (!TestMetadataCarriesDependenciesAndTags())
    {
        return 2;
    }

    if (!TestAssetCatalogContractCompilesAndReturnsSnapshots())
    {
        return 3;
    }

    return 0;
}
