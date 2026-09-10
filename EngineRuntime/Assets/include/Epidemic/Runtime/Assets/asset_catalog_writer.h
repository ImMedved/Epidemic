#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_metadata.h"

namespace epidemic::runtime
{
// Threading: mutation must occur on the runtime thread.
// Concurrent reads/writes are not supported unless explicitly documented.
class IAssetCatalogWriter
{
  public:
    virtual ~IAssetCatalogWriter() = default;

    // Preconditions: metadata identifiers/enums/location and bounded collection sizes
    // must satisfy the Assets contract. Semantic validation failures return Result failure
    // and leave the catalog unchanged. Process-wide allocation exhaustion (std::bad_alloc)
    // is intentionally not translated into a module-specific recoverable error.
    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(const AssetMetadata& metadata) = 0;
    // Seal is idempotent. Once sealed, successful/failed registration attempts cannot
    // reopen or otherwise mutate the catalog.
    [[nodiscard]] virtual foundation::Result<void> Seal() = 0;
};
} // namespace epidemic::runtime
