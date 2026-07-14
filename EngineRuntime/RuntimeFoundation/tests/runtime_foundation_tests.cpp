#include "Epidemic/Runtime/Foundation/runtime_foundation.h"

// File note:
// Focused module-level tests for the surrounding runtime component. Each helper builds
// a narrow fixture, and each Test* function verifies one public contract or regression.
#include <chrono>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

namespace
{
using epidemic::runtime::AssetId;
using epidemic::runtime::AsyncOperationStatus;
using epidemic::runtime::ChunkId;
using epidemic::runtime::ObjectRealityLevel;
using epidemic::runtime::PersistenceTier;
using epidemic::runtime::ResidencyState;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SimulationLod;

// Verifies default invalid ids.
bool TestDefaultInvalidIds()
{
    const AssetId asset_id{};
    const ResourceId resource_id{};
    const RuntimeObjectId runtime_object_id{};

    return !asset_id.IsValid() && !resource_id.IsValid() && !runtime_object_id.IsValid();
}

// Verifies equality.
bool TestEquality()
{
    const RuntimeObjectId left{42};
    const RuntimeObjectId same{42};
    const RuntimeObjectId different{7};

    return left == same && !(left == different);
}

// Verifies hash works in unordered map.
bool TestHashWorksInUnorderedMap()
{
    std::unordered_map<ChunkId, std::uint32_t> chunk_load_order;
    chunk_load_order.emplace(ChunkId{11}, 3u);

    const auto iterator = chunk_load_order.find(ChunkId{11});
    return iterator != chunk_load_order.end() && iterator->second == 3u;
}

// Verifies asset and resource ids use string backed runtime ids.
bool TestAssetAndResourceIdsUseStringBackedRuntimeIds()
{
    const AssetId asset_id = AssetId::FromString("items/potato.itemdef");
    const ResourceId resource_id = ResourceId::FromString("resources/potato.mesh");

    return asset_id.IsValid() && resource_id.IsValid() && asset_id.Raw() != resource_id.Raw();
}

// Verifies runtime foundation and resources headers can coexist.
// Verifies runtime budget defaults to empty.
bool TestRuntimeBudgetDefaultsToEmpty()
{
    const RuntimeBudget budget{};
    return budget.IsUnlimited() && budget.IsEmpty() && !budget.HasTimeLimit() && !budget.HasItemLimit() && !budget.HasByteLimit();
}

// Verifies runtime budget tracks limits.
bool TestRuntimeBudgetTracksLimits()
{
    const RuntimeBudget budget{std::chrono::microseconds{250}, 8u, 4096u};
    return budget.HasTimeLimit() && budget.HasItemLimit() && budget.HasByteLimit() && !budget.IsUnlimited();
}

// Verifies operation helpers.
bool TestOperationHelpers()
{
    return epidemic::runtime::IsActiveOperationStatus(AsyncOperationStatus::Pending) &&
           epidemic::runtime::IsActiveOperationStatus(AsyncOperationStatus::Running) &&
           epidemic::runtime::IsActiveOperationStatus(AsyncOperationStatus::PartiallyComplete) &&
           epidemic::runtime::IsTerminalOperationStatus(AsyncOperationStatus::Completed) &&
           epidemic::runtime::IsTerminalOperationStatus(AsyncOperationStatus::Failed) &&
           epidemic::runtime::IsTerminalOperationStatus(AsyncOperationStatus::Cancelled) &&
           !epidemic::runtime::IsTerminalOperationStatus(AsyncOperationStatus::WaitingForMainThread) &&
           !epidemic::runtime::IsActiveOperationStatus(AsyncOperationStatus::Completed);
}

// Verifies state ordering represents escalation.
bool TestExplicitStateHelpers()
{
    return epidemic::runtime::CanTransition(ResidencyState::Unloaded, ResidencyState::Loading) &&
           epidemic::runtime::CanTransition(ResidencyState::Loading, ResidencyState::Resident) &&
           epidemic::runtime::CanTransition(ResidencyState::Resident, ResidencyState::Active) &&
           epidemic::runtime::CanTransition(ResidencyState::Active, ResidencyState::Unloading) &&
           epidemic::runtime::CanTransition(ResidencyState::Unloading, ResidencyState::Unloaded) &&
           !epidemic::runtime::CanTransition(ResidencyState::Unloaded, ResidencyState::Active) &&
           !epidemic::runtime::CanTransition(ResidencyState::Unloading, ResidencyState::Active) &&
           epidemic::runtime::SimulationLodRank(SimulationLod::Dormant) < epidemic::runtime::SimulationLodRank(SimulationLod::Observed) &&
           epidemic::runtime::PersistenceTierRank(PersistenceTier::Disposable) < epidemic::runtime::PersistenceTierRank(PersistenceTier::PlayerTouched);
}
// Verifies object reality helpers are explicit.
bool TestObjectRealityHelpersAreExplicit()
{
    return epidemic::runtime::RealityRank(ObjectRealityLevel::AbstractFact) <
               epidemic::runtime::RealityRank(ObjectRealityLevel::Logical) &&
           epidemic::runtime::RealityRank(ObjectRealityLevel::Logical) <
               epidemic::runtime::RealityRank(ObjectRealityLevel::Physical) &&
           epidemic::runtime::IsMoreConcreteRealityLevel(ObjectRealityLevel::Physical, ObjectRealityLevel::Logical) &&
           epidemic::runtime::IsLessConcreteRealityLevel(ObjectRealityLevel::AbstractFact, ObjectRealityLevel::Physical);
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    static_assert(!std::is_same_v<AssetId, ResourceId>);
    static_assert(!epidemic::runtime::CanTransition(ResidencyState::Unloaded, ResidencyState::Active));
    static_assert(epidemic::runtime::RealityRank(ObjectRealityLevel::AbstractFact) < epidemic::runtime::RealityRank(ObjectRealityLevel::Physical));
    static_assert(epidemic::runtime::PersistenceTierRank(PersistenceTier::Disposable) != epidemic::runtime::PersistenceTierRank(PersistenceTier::QuestCritical));
    static_assert(epidemic::runtime::SimulationLodRank(SimulationLod::Dormant) != epidemic::runtime::SimulationLodRank(SimulationLod::Active));

    if (!TestDefaultInvalidIds())
    {
        return 1;
    }

    if (!TestEquality())
    {
        return 2;
    }

    if (!TestHashWorksInUnorderedMap())
    {
        return 3;
    }

    if (!TestAssetAndResourceIdsUseStringBackedRuntimeIds())
    {
        return 4;
    }

    if (!TestRuntimeBudgetDefaultsToEmpty())
    {
        return 6;
    }

    if (!TestRuntimeBudgetTracksLimits())
    {
        return 7;
    }

    if (!TestOperationHelpers())
    {
        return 8;
    }

    if (!TestExplicitStateHelpers())
    {
        return 9;
    }

    if (!TestObjectRealityHelpersAreExplicit())
    {
        return 10;
    }

    return 0;
}


