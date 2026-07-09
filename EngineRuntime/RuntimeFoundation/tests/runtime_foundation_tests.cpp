#include "Epidemic/Runtime/Foundation/runtime_foundation.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"

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
using epidemic::runtime::ResourceHandle;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SimulationLod;

bool TestDefaultInvalidIds()
{
    const AssetId asset_id{};
    const ResourceId resource_id{};
    const RuntimeObjectId runtime_object_id{};

    return !asset_id.IsValid() && !resource_id.IsValid() && !runtime_object_id.IsValid();
}

bool TestEquality()
{
    const RuntimeObjectId left{42};
    const RuntimeObjectId same{42};
    const RuntimeObjectId different{7};

    return left == same && !(left == different);
}

bool TestHashWorksInUnorderedMap()
{
    std::unordered_map<ChunkId, std::uint32_t> chunk_load_order;
    chunk_load_order.emplace(ChunkId{11}, 3u);

    const auto iterator = chunk_load_order.find(ChunkId{11});
    return iterator != chunk_load_order.end() && iterator->second == 3u;
}

bool TestAssetAndResourceIdsUseStringBackedRuntimeIds()
{
    const AssetId asset_id = AssetId::FromString("items/potato.itemdef");
    const ResourceId resource_id = ResourceId::FromString("resources/potato.mesh");

    return asset_id.IsValid() && resource_id.IsValid() && asset_id.Raw() != resource_id.Raw();
}

bool TestRuntimeFoundationAndResourcesHeadersCanCoexist()
{
    const ResourceHandle handle{};
    return !handle.IsValid();
}

bool TestRuntimeBudgetDefaultsToEmpty()
{
    const RuntimeBudget budget{};
    return budget.IsEmpty() && !budget.HasTimeBudget() && !budget.HasItemBudget() && !budget.HasByteBudget();
}

bool TestRuntimeBudgetTracksLimits()
{
    const RuntimeBudget budget{std::chrono::microseconds{250}, 8u, 4096u};
    return budget.HasTimeBudget() && budget.HasItemBudget() && budget.HasByteBudget() && !budget.IsEmpty();
}

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

bool TestStateOrderingRepresentsEscalation()
{
    return static_cast<int>(ResidencyState::Unloaded) < static_cast<int>(ResidencyState::Resident) &&
           static_cast<int>(ResidencyState::Resident) < static_cast<int>(ResidencyState::Active) &&
           static_cast<int>(SimulationLod::Dormant) < static_cast<int>(SimulationLod::Observed) &&
           static_cast<int>(SimulationLod::Observed) < static_cast<int>(SimulationLod::Active) &&
           static_cast<int>(PersistenceTier::Disposable) < static_cast<int>(PersistenceTier::PlayerTouched) &&
           static_cast<int>(PersistenceTier::PlayerTouched) < static_cast<int>(PersistenceTier::QuestCritical);
}

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

int main()
{
    static_assert(!std::is_same_v<AssetId, ResourceId>);
    static_assert(static_cast<int>(ResidencyState::Unloaded) != static_cast<int>(ResidencyState::Active));
    static_assert(static_cast<int>(ObjectRealityLevel::AbstractFact) < static_cast<int>(ObjectRealityLevel::Physical));
    static_assert(static_cast<int>(PersistenceTier::Disposable) != static_cast<int>(PersistenceTier::QuestCritical));
    static_assert(static_cast<int>(SimulationLod::Dormant) != static_cast<int>(SimulationLod::Active));

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

    if (!TestRuntimeFoundationAndResourcesHeadersCanCoexist())
    {
        return 5;
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

    if (!TestStateOrderingRepresentsEscalation())
    {
        return 9;
    }

    if (!TestObjectRealityHelpersAreExplicit())
    {
        return 10;
    }

    return 0;
}
