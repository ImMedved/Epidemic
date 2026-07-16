#include "Epidemic/Runtime/Foundation/runtime_foundation.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace
{
using epidemic::runtime::AssetId;
using epidemic::runtime::AsyncOperationStatus;
using epidemic::runtime::ChunkId;
using epidemic::runtime::GameDuration;
using epidemic::runtime::GameTimePoint;
using epidemic::runtime::ObjectRealityLevel;
using epidemic::runtime::PersistenceTier;
using epidemic::runtime::Quat;
using epidemic::runtime::ResidencyState;
using epidemic::runtime::ResourceId;
using epidemic::runtime::RuntimeBudget;
using epidemic::runtime::RuntimeObjectId;
using epidemic::runtime::SimulationLod;
using epidemic::runtime::Transform;
using epidemic::runtime::Vec3;

bool Near(float left, float right)
{
    return std::abs(left - right) <= 0.001f;
}

bool Near(Vec3 left, Vec3 right)
{
    return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
}

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

// Verifies typed game time operations stay deterministic and raw-integer free.
bool TestGameTimeOperations()
{
    const GameTimePoint start{100};
    const GameDuration delta{25};
    const GameTimePoint end = start + delta;

    return !delta.IsZero() && end.ticks == 125 && (end - delta).ticks == 100 && (end - start).ticks == 25;
}

bool TestGameTimeOverflowHelpers()
{
    const auto overflow = epidemic::runtime::CheckedAdd(
        GameTimePoint{std::numeric_limits<std::int64_t>::max()},
        GameDuration{1});
    const auto underflow = epidemic::runtime::CheckedSubtract(
        GameTimePoint{std::numeric_limits<std::int64_t>::min()},
        GameDuration{1});
    const auto valid = epidemic::runtime::CheckedAdd(GameTimePoint{10}, GameDuration{5});

    return !overflow.has_value() && !underflow.has_value() && valid.has_value() && valid->ticks == 15;
}

bool TestQuaternionAndTransformMath()
{
    const float half_turn = 0.70710677f;
    const Quat rotate_z_90{0.0f, 0.0f, half_turn, half_turn};

    const Vec3 rotated = epidemic::runtime::RotateVector(rotate_z_90, Vec3{1.0f, 0.0f, 0.0f});
    if (!Near(rotated, Vec3{0.0f, 1.0f, 0.0f}))
    {
        return false;
    }

    const Transform parent{Vec3{10.0f, 0.0f, 0.0f}, rotate_z_90, Vec3{2.0f, 3.0f, 1.0f}};
    const Transform local{Vec3{1.0f, 0.0f, 0.0f}, {}, Vec3{1.0f, 2.0f, 1.0f}};
    const Transform composed = epidemic::runtime::ComposeTransform(parent, local);

    return Near(composed.position, Vec3{10.0f, 2.0f, 0.0f}) &&
           Near(composed.scale, Vec3{2.0f, 6.0f, 1.0f}) &&
           epidemic::runtime::IsNormalized(composed.rotation);
}

bool TestTransformAabb()
{
    const float half_turn = 0.70710677f;
    const Transform transform{Vec3{1.0f, 2.0f, 0.0f}, Quat{0.0f, 0.0f, half_turn, half_turn}, Vec3{2.0f, 1.0f, 1.0f}};
    const auto bounds = epidemic::runtime::TransformAabb(transform, epidemic::runtime::Aabb{Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f}});

    return Near(bounds.min, Vec3{0.0f, 0.0f, 0.0f}) && Near(bounds.max, Vec3{2.0f, 4.0f, 0.0f});
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

    if (!TestGameTimeOperations())
    {
        return 8;
    }

    if (!TestGameTimeOverflowHelpers())
    {
        return 9;
    }

    if (!TestQuaternionAndTransformMath())
    {
        return 10;
    }

    if (!TestTransformAabb())
    {
        return 11;
    }

    if (!TestOperationHelpers())
    {
        return 12;
    }

    if (!TestExplicitStateHelpers())
    {
        return 13;
    }

    if (!TestObjectRealityHelpersAreExplicit())
    {
        return 14;
    }

    return 0;
}


