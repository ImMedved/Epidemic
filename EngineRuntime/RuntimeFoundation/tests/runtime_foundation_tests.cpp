#include "Epidemic/Runtime/Foundation/runtime_foundation.h"
#include "Epidemic/Runtime/Foundation/runtime_handles.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_map>

namespace
{
using epidemic::runtime::AllocateMonotonicId;
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
using epidemic::runtime::RuntimeFrameDuration;
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

// Verifies monotonic allocators issue the last valid value once and never wrap to zero.
bool TestCheckedMonotonicIdAllocatorExhaustion()
{
    std::uint64_t next = std::numeric_limits<std::uint64_t>::max() - 1u;
    const auto penultimate = AllocateMonotonicId(next);
    const auto last = AllocateMonotonicId(next);
    const auto exhausted = AllocateMonotonicId(next);
    return penultimate && penultimate.Value() == std::numeric_limits<std::uint64_t>::max() - 1u &&
           last && last.Value() == std::numeric_limits<std::uint64_t>::max() && next == 0u &&
           !exhausted && exhausted.GetError().HasCode("runtime.id_exhausted");
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

bool TestRuntimeBudgetDefaultsToUnlimited()
{
    const RuntimeBudget budget{};
    return budget.IsUnlimited() && !budget.HasTimeLimit() && !budget.HasItemLimit() && !budget.HasByteLimit();
}

// Verifies runtime budget tracks limits.
bool TestRuntimeBudgetTracksLimits()
{
    const RuntimeBudget budget{std::chrono::microseconds{250}, 8u, 4096u};
    return budget.HasTimeLimit() && budget.HasItemLimit() && budget.HasByteLimit() &&
           budget.HasTimeBudget() && budget.HasItemBudget() && budget.HasByteBudget() && !budget.IsUnlimited();
}

bool TestRuntimeBudgetRejectsNegativeTime()
{
    const RuntimeBudget negative{std::chrono::microseconds{-1}, 0, 0};
    const RuntimeBudget zero{};
    const auto invalid = epidemic::runtime::ValidateRuntimeBudget(negative);
    const auto valid = epidemic::runtime::ValidateRuntimeBudget(zero);
    return !invalid && invalid.GetError().HasCode("runtime.invalid_budget") && valid;
}

bool TestTransactionalMonotonicReservation()
{
    std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
    auto reserved = epidemic::runtime::ReserveMonotonicId(next);
    if (!reserved || reserved.Value().Value() != std::numeric_limits<std::uint64_t>::max() || next != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    reserved.Value().Rollback();
    if (next != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    auto retry = epidemic::runtime::ReserveMonotonicId(next);
    if (!retry || retry.Value().Value() != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    retry.Value().Commit();
    return next == 0 && !epidemic::runtime::PeekMonotonicId(next);
}

bool TestCheckedArithmeticBoundaries()
{
    const auto inc = epidemic::runtime::CheckedRevisionIncrement(std::numeric_limits<std::uint64_t>::max() - 1);
    const auto exhausted = epidemic::runtime::CheckedRevisionIncrement(std::numeric_limits<std::uint64_t>::max());
    const auto add_ok = epidemic::runtime::CheckedUnsignedAdd<std::uint64_t>(std::numeric_limits<std::uint64_t>::max() - 1, 1);
    const auto add_bad = epidemic::runtime::CheckedUnsignedAdd<std::uint64_t>(std::numeric_limits<std::uint64_t>::max(), 1);
    const auto frame_ok = epidemic::runtime::CheckedAdd(RuntimeFrameDuration{std::chrono::microseconds{10}}, RuntimeFrameDuration{std::chrono::microseconds{5}});
    const auto frame_bad = epidemic::runtime::CheckedAdd(RuntimeFrameDuration{std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()}}, RuntimeFrameDuration{std::chrono::microseconds{1}});
    const auto seconds_bad = epidemic::runtime::CheckedSecondsToMicroseconds(std::numeric_limits<double>::max());
    return inc && *inc == std::numeric_limits<std::uint64_t>::max() && !exhausted &&
           add_ok && *add_ok == std::numeric_limits<std::uint64_t>::max() && !add_bad &&
           frame_ok && frame_ok->value.count() == 15 && !frame_bad && !seconds_bad;
}

bool TestRuntimeFrameDurationUsesRealMicroseconds()
{
    const RuntimeFrameDuration zero{};
    const RuntimeFrameDuration negative{std::chrono::microseconds{-1}};
    const RuntimeFrameDuration positive{std::chrono::microseconds{16667}};

    return zero.IsZero() && !zero.IsNegative() &&
           negative.IsNegative() &&
           positive.value.count() == 16667 &&
           negative < positive;
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
    constexpr std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    const auto overflow = epidemic::runtime::CheckedAdd(
        GameTimePoint{maximum},
        GameDuration{1});
    const auto underflow = epidemic::runtime::CheckedSubtract(
        GameTimePoint{minimum},
        GameDuration{1});
    const auto min_duration_valid_subtract = epidemic::runtime::CheckedSubtract(
        GameTimePoint{-1},
        GameDuration{minimum});
    const auto difference_overflow = epidemic::runtime::CheckedDifference(GameTimePoint{maximum}, GameTimePoint{-1});
    const auto valid = epidemic::runtime::CheckedAdd(GameTimePoint{10}, GameDuration{5});

    return !overflow.has_value() && !underflow.has_value() &&
           min_duration_valid_subtract.has_value() && min_duration_valid_subtract->ticks == maximum &&
           !difference_overflow.has_value() && valid.has_value() && valid->ticks == 15;
}

bool TestGameTimeOperatorsSaturateAtInt64Edges()
{
    constexpr std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();

    const GameTimePoint add_overflow = GameTimePoint{maximum} + GameDuration{1};
    const GameTimePoint subtract_underflow = GameTimePoint{minimum} - GameDuration{1};
    const GameDuration difference_overflow = GameTimePoint{maximum} - GameTimePoint{-1};
    const GameDuration difference_underflow = GameTimePoint{minimum} - GameTimePoint{1};

    return add_overflow.ticks == maximum &&
           subtract_underflow.ticks == minimum &&
           difference_overflow.ticks == maximum &&
           difference_underflow.ticks == minimum;
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

bool TestNestedRotatedTransformCompositionAndNegativeScale()
{
    const float half_turn = 0.70710677f;
    const Quat rotate_z_90{0.0f, 0.0f, half_turn, half_turn};
    const Transform root{Vec3{2.0f, 0.0f, 0.0f}, rotate_z_90, Vec3{-1.0f, 1.0f, 1.0f}};
    const Transform middle{Vec3{0.0f, 3.0f, 0.0f}, rotate_z_90, Vec3{1.0f, 2.0f, 1.0f}};
    const Transform leaf{Vec3{1.0f, 0.0f, 0.0f}, {}, Vec3{1.0f, 1.0f, 1.0f}};

    const Transform composed = epidemic::runtime::ComposeTransform(epidemic::runtime::ComposeTransform(root, middle), leaf);
    const Vec3 transformed_origin = epidemic::runtime::TransformPoint(composed, Vec3{0.0f, 0.0f, 0.0f});

    return epidemic::runtime::IsValidTransform(root) &&
           epidemic::runtime::IsValidTransform(composed) &&
           Near(transformed_origin, Vec3{0.0f, 0.0f, 0.0f});
}

bool TestInvalidAndZeroQuaternionValidation()
{
    const Quat zero{0.0f, 0.0f, 0.0f, 0.0f};
    const Quat invalid{std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 1.0f};
    const Quat normalized_zero = epidemic::runtime::Normalize(zero);
    const Transform zero_rotation_transform{{}, zero, Vec3{1.0f, 1.0f, 1.0f}};
    const Transform invalid_rotation_transform{{}, invalid, Vec3{1.0f, 1.0f, 1.0f}};

    return normalized_zero == Quat{} &&
           !epidemic::runtime::IsNormalized(zero) &&
           !epidemic::runtime::IsFinite(invalid) &&
           !epidemic::runtime::IsValidTransform(zero_rotation_transform) &&
           !epidemic::runtime::IsValidTransform(invalid_rotation_transform);
}

bool TestTransformAabb()
{
    const float half_turn = 0.70710677f;
    const Transform transform{Vec3{1.0f, 2.0f, 0.0f}, Quat{0.0f, 0.0f, half_turn, half_turn}, Vec3{2.0f, 1.0f, 1.0f}};
    const auto bounds = epidemic::runtime::TransformAabb(transform, epidemic::runtime::Aabb{Vec3{-1.0f, -1.0f, 0.0f}, Vec3{1.0f, 1.0f, 0.0f}});
    const auto translated = epidemic::runtime::TranslateBounds(
        epidemic::runtime::Aabb{Vec3{-1.0f, 0.0f, 2.0f}, Vec3{1.0f, 2.0f, 4.0f}}, Vec3{3.0f, -1.0f, 5.0f});

    return Near(bounds.min, Vec3{0.0f, 0.0f, 0.0f}) && Near(bounds.max, Vec3{2.0f, 4.0f, 0.0f}) &&
           translated == epidemic::runtime::Aabb{Vec3{2.0f, -1.0f, 7.0f}, Vec3{4.0f, 1.0f, 9.0f}};
}


bool TestAllRuntimeIdFamiliesAndHashConsistency()
{
    using epidemic::runtime::ChunkId;
    using epidemic::runtime::LazyRuleId;
    using epidemic::runtime::PersistentObjectId;
    using epidemic::runtime::RegionId;
    using epidemic::runtime::SimulationZoneId;
    using epidemic::runtime::SurfaceId;

    const AssetId empty_asset = AssetId::FromString("");
    const ResourceId empty_resource = ResourceId::FromString("");
    const AssetId asset = AssetId::FromString("shared/name");
    const ResourceId resource = ResourceId::FromString("shared/name");

    const auto hash_matches = []<typename T>(T left, T right) {
        return left == right && std::hash<T>{}(left) == std::hash<T>{}(right);
    };

    return !empty_asset.IsValid() && empty_asset.Raw() == 0 &&
           !empty_resource.IsValid() && empty_resource.Raw() == 0 &&
           asset.IsValid() && resource.IsValid() && asset.Raw() == resource.Raw() &&
           !RuntimeObjectId{}.IsValid() && !PersistentObjectId{}.IsValid() && !RegionId{}.IsValid() &&
           !ChunkId{}.IsValid() && !SurfaceId{}.IsValid() && !SimulationZoneId{}.IsValid() && !LazyRuleId{}.IsValid() &&
           hash_matches(RuntimeObjectId{17}, RuntimeObjectId{17}) &&
           hash_matches(PersistentObjectId{17}, PersistentObjectId{17}) &&
           hash_matches(RegionId{17}, RegionId{17}) &&
           hash_matches(ChunkId{17}, ChunkId{17}) &&
           hash_matches(SurfaceId{17}, SurfaceId{17}) &&
           hash_matches(SimulationZoneId{17}, SimulationZoneId{17}) &&
           hash_matches(LazyRuleId{17}, LazyRuleId{17}) &&
           hash_matches(asset, AssetId::FromString("shared/name")) &&
           hash_matches(resource, ResourceId::FromString("shared/name"));
}

bool TestMonotonicIdHelperBoundariesAndNoOpCommit()
{
    std::uint64_t next = 1;
    const auto peek_one = epidemic::runtime::PeekMonotonicId(next);
    if (!peek_one || peek_one.Value() != 1 || next != 1)
    {
        return false;
    }

    auto reservation = epidemic::runtime::ReserveMonotonicId(next);
    if (!reservation || !reservation.Value().IsValid() || reservation.Value().Value() != 1 || next != 1)
    {
        return false;
    }
    reservation.Value().Commit();
    if (next != 2 || reservation.Value().IsValid())
    {
        return false;
    }
    reservation.Value().Commit();
    if (next != 2)
    {
        return false;
    }

    auto rolled_back = epidemic::runtime::ReserveMonotonicId(next);
    if (!rolled_back || rolled_back.Value().Value() != 2)
    {
        return false;
    }
    rolled_back.Value().Rollback();
    if (next != 2 || rolled_back.Value().IsValid())
    {
        return false;
    }

    epidemic::runtime::CommitMonotonicId(next, static_cast<std::uint64_t>(99));
    if (next != 2)
    {
        return false;
    }

    const auto allocated_normal = AllocateMonotonicId(next);
    if (!allocated_normal || allocated_normal.Value() != 2 || next != 3)
    {
        return false;
    }

    next = std::numeric_limits<std::uint64_t>::max();
    const auto peek_max = epidemic::runtime::PeekMonotonicId(next);
    if (!peek_max || peek_max.Value() != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    auto reserve_max = epidemic::runtime::ReserveMonotonicId(next);
    if (!reserve_max || reserve_max.Value().Value() != std::numeric_limits<std::uint64_t>::max())
    {
        return false;
    }
    reserve_max.Value().Commit();
    if (next != 0)
    {
        return false;
    }

    const auto exhausted_peek = epidemic::runtime::PeekMonotonicId(next);
    const auto exhausted_reserve = epidemic::runtime::ReserveMonotonicId(next);
    const auto exhausted_allocate = AllocateMonotonicId(next);
    return !epidemic::runtime::CanAllocateMonotonicId(next) && !exhausted_peek && !exhausted_reserve &&
           !exhausted_allocate && next == 0;
}

bool TestNumericValidationContracts()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const auto generation_ok = epidemic::runtime::CheckedGenerationIncrement<std::uint64_t>(41);
    const auto generation_exhausted = epidemic::runtime::CheckedGenerationIncrement<std::uint64_t>(std::numeric_limits<std::uint64_t>::max());

    return epidemic::runtime::IsFinite(0.0) && epidemic::runtime::IsFinite(-1.0) &&
           !epidemic::runtime::IsFinite(nan) && !epidemic::runtime::IsFinite(infinity) &&
           epidemic::runtime::IsFinitePositive(1.0) && !epidemic::runtime::IsFinitePositive(0.0) &&
           !epidemic::runtime::IsFinitePositive(-1.0) && !epidemic::runtime::IsFinitePositive(nan) &&
           epidemic::runtime::IsFiniteNonNegative(0.0) && epidemic::runtime::IsFiniteNonNegative(1.0) &&
           !epidemic::runtime::IsFiniteNonNegative(-1.0) && !epidemic::runtime::IsFiniteNonNegative(infinity) &&
           generation_ok && *generation_ok == 42 && !generation_exhausted;
}

bool TestRuntimeBudgetMaximumUnsignedLimits()
{
    const RuntimeBudget maximum{
        std::chrono::microseconds{std::numeric_limits<std::int64_t>::max()},
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint64_t>::max()};
    const auto valid = epidemic::runtime::ValidateRuntimeBudget(maximum);
    return valid && maximum.HasTimeLimit() && maximum.HasItemLimit() && maximum.HasByteLimit() &&
           maximum.max_items == std::numeric_limits<std::uint32_t>::max() &&
           maximum.max_bytes == std::numeric_limits<std::uint64_t>::max();
}

bool TestCheckedSecondsToMicrosecondsBoundaries()
{
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const double rounded_boundary_seconds = static_cast<double>(maximum) / 1000000.0;
    const double overflow_seconds = std::nextafter(rounded_boundary_seconds, std::numeric_limits<double>::infinity());

    const auto zero = epidemic::runtime::CheckedSecondsToMicroseconds(0.0);
    const auto one = epidemic::runtime::CheckedSecondsToMicroseconds(1.0);
    const auto rounded_boundary = epidemic::runtime::CheckedSecondsToMicroseconds(rounded_boundary_seconds);
    const auto overflow = epidemic::runtime::CheckedSecondsToMicroseconds(overflow_seconds);
    const auto negative = epidemic::runtime::CheckedSecondsToMicroseconds(-0.000001);
    const auto nan = epidemic::runtime::CheckedSecondsToMicroseconds(std::numeric_limits<double>::quiet_NaN());
    const auto infinity = epidemic::runtime::CheckedSecondsToMicroseconds(std::numeric_limits<double>::infinity());

    if (!zero || zero->value.count() != 0 || !one || one->value.count() != 1000000 || !rounded_boundary ||
        rounded_boundary->value.count() != maximum - 417 || overflow || negative || nan || infinity)
    {
        return false;
    }

    const double boundaries[] = {0.5, 0.25, 0.125, 0.0625, 0.03125, 0.015625};
    const std::int64_t expected_microseconds[] = {500000, 250000, 125000, 62500, 31250, 15625};
    for (std::size_t index = 0; index < std::size(boundaries); ++index)
    {
        const auto below = epidemic::runtime::CheckedSecondsToMicroseconds(std::nextafter(boundaries[index], 0.0));
        const auto exact = epidemic::runtime::CheckedSecondsToMicroseconds(boundaries[index]);
        const auto above = epidemic::runtime::CheckedSecondsToMicroseconds(
            std::nextafter(boundaries[index], std::numeric_limits<double>::infinity()));
        if (!below || !exact || !above || below->value.count() != expected_microseconds[index] - 1 ||
            exact->value.count() != expected_microseconds[index] || above->value.count() != expected_microseconds[index])
        {
            return false;
        }
    }
    return true;
}

bool TestCheckedScaleDurationBoundaries()
{
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const RuntimeFrameDuration max_duration{std::chrono::microseconds{maximum}};
    const auto unchanged = epidemic::runtime::CheckedScaleDuration(max_duration, 1.0);
    const auto zero = epidemic::runtime::CheckedScaleDuration(max_duration, 0.0);
    const auto zero_duration = epidemic::runtime::CheckedScaleDuration(
        RuntimeFrameDuration{std::chrono::microseconds{0}}, std::numeric_limits<double>::max());
    const auto half = epidemic::runtime::CheckedScaleDuration(max_duration, 0.5);
    const auto tenth = epidemic::runtime::CheckedScaleDuration(max_duration, 0.1);
    const auto truncated = epidemic::runtime::CheckedScaleDuration(
        RuntimeFrameDuration{std::chrono::microseconds{3}}, 0.5);
    const auto overflow = epidemic::runtime::CheckedScaleDuration(max_duration, std::nextafter(1.0, 2.0));
    const auto negative_duration = epidemic::runtime::CheckedScaleDuration(RuntimeFrameDuration{std::chrono::microseconds{-1}}, 1.0);
    const auto negative_rate = epidemic::runtime::CheckedScaleDuration(RuntimeFrameDuration{std::chrono::microseconds{1}}, -1.0);
    const auto nan_rate = epidemic::runtime::CheckedScaleDuration(RuntimeFrameDuration{std::chrono::microseconds{1}}, std::numeric_limits<double>::quiet_NaN());
    const auto infinite_rate = epidemic::runtime::CheckedScaleDuration(RuntimeFrameDuration{std::chrono::microseconds{1}}, std::numeric_limits<double>::infinity());

    return unchanged && unchanged->value.count() == maximum && zero && zero->value.count() == 0 &&
           zero_duration && zero_duration->value.count() == 0 &&
           half && half->value.count() == 4611686018427387903LL &&
           tenth && tenth->value.count() == 922337203685477631LL && truncated && truncated->value.count() == 1 &&
           !overflow && !negative_duration && !negative_rate && !nan_rate && !infinite_rate;
}

bool TestGameTimeInt64MinimumMaximumBoundaries()
{
    constexpr std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();

    const auto add_min_exact = epidemic::runtime::CheckedAdd(GameTimePoint{0}, GameDuration{minimum});
    const auto add_min_underflow = epidemic::runtime::CheckedAdd(GameTimePoint{-1}, GameDuration{minimum});
    const auto subtract_min_exact = epidemic::runtime::CheckedSubtract(GameTimePoint{-1}, GameDuration{minimum});
    const auto subtract_min_overflow = epidemic::runtime::CheckedSubtract(GameTimePoint{0}, GameDuration{minimum});
    const auto difference_max = epidemic::runtime::CheckedDifference(GameTimePoint{maximum}, GameTimePoint{0});
    const auto difference_min = epidemic::runtime::CheckedDifference(GameTimePoint{minimum}, GameTimePoint{0});
    const auto difference_overflow = epidemic::runtime::CheckedDifference(GameTimePoint{maximum}, GameTimePoint{minimum});
    const auto difference_underflow = epidemic::runtime::CheckedDifference(GameTimePoint{minimum}, GameTimePoint{maximum});

    return add_min_exact && add_min_exact->ticks == minimum && !add_min_underflow &&
           subtract_min_exact && subtract_min_exact->ticks == maximum && !subtract_min_overflow &&
           difference_max && difference_max->ticks == maximum && difference_min && difference_min->ticks == minimum &&
           !difference_overflow && !difference_underflow &&
           epidemic::runtime::SaturatingAdd(GameTimePoint{-1}, GameDuration{minimum}).ticks == minimum &&
           epidemic::runtime::SaturatingSubtract(GameTimePoint{0}, GameDuration{minimum}).ticks == maximum &&
           epidemic::runtime::SaturatingDifference(GameTimePoint{maximum}, GameTimePoint{minimum}).ticks == maximum &&
           epidemic::runtime::SaturatingDifference(GameTimePoint{minimum}, GameTimePoint{maximum}).ticks == minimum;
}

bool TestSpatialFiniteValidationFamilies()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    const Vec3 finite{1.0f, -2.0f, 3.0f};
    const Quat identity{};

    if (!epidemic::runtime::IsFinite(finite) || epidemic::runtime::IsFinite(Vec3{nan, 0.0f, 0.0f}) ||
        epidemic::runtime::IsFinite(Vec3{0.0f, infinity, 0.0f}) || epidemic::runtime::IsFinite(Vec3{0.0f, 0.0f, nan}) ||
        !epidemic::runtime::IsFinite(identity) || epidemic::runtime::IsFinite(Quat{nan, 0.0f, 0.0f, 1.0f}) ||
        epidemic::runtime::IsFinite(Quat{0.0f, infinity, 0.0f, 1.0f}) || epidemic::runtime::IsFinite(Quat{0.0f, 0.0f, nan, 1.0f}) ||
        epidemic::runtime::IsFinite(Quat{0.0f, 0.0f, 0.0f, infinity}))
    {
        return false;
    }

    const Transform valid{finite, identity, Vec3{1.0f, -2.0f, 3.0f}};
    const Transform bad_position{Vec3{nan, 0.0f, 0.0f}, identity, Vec3{1.0f, 1.0f, 1.0f}};
    const Transform bad_rotation{{}, Quat{0.0f, 0.0f, 0.0f, 2.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    const Transform bad_scale{{}, identity, Vec3{1.0f, infinity, 1.0f}};
    const Transform zero_scale_x{{}, identity, Vec3{0.0f, 1.0f, 1.0f}};
    const Transform zero_scale_y{{}, identity, Vec3{1.0f, 0.0f, 1.0f}};
    const Transform zero_scale_z{{}, identity, Vec3{1.0f, 1.0f, -0.0f}};

    const epidemic::runtime::Aabb valid_bounds{Vec3{-1.0f, -2.0f, -3.0f}, Vec3{1.0f, 2.0f, 3.0f}};
    const epidemic::runtime::Aabb inverted_x{Vec3{2.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    const epidemic::runtime::Aabb inverted_y{Vec3{0.0f, 2.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    const epidemic::runtime::Aabb inverted_z{Vec3{0.0f, 0.0f, 2.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    const epidemic::runtime::Aabb non_finite{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, nan, 1.0f}};

    return epidemic::runtime::IsValidTransform(valid) && !epidemic::runtime::IsValidTransform(bad_position) &&
           !epidemic::runtime::IsValidTransform(bad_rotation) && !epidemic::runtime::IsValidTransform(bad_scale) &&
           !epidemic::runtime::IsValidTransform(zero_scale_x) && !epidemic::runtime::IsValidTransform(zero_scale_y) &&
           !epidemic::runtime::IsValidTransform(zero_scale_z) && epidemic::runtime::IsValidAabb(valid_bounds) &&
           !epidemic::runtime::IsValidAabb(inverted_x) && !epidemic::runtime::IsValidAabb(inverted_y) &&
           !epidemic::runtime::IsValidAabb(inverted_z) && !epidemic::runtime::IsValidAabb(non_finite);
}

bool TestQuaternionNormalizationAndFallbackContract()
{
    const float maximum = std::numeric_limits<float>::max();
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float near_overflow = static_cast<float>(std::sqrt(static_cast<double>(maximum)) * 0.5);
    const Quat identity = epidemic::runtime::Normalize(Quat{});
    const Quat normalized = epidemic::runtime::Normalize(Quat{0.0f, 0.0f, 2.0f, 2.0f});
    const Quat near_overflow_normalized = epidemic::runtime::Normalize(Quat{near_overflow, 0.0f, 0.0f, 0.0f});
    const Quat zero = epidemic::runtime::Normalize(Quat{0.0f, 0.0f, 0.0f, 0.0f});
    const Quat non_finite_inputs[] = {
        Quat{infinity, 0.0f, 0.0f, 1.0f}, Quat{0.0f, infinity, 0.0f, 1.0f},
        Quat{0.0f, 0.0f, infinity, 1.0f}, Quat{0.0f, 0.0f, 0.0f, infinity},
        Quat{nan, 0.0f, 0.0f, 1.0f}, Quat{0.0f, nan, 0.0f, 1.0f},
        Quat{0.0f, 0.0f, nan, 1.0f}, Quat{0.0f, 0.0f, 0.0f, nan},
    };
    const Quat overflow_x = epidemic::runtime::Normalize(Quat{maximum, 0.0f, 0.0f, 0.0f});
    const Quat overflow_y = epidemic::runtime::Normalize(Quat{0.0f, maximum, 0.0f, 0.0f});
    const Quat overflow_z = epidemic::runtime::Normalize(Quat{0.0f, 0.0f, maximum, 0.0f});
    const Quat overflow_w = epidemic::runtime::Normalize(Quat{0.0f, 0.0f, 0.0f, maximum});

    if (identity != Quat{} || !epidemic::runtime::IsFinite(normalized) || !epidemic::runtime::IsNormalized(normalized) ||
        !epidemic::runtime::IsFinite(near_overflow_normalized) || !epidemic::runtime::IsNormalized(near_overflow_normalized) ||
        zero != Quat{} || overflow_x != Quat{} || overflow_y != Quat{} || overflow_z != Quat{} || overflow_w != Quat{} ||
        epidemic::runtime::IsNormalized(Quat{maximum, 0.0f, 0.0f, 0.0f}) ||
        epidemic::runtime::IsNormalized(Quat{0.0f, 0.0f, 0.0f, 0.0f}))
    {
        return false;
    }

    for (const Quat& value : non_finite_inputs)
    {
        if (epidemic::runtime::Normalize(value) != Quat{} || epidemic::runtime::IsNormalized(value))
        {
            return false;
        }
    }
    return true;
}

bool TestApproximateTrsCompositionContract()
{
    const float half_turn = 0.70710677f;
    const Transform parent{
        Vec3{1.0f, 2.0f, 3.0f},
        Quat{0.0f, 0.0f, half_turn, half_turn},
        Vec3{2.0f, 3.0f, 4.0f}};
    const Transform local{
        Vec3{1.0f, 2.0f, 3.0f},
        Quat{half_turn, 0.0f, 0.0f, half_turn},
        Vec3{-5.0f, 6.0f, 0.5f}};

    const Transform first = epidemic::runtime::ComposeTransform(parent, local);
    const Transform second = epidemic::runtime::ComposeTransform(parent, local);

    return first == second && Near(first.position, Vec3{-5.0f, 4.0f, 15.0f}) &&
           Near(first.scale, Vec3{-10.0f, 18.0f, 2.0f}) && epidemic::runtime::IsNormalized(first.rotation) &&
           epidemic::runtime::IsValidTransform(first);
}

bool TestTransformAabbNegativeScaleAndDegenerateBounds()
{
    const Transform transform{Vec3{1.0f, 2.0f, 3.0f}, Quat{}, Vec3{-2.0f, 3.0f, -4.0f}};
    const epidemic::runtime::Aabb bounds{Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 2.0f, 3.0f}};
    const auto transformed = epidemic::runtime::TransformAabb(transform, bounds);
    if (!Near(transformed.min, Vec3{-1.0f, 2.0f, -9.0f}) || !Near(transformed.max, Vec3{1.0f, 8.0f, 3.0f}))
    {
        return false;
    }

    const epidemic::runtime::Aabb point_bounds{Vec3{2.0f, -1.0f, 0.5f}, Vec3{2.0f, -1.0f, 0.5f}};
    const auto point_transformed = epidemic::runtime::TransformAabb(transform, point_bounds);
    return Near(point_transformed.min, Vec3{-3.0f, -1.0f, 1.0f}) && point_transformed.min == point_transformed.max;
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
           epidemic::runtime::PersistenceTierRank(PersistenceTier::Disposable) < epidemic::runtime::PersistenceTierRank(PersistenceTier::PlayerTouched) &&
           epidemic::runtime::CanPromotePersistenceTier(PersistenceTier::Disposable, PersistenceTier::PlayerTouched) &&
           !epidemic::runtime::CanPromotePersistenceTier(PersistenceTier::QuestCritical, PersistenceTier::Disposable);
}
// Verifies object reality helpers are explicit.
bool TestObjectRealityHelpersAreExplicit()
{
    return epidemic::runtime::RealityRank(ObjectRealityLevel::AbstractFact) <
               epidemic::runtime::RealityRank(ObjectRealityLevel::Logical) &&
           epidemic::runtime::RealityRank(ObjectRealityLevel::Logical) <
               epidemic::runtime::RealityRank(ObjectRealityLevel::Physical) &&
           epidemic::runtime::IsMoreConcreteRealityLevel(ObjectRealityLevel::Physical, ObjectRealityLevel::Logical) &&
           epidemic::runtime::IsLessConcreteRealityLevel(ObjectRealityLevel::AbstractFact, ObjectRealityLevel::Physical) &&
           epidemic::runtime::CanPromoteReality(ObjectRealityLevel::Logical, ObjectRealityLevel::Physical) &&
           !epidemic::runtime::CanPromoteReality(ObjectRealityLevel::Physical, ObjectRealityLevel::Logical) &&
           epidemic::runtime::CanDemoteReality(ObjectRealityLevel::Physical, ObjectRealityLevel::Logical) &&
           !epidemic::runtime::CanDemoteReality(ObjectRealityLevel::AbstractFact, ObjectRealityLevel::Physical);
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

    using PersistentObjectId = epidemic::runtime::PersistentObjectId;
    using RegionId = epidemic::runtime::RegionId;
    using SurfaceId = epidemic::runtime::SurfaceId;
    using SimulationZoneId = epidemic::runtime::SimulationZoneId;
    using LazyRuleId = epidemic::runtime::LazyRuleId;
    static_assert(!std::is_constructible_v<AssetId, ResourceId> && !std::is_constructible_v<ResourceId, AssetId>);
    static_assert(!std::is_constructible_v<RuntimeObjectId, PersistentObjectId> && !std::is_constructible_v<RuntimeObjectId, RegionId> && !std::is_constructible_v<RuntimeObjectId, ChunkId> && !std::is_constructible_v<RuntimeObjectId, SurfaceId> && !std::is_constructible_v<RuntimeObjectId, SimulationZoneId> && !std::is_constructible_v<RuntimeObjectId, LazyRuleId>);
    static_assert(!std::is_constructible_v<PersistentObjectId, RuntimeObjectId> && !std::is_constructible_v<PersistentObjectId, RegionId> && !std::is_constructible_v<PersistentObjectId, ChunkId> && !std::is_constructible_v<PersistentObjectId, SurfaceId> && !std::is_constructible_v<PersistentObjectId, SimulationZoneId> && !std::is_constructible_v<PersistentObjectId, LazyRuleId>);
    static_assert(!std::is_constructible_v<RegionId, RuntimeObjectId> && !std::is_constructible_v<RegionId, PersistentObjectId> && !std::is_constructible_v<RegionId, ChunkId> && !std::is_constructible_v<RegionId, SurfaceId> && !std::is_constructible_v<RegionId, SimulationZoneId> && !std::is_constructible_v<RegionId, LazyRuleId>);
    static_assert(!std::is_constructible_v<ChunkId, RuntimeObjectId> && !std::is_constructible_v<ChunkId, PersistentObjectId> && !std::is_constructible_v<ChunkId, RegionId> && !std::is_constructible_v<ChunkId, SurfaceId> && !std::is_constructible_v<ChunkId, SimulationZoneId> && !std::is_constructible_v<ChunkId, LazyRuleId>);
    static_assert(!std::is_constructible_v<SurfaceId, RuntimeObjectId> && !std::is_constructible_v<SurfaceId, PersistentObjectId> && !std::is_constructible_v<SurfaceId, RegionId> && !std::is_constructible_v<SurfaceId, ChunkId> && !std::is_constructible_v<SurfaceId, SimulationZoneId> && !std::is_constructible_v<SurfaceId, LazyRuleId>);
    static_assert(!std::is_constructible_v<SimulationZoneId, RuntimeObjectId> && !std::is_constructible_v<SimulationZoneId, PersistentObjectId> && !std::is_constructible_v<SimulationZoneId, RegionId> && !std::is_constructible_v<SimulationZoneId, ChunkId> && !std::is_constructible_v<SimulationZoneId, SurfaceId> && !std::is_constructible_v<SimulationZoneId, LazyRuleId>);
    static_assert(!std::is_constructible_v<LazyRuleId, RuntimeObjectId> && !std::is_constructible_v<LazyRuleId, PersistentObjectId> && !std::is_constructible_v<LazyRuleId, RegionId> && !std::is_constructible_v<LazyRuleId, ChunkId> && !std::is_constructible_v<LazyRuleId, SurfaceId> && !std::is_constructible_v<LazyRuleId, SimulationZoneId>);
    struct HandleATag {};
    struct HandleBTag {};
    static_assert(!std::is_same_v<epidemic::runtime::RuntimeHandle<HandleATag>, epidemic::runtime::RuntimeHandle<HandleBTag>>);

    if (!TestDefaultInvalidIds())
    {
        return 1;
    }

    if (!TestEquality())
    {
        return 2;
    }

    if (!TestCheckedMonotonicIdAllocatorExhaustion())
    {
        return 5;
    }

    if (!TestHashWorksInUnorderedMap())
    {
        return 3;
    }

    if (!TestAssetAndResourceIdsUseStringBackedRuntimeIds())
    {
        return 4;
    }

    if (!TestRuntimeBudgetDefaultsToUnlimited())
    {
        return 6;
    }

    if (!TestRuntimeBudgetTracksLimits())
    {
        return 7;
    }
    if (!TestRuntimeBudgetRejectsNegativeTime())
    {
        return 19;
    }
    if (!TestTransactionalMonotonicReservation())
    {
        return 20;
    }
    if (!TestCheckedArithmeticBoundaries())
    {
        return 21;
    }

    if (!TestRuntimeFrameDurationUsesRealMicroseconds())
    {
        return 18;
    }

    if (!TestGameTimeOperations())
    {
        return 8;
    }

    if (!TestGameTimeOverflowHelpers())
    {
        return 9;
    }

    if (!TestGameTimeOperatorsSaturateAtInt64Edges())
    {
        return 10;
    }

    if (!TestQuaternionAndTransformMath())
    {
        return 11;
    }

    if (!TestNestedRotatedTransformCompositionAndNegativeScale())
    {
        return 12;
    }

    if (!TestInvalidAndZeroQuaternionValidation())
    {
        return 13;
    }

    if (!TestTransformAabb())
    {
        return 14;
    }


    if (!TestAllRuntimeIdFamiliesAndHashConsistency())
    {
        return 22;
    }

    if (!TestMonotonicIdHelperBoundariesAndNoOpCommit())
    {
        return 23;
    }

    if (!TestNumericValidationContracts())
    {
        return 24;
    }

    if (!TestRuntimeBudgetMaximumUnsignedLimits())
    {
        return 25;
    }

    if (!TestCheckedSecondsToMicrosecondsBoundaries())
    {
        return 26;
    }

    if (!TestCheckedScaleDurationBoundaries())
    {
        return 27;
    }

    if (!TestGameTimeInt64MinimumMaximumBoundaries())
    {
        return 28;
    }

    if (!TestSpatialFiniteValidationFamilies())
    {
        return 29;
    }

    if (!TestQuaternionNormalizationAndFallbackContract())
    {
        return 30;
    }

    if (!TestApproximateTrsCompositionContract())
    {
        return 31;
    }

    if (!TestTransformAabbNegativeScaleAndDegenerateBounds())
    {
        return 32;
    }

    if (!TestOperationHelpers())
    {
        return 15;
    }

    if (!TestExplicitStateHelpers())
    {
        return 16;
    }

    if (!TestObjectRealityHelpersAreExplicit())
    {
        return 17;
    }

    return 0;
}


