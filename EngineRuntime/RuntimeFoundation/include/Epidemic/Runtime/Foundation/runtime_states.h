#pragma once

namespace epidemic::runtime
{
enum class ResidencyState
{
    Unloaded,
    Loading,
    Resident,
    Active,
    Sleeping,
    Unloading,
};

[[nodiscard]] constexpr bool CanTransition(ResidencyState from, ResidencyState to) noexcept
{
    if (from == to)
    {
        return true;
    }

    switch (from)
    {
    case ResidencyState::Unloaded:
        return to == ResidencyState::Loading || to == ResidencyState::Resident;
    case ResidencyState::Loading:
        return to == ResidencyState::Resident || to == ResidencyState::Unloaded;
    case ResidencyState::Resident:
        return to == ResidencyState::Active || to == ResidencyState::Sleeping || to == ResidencyState::Unloading;
    case ResidencyState::Active:
        return to == ResidencyState::Resident || to == ResidencyState::Sleeping || to == ResidencyState::Unloading;
    case ResidencyState::Sleeping:
        return to == ResidencyState::Resident || to == ResidencyState::Active || to == ResidencyState::Unloading;
    case ResidencyState::Unloading:
        return to == ResidencyState::Unloaded;
    }

    return false;
}

enum class ObjectRealityLevel
{
    AbstractFact,
    Logical,
    Physical,
};

[[nodiscard]] constexpr int RealityRank(ObjectRealityLevel level) noexcept
{
    switch (level)
    {
    case ObjectRealityLevel::AbstractFact:
        return 0;
    case ObjectRealityLevel::Logical:
        return 1;
    case ObjectRealityLevel::Physical:
        return 2;
    }

    return 0;
}

[[nodiscard]] constexpr bool IsMoreConcreteRealityLevel(ObjectRealityLevel left, ObjectRealityLevel right) noexcept
{
    return RealityRank(left) > RealityRank(right);
}

[[nodiscard]] constexpr bool IsLessConcreteRealityLevel(ObjectRealityLevel left, ObjectRealityLevel right) noexcept
{
    return RealityRank(left) < RealityRank(right);
}

enum class PersistenceTier
{
    Disposable,
    TemporaryObserved,
    PlayerTouched,
    Protected,
    QuestCritical,
};

[[nodiscard]] constexpr int PersistenceTierRank(PersistenceTier tier) noexcept
{
    switch (tier)
    {
    case PersistenceTier::Disposable:
        return 0;
    case PersistenceTier::TemporaryObserved:
        return 1;
    case PersistenceTier::PlayerTouched:
        return 2;
    case PersistenceTier::Protected:
        return 3;
    case PersistenceTier::QuestCritical:
        return 4;
    }

    return 0;
}

enum class SimulationLod
{
    Dormant,
    Abstract,
    Scheduled,
    Relevant,
    Observed,
    Active,
};

[[nodiscard]] constexpr int SimulationLodRank(SimulationLod lod) noexcept
{
    switch (lod)
    {
    case SimulationLod::Dormant:
        return 0;
    case SimulationLod::Abstract:
        return 1;
    case SimulationLod::Scheduled:
        return 2;
    case SimulationLod::Relevant:
        return 3;
    case SimulationLod::Observed:
        return 4;
    case SimulationLod::Active:
        return 5;
    }

    return 0;
}
} // namespace epidemic::runtime
