#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"
#include "Epidemic/Runtime/World/object_placement.h"

#include <cstdint>

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
    ObjectPlacement placement{HiddenPlacement{}};
    std::uint64_t revision = 0;

    [[nodiscard]] bool operator==(const WorldObjectRecord&) const noexcept = default;
};

using WorldObjectSnapshot = WorldObjectRecord;
} // namespace epidemic::runtime
