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
    // is intentionally not translated into a module-specific recoverable error and also
    // leaves the pre-call catalog observable state unchanged. Registration stores one
    // normalized detached copy. Metadata version is per asset, not a global catalog revision.
    [[nodiscard]] virtual foundation::Result<void> RegisterAsset(const AssetMetadata& metadata) = 0;
    // Seal is idempotent. Once sealed, successful/failed registration attempts cannot
    // reopen or otherwise mutate the catalog. Calling Seal() on an already sealed catalog
    // is a semantic no-op that still returns success.
    [[nodiscard]] virtual foundation::Result<void> Seal() = 0;
};
} // namespace epidemic::runtime
