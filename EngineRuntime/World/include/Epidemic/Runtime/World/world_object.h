#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"
#include "Epidemic/Runtime/World/object_placement.h"

namespace epidemic::runtime
{
struct WorldObjectRecord
{
    RuntimeObjectId runtime_id{};
    PersistentObjectId persistent_id{};
    AssetId asset_id{};
    ObjectRealityLevel reality = ObjectRealityLevel::Logical;
    ResidencyState residency = ResidencyState::Unloaded;
    PersistenceTier persistence_tier = PersistenceTier::Disposable;
    ObjectPlacement placement{};

    [[nodiscard]] constexpr bool operator==(const WorldObjectRecord&) const noexcept = default;
};
} // namespace epidemic::runtime
