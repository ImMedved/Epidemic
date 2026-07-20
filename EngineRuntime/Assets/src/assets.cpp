#include "Epidemic/Runtime/Assets/asset_services.h"
#include "in_memory_asset_catalog.h"

namespace epidemic::runtime
{
foundation::Result<AssetServices> CreateAssetServices(const AssetsOptions&)
{
    auto catalog = std::make_shared<InMemoryAssetCatalog>();
    AssetServices services{};
    services.catalog = catalog;
    services.writer = catalog;
    services.location_resolver = catalog;
    return foundation::Result<AssetServices>::Success(std::move(services));
}
} // namespace epidemic::runtime
