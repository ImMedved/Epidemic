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

enum class SimulationLod
{
    Dormant,
    Abstract,
    Scheduled,
    Relevant,
    Observed,
    Active,
};
} // namespace epidemic::runtime
