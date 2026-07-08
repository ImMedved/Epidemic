#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Assets/asset_location.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"

namespace epidemic::runtime
{
class IAssetLocationResolver
{
  public:
    virtual ~IAssetLocationResolver() = default;

    [[nodiscard]] virtual foundation::Result<AssetLocation> Resolve(AssetId id) const = 0;
};
} // namespace epidemic::runtime
