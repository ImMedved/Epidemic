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
    Physical,
    Logical,
    AbstractFact,
};

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
