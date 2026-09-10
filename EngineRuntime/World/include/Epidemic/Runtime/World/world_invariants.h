#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"
#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_query.h"

namespace epidemic::runtime
{
[[nodiscard]] constexpr bool IsValidResidencyState(ResidencyState state) noexcept
{
    switch (state)
    {
    case ResidencyState::Unloaded:
    case ResidencyState::Loading:
    case ResidencyState::Resident:
    case ResidencyState::Active:
    case ResidencyState::Sleeping:
    case ResidencyState::Unloading:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidObjectRealityLevel(ObjectRealityLevel level) noexcept
{
    switch (level)
    {
    case ObjectRealityLevel::AbstractFact:
    case ObjectRealityLevel::Logical:
    case ObjectRealityLevel::Physical:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidPersistenceTier(PersistenceTier tier) noexcept
{
    switch (tier)
    {
    case PersistenceTier::Disposable:
    case PersistenceTier::TemporaryObserved:
    case PersistenceTier::PlayerTouched:
    case PersistenceTier::Protected:
    case PersistenceTier::QuestCritical:
        return true;
    }
    return false;
}

[[nodiscard]] foundation::Result<void> ValidateRealityResidencyCombination(ObjectRealityLevel reality, ResidencyState residency);

[[nodiscard]] foundation::Result<void> ValidateWorldObjectInvariant(
    const WorldObjectSnapshot& object,
    const IRegionRegistry& regions,
    const IChunkRegistry& chunks,
    const IWorldQuery& objects);
} // namespace epidemic::runtime
