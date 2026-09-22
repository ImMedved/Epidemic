#include "animation_runtime_impl.h"

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Runtime/Foundation/checked_id_allocator.h"
#include "Epidemic/Runtime/Foundation/numeric_validation.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <limits>
#include <exception>
#include <new>
#include <string_view>
#include <utility>
#include <type_traits>


namespace
{
static_assert(std::numeric_limits<double>::is_iec559, "playback scaling requires IEEE-754 binary64 semantics");
static_assert(std::numeric_limits<double>::radix == 2 && std::numeric_limits<double>::digits == 53,
              "playback scaling requires IEEE-754 binary64 precision");
static_assert(std::numeric_limits<double>::min_exponent == -1021 && std::numeric_limits<double>::max_exponent == 1024,
              "playback scaling requires IEEE-754 binary64 exponent range");

struct WideUInt
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

struct BinaryFraction
{
    WideUInt significand{};
    int exponent = 0;
};

[[nodiscard]] constexpr bool IsZero(WideUInt value) noexcept
{
    return value.high == 0 && value.low == 0;
}

[[nodiscard]] int BitWidth(WideUInt value) noexcept
{
    if (value.high != 0) return 64 + static_cast<int>(std::bit_width(value.high));
    return static_cast<int>(std::bit_width(value.low));
}

[[nodiscard]] WideUInt MultiplyWide(std::uint64_t lhs, std::uint64_t rhs) noexcept
{
    constexpr std::uint64_t limb_mask = 0xffff'ffffULL;
    const std::uint64_t lhs_low = lhs & limb_mask;
    const std::uint64_t lhs_high = lhs >> 32;
    const std::uint64_t rhs_low = rhs & limb_mask;
    const std::uint64_t rhs_high = rhs >> 32;

    const std::uint64_t product_low = lhs_low * rhs_low;
    const std::uint64_t product_mid_a = lhs_high * rhs_low;
    const std::uint64_t product_mid_b = lhs_low * rhs_high;
    const std::uint64_t product_high = lhs_high * rhs_high;

    const std::uint64_t carry = (product_low >> 32) + (product_mid_a & limb_mask) + (product_mid_b & limb_mask);
    return WideUInt{
        product_high + (product_mid_a >> 32) + (product_mid_b >> 32) + (carry >> 32),
        (product_low & limb_mask) | (carry << 32)};
}

[[nodiscard]] WideUInt ShiftRight(WideUInt value, unsigned shift) noexcept
{
    if (shift == 0) return value;
    if (shift >= 128) return {};
    if (shift >= 64) return WideUInt{0, value.high >> (shift - 64)};
    return WideUInt{value.high >> shift, (value.low >> shift) | (value.high << (64 - shift))};
}

[[nodiscard]] WideUInt LowBits(WideUInt value, unsigned bit_count) noexcept
{
    if (bit_count == 0) return {};
    if (bit_count >= 128) return value;
    if (bit_count == 64) return WideUInt{0, value.low};
    if (bit_count > 64)
    {
        const unsigned high_bits = bit_count - 64;
        const std::uint64_t mask = (std::uint64_t{1} << high_bits) - 1;
        return WideUInt{value.high & mask, value.low};
    }
    const std::uint64_t mask = (std::uint64_t{1} << bit_count) - 1;
    return WideUInt{0, value.low & mask};
}

[[nodiscard]] unsigned CountTrailingZeros(WideUInt value) noexcept
{
    if (value.low != 0) return static_cast<unsigned>(std::countr_zero(value.low));
    if (value.high != 0) return 64u + static_cast<unsigned>(std::countr_zero(value.high));
    return 0;
}

[[nodiscard]] BinaryFraction NormalizeFraction(BinaryFraction value) noexcept
{
    if (IsZero(value.significand)) return BinaryFraction{};
    const unsigned trailing = CountTrailingZeros(value.significand);
    value.significand = ShiftRight(value.significand, trailing);
    value.exponent += static_cast<int>(trailing);
    return value;
}

[[nodiscard]] BinaryFraction DecomposeFraction(double value) noexcept
{
    if (value == 0.0) return {};
    int exponent = 0;
    const double fraction = std::frexp(value, &exponent);
    const auto significand = static_cast<std::uint64_t>(std::ldexp(fraction, std::numeric_limits<double>::digits));
    return NormalizeFraction(BinaryFraction{WideUInt{0, significand}, exponent - std::numeric_limits<double>::digits});
}

// Every positive binary64 value below 1.0 is an integer multiple of 2^-1074.
// Keeping that fixed-point grid lets us combine the product fraction and the
// carried remainder exactly before rounding back to binary64.
constexpr int kBinary64FractionGridExponent = -1074;
constexpr unsigned kBinary64FractionCarryBit = 1074;
constexpr std::size_t kBinary64FractionGridLimbs = 17;
using FractionGrid = std::array<std::uint64_t, kBinary64FractionGridLimbs>;

void AddGridLimb(FractionGrid& grid, std::size_t index, std::uint64_t value) noexcept
{
    while (value != 0 && index < grid.size())
    {
        const std::uint64_t previous = grid[index];
        grid[index] += value;
        value = grid[index] < previous ? 1 : 0;
        ++index;
    }
}

void AddShiftedWord(FractionGrid& grid, std::uint64_t value, unsigned bit_offset) noexcept
{
    if (value == 0) return;
    const std::size_t limb = bit_offset / 64;
    const unsigned shift = bit_offset % 64;
    AddGridLimb(grid, limb, value << shift);
    if (shift != 0) AddGridLimb(grid, limb + 1, value >> (64 - shift));
}

void AddFractionToGrid(FractionGrid& grid, BinaryFraction value) noexcept
{
    value = NormalizeFraction(value);
    if (IsZero(value.significand)) return;

    const int offset = value.exponent - kBinary64FractionGridExponent;
    if (offset < 0) return;
    AddShiftedWord(grid, value.significand.low, static_cast<unsigned>(offset));
    AddShiftedWord(grid, value.significand.high, static_cast<unsigned>(offset) + 64u);
}

[[nodiscard]] bool TestGridBit(const FractionGrid& grid, unsigned bit) noexcept
{
    return (grid[bit / 64] & (std::uint64_t{1} << (bit % 64))) != 0;
}

void ClearGridBit(FractionGrid& grid, unsigned bit) noexcept
{
    grid[bit / 64] &= ~(std::uint64_t{1} << (bit % 64));
}

[[nodiscard]] int HighestGridBit(const FractionGrid& grid) noexcept
{
    for (std::size_t index = grid.size(); index-- > 0;)
    {
        if (grid[index] != 0)
        {
            return static_cast<int>(index * 64 + std::bit_width(grid[index]) - 1);
        }
    }
    return -1;
}

[[nodiscard]] std::uint64_t ExtractGridBits(const FractionGrid& grid, unsigned first_bit, unsigned bit_count) noexcept
{
    if (bit_count == 0) return 0;
    const std::size_t limb = first_bit / 64;
    const unsigned shift = first_bit % 64;
    std::uint64_t value = grid[limb] >> shift;
    if (shift != 0 && limb + 1 < grid.size()) value |= grid[limb + 1] << (64 - shift);
    if (bit_count < 64) value &= (std::uint64_t{1} << bit_count) - 1;
    return value;
}

[[nodiscard]] bool AnyGridBitsBelow(const FractionGrid& grid, unsigned bit_exclusive) noexcept
{
    if (bit_exclusive == 0) return false;
    const std::size_t full_limbs = bit_exclusive / 64;
    for (std::size_t i = 0; i < full_limbs; ++i)
    {
        if (grid[i] != 0) return true;
    }
    const unsigned remainder = bit_exclusive % 64;
    if (remainder == 0) return false;
    const std::uint64_t mask = (std::uint64_t{1} << remainder) - 1;
    return (grid[full_limbs] & mask) != 0;
}

[[nodiscard]] double GridFractionToDouble(const FractionGrid& grid) noexcept
{
    const int highest = HighestGridBit(grid);
    if (highest < 0) return 0.0;

    if (highest < 52)
    {
        const std::uint64_t significand = ExtractGridBits(grid, 0, 52);
        return std::ldexp(static_cast<double>(significand), kBinary64FractionGridExponent);
    }

    unsigned discarded_bits = static_cast<unsigned>(highest - 52);
    std::uint64_t significand = ExtractGridBits(grid, discarded_bits, 53);
    if (discarded_bits != 0)
    {
        const bool guard = TestGridBit(grid, discarded_bits - 1);
        const bool sticky = AnyGridBitsBelow(grid, discarded_bits - 1);
        if (guard && (sticky || (significand & 1u) != 0))
        {
            ++significand;
            if (significand == (std::uint64_t{1} << 53))
            {
                significand >>= 1;
                ++discarded_bits;
            }
        }
    }

    double result = std::ldexp(
        static_cast<double>(significand),
        kBinary64FractionGridExponent + static_cast<int>(discarded_bits));
    if (result >= 1.0) result = std::nextafter(1.0, 0.0);
    return result;
}

struct FractionSum
{
    bool carry = false;
    FractionGrid exact_remainder{};
    double rounded_remainder = 0.0;
};

[[nodiscard]] bool GridIsZero(const FractionGrid& grid) noexcept
{
    return HighestGridBit(grid) < 0;
}

[[nodiscard]] bool GridIsValidFraction(const FractionGrid& grid) noexcept
{
    return HighestGridBit(grid) < static_cast<int>(kBinary64FractionCarryBit);
}

[[nodiscard]] FractionSum AddFractions(BinaryFraction product_fraction, const FractionGrid& stored_remainder) noexcept
{
    FractionGrid grid = stored_remainder;
    AddFractionToGrid(grid, product_fraction);

    const bool carry = TestGridBit(grid, kBinary64FractionCarryBit);
    if (carry) ClearGridBit(grid, kBinary64FractionCarryBit);
    return FractionSum{carry, grid, GridFractionToDouble(grid)};
}

struct ScaledPlaybackDelta
{
    std::int64_t whole_microseconds = 0;
    FractionGrid exact_fractional_microseconds{};
    double fractional_microseconds = 0.0;
};

[[nodiscard]] std::optional<ScaledPlaybackDelta> ScalePlaybackDelta(
    std::int64_t delta_microseconds,
    double rate,
    const FractionGrid& stored_remainder,
    double stored_remainder_mirror) noexcept
{
    if (delta_microseconds < 0 || !std::isfinite(rate) || rate < 0.0 ||
        !std::isfinite(stored_remainder_mirror) || stored_remainder_mirror < 0.0 || stored_remainder_mirror >= 1.0 ||
        !GridIsValidFraction(stored_remainder))
    {
        return std::nullopt;
    }

    if (delta_microseconds == 0 || rate == 0.0)
    {
        return ScaledPlaybackDelta{0, stored_remainder, GridFractionToDouble(stored_remainder)};
    }
    if (rate == 1.0)
    {
        if (delta_microseconds == std::numeric_limits<std::int64_t>::max() && !GridIsZero(stored_remainder)) return std::nullopt;
        return ScaledPlaybackDelta{delta_microseconds, stored_remainder, GridFractionToDouble(stored_remainder)};
    }

    int rate_exponent = 0;
    const double rate_fraction = std::frexp(rate, &rate_exponent);
    const auto rate_significand = static_cast<std::uint64_t>(std::ldexp(rate_fraction, std::numeric_limits<double>::digits));
    BinaryFraction rate_parts{WideUInt{0, rate_significand}, rate_exponent - std::numeric_limits<double>::digits};
    rate_parts = NormalizeFraction(rate_parts);

    const WideUInt product = MultiplyWide(static_cast<std::uint64_t>(delta_microseconds), rate_parts.significand.low);
    const int product_exponent = rate_parts.exponent;
    std::uint64_t whole = 0;
    BinaryFraction product_fraction{};

    if (product_exponent >= 0)
    {
        const int width = BitWidth(product);
        if (width != 0 && width + product_exponent > 63) return std::nullopt;
        whole = product.low << static_cast<unsigned>(product_exponent);
    }
    else
    {
        const unsigned fractional_bits = static_cast<unsigned>(-product_exponent);
        const WideUInt quotient = ShiftRight(product, fractional_bits);
        if (quotient.high != 0 || quotient.low > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
        whole = quotient.low;
        product_fraction = NormalizeFraction(BinaryFraction{LowBits(product, fractional_bits), product_exponent});
    }

    const bool has_product_fraction = !IsZero(product_fraction.significand);
    if (whole == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
        (has_product_fraction || !GridIsZero(stored_remainder)))
    {
        return std::nullopt;
    }

    const FractionSum fraction_sum = AddFractions(product_fraction, stored_remainder);
    if (fraction_sum.carry)
    {
        if (whole == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return std::nullopt;
        ++whole;
    }
    if (whole == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) && !GridIsZero(fraction_sum.exact_remainder))
    {
        return std::nullopt;
    }

    return ScaledPlaybackDelta{static_cast<std::int64_t>(whole), fraction_sum.exact_remainder, fraction_sum.rounded_remainder};
}
}

namespace epidemic::runtime::animation
{
AnimationRuntime::AnimationRuntime(AnimationOptions options, AnimationDependencies dependencies)
    : options_(options), dependencies_(std::move(dependencies))
{
    const std::size_t capacity = options_.event_capacity == 0 ? AnimationOptions{}.event_capacity : options_.event_capacity;
    try
    {
        events_.resize(capacity);
        for (auto& event : events_) event.name.reserve(kEventNameCapacity);
        event_storage_ready_ = true;
    }
    catch (...)
    {
        events_.clear();
        event_storage_ready_ = false;
    }
}

foundation::Result<void> AnimationRuntime::RegisterSkeleton(SkeletonDesc desc)
{
    if (registries_frozen_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.registry_frozen", "animation registries are frozen"));
    const auto validation = ValidateSkeleton(desc);
    if (!validation) return validation;
    if (skeletons_.contains(desc.id)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.duplicate_skeleton", "skeleton is already registered"));
    try { skeletons_.emplace(desc.id, desc); }
    catch (...) { return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to register skeleton")); }
    return foundation::Result<void>::Success();
}


bool AnimationRuntime::HasSkeleton(SkeletonId id) const
{
    if (skeletons_.contains(id)) return true;
    if (dependencies_.resources == nullptr) return false;
    try
    {
        const auto loaded = dependencies_.resources->LoadSkeleton(id);
        return loaded && loaded.Value().id == id && loaded.Value().joint_count != 0 &&
               (options_.max_skeleton_joints == 0 || loaded.Value().joint_count <= options_.max_skeleton_joints);
    }
    catch (...) { return false; }
}


foundation::Result<void> AnimationRuntime::RegisterClip(AnimationClipDesc desc)
{
    if (registries_frozen_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.registry_frozen", "animation registries are frozen"));
    const auto validation = ValidateClip(desc);
    if (!validation) return validation;
    if (!HasSkeleton(desc.skeleton)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_not_found", "clip must reference a registered skeleton"));
    if (clips_.contains(desc.id)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.duplicate_clip", "animation clip is already registered"));
    try { clips_.emplace(desc.id, desc); }
    catch (...) { return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to register clip")); }
    return foundation::Result<void>::Success();
}


bool AnimationRuntime::HasClip(AnimationClipId id) const
{
    if (clips_.contains(id)) return true;
    if (dependencies_.resources == nullptr) return false;
    try
    {
        const auto loaded = dependencies_.resources->LoadClip(id);
        return loaded && loaded.Value().id == id && loaded.Value().skeleton.IsValid() && IsFinitePositive(loaded.Value().duration_seconds) &&
               CheckedSecondsToMicroseconds(static_cast<double>(loaded.Value().duration_seconds)).has_value();
    }
    catch (...) { return false; }
}

foundation::Result<void> AnimationRuntime::Freeze()
{
    registries_frozen_ = true;
    return foundation::Result<void>::Success();
}

bool AnimationRuntime::IsFrozen() const noexcept
{
    return registries_frozen_;
}


foundation::Result<AnimatorHandle> AnimationRuntime::CreateAnimatorHandle(const AnimatorDesc& desc)
{
    if (!desc.owner.IsValid()) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.invalid_owner", "animator owner must be valid before creation"));
    switch (desc.lod) { case AnimationLodLevel::Full: case AnimationLodLevel::Reduced: case AnimationLodLevel::Frozen: break; default: return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.invalid_lod", "animator LOD value is invalid")); }
    if (options_.max_animators != 0 && animators_.size() >= options_.max_animators) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.animator_capacity", "animator capacity is exhausted"));
    const auto skeleton = ResolveSkeleton(desc.skeleton);
    if (!skeleton) return foundation::Result<AnimatorHandle>::Failure(skeleton.GetError());
    const auto id_value = PeekMonotonicId(next_animator_value_, "animation.animator_id_exhausted", "animator id allocator is exhausted");
    const auto generation = PeekMonotonicId(next_generation_, "animation.animator_generation_exhausted", "animator generation allocator is exhausted");
    if (!id_value) return foundation::Result<AnimatorHandle>::Failure(id_value.GetError());
    if (!generation) return foundation::Result<AnimatorHandle>::Failure(generation.GetError());
    const AnimatorInstanceId id{id_value.Value()};
    const AnimatorHandle handle{id, generation.Value()};
    if (animators_.contains(id)) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.duplicate_animator_id", "allocated animator id already exists"));
    try
    {
        AnimatorRecord record{}; record.desc=desc; record.handle=handle; record.lifecycle=AnimatorLifecycle::Ready; record.readiness=AnimatorReadiness::Ready;
        record.playback_state=AnimatorPlaybackState::Stopped; record.pose_state=PoseState::Clean; record.revision=1;
        record.cached_pose=PoseBuffer{handle, desc.owner, std::vector<Transform>(skeleton.Value().joint_count), record.revision};
        const auto [_, inserted]=animators_.emplace(id, std::move(record));
        if (!inserted) return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.duplicate_animator_id", "allocated animator id already exists"));
    }
    catch (...) { return foundation::Result<AnimatorHandle>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to allocate animator pose storage")); }
    CommitMonotonicId(next_animator_value_, id_value.Value()); CommitMonotonicId(next_generation_, generation.Value());
    return foundation::Result<AnimatorHandle>::Success(handle);
}


foundation::Result<void> AnimationRuntime::DestroyAnimator(AnimatorHandle handle)
{
    AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for destruction"));
    }

    animators_.erase(handle.id);
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::Play(const AnimationPlaybackCommand& command)
{
    AnimatorRecord* animator=FindAnimator(command.animator);
    if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for playback"));
    if (!IsFiniteNonNegative(command.playback_rate) || !CheckedScaleDuration(FrameDuration{std::chrono::microseconds{1}}, command.playback_rate)) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_playback", "animation playback rate exceeds supported range"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    const auto clip_result=ResolveClip(command.clip); if (!clip_result) return foundation::Result<void>::Failure(clip_result.GetError());
    if (clip_result.Value().skeleton != animator->desc.skeleton) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    if (!event_storage_ready_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "animation event storage is unavailable"));
    animator->readiness=AnimatorReadiness::Ready; animator->playback=AnimatorPlayback{command.clip,command.loop,command.playback_rate,FrameDuration{},0.0}; animator->exact_fractional_microseconds.fill(0); animator->crossfade.reset();
    animator->playback_state=AnimatorPlaybackState::Playing; animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value();
    QueueEvent(command.animator.id,"animation.started",0.0f); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Pause(AnimatorHandle handle)
{
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pause"));
    if (animator->playback_state!=AnimatorPlaybackState::Playing && animator->playback_state!=AnimatorPlaybackState::Blending) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_playback_transition", "only playing or blending animators can be paused"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    animator->playback_state=AnimatorPlaybackState::Paused; animator->revision=revision.Value(); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Stop(AnimatorHandle handle)
{
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for stop"));
    if (animator->playback_state==AnimatorPlaybackState::Stopped) return foundation::Result<void>::Success();
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    if (!event_storage_ready_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "animation event storage is unavailable"));
    animator->playback_state=AnimatorPlaybackState::Stopped; animator->playback.local_time=FrameDuration{}; animator->playback.fractional_microseconds=0.0; animator->exact_fractional_microseconds.fill(0); animator->crossfade.reset(); animator->pose_state=PoseState::Clean; animator->revision=revision.Value();
    QueueEvent(handle.id,"animation.stopped",0.0f); return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::Crossfade(AnimatorHandle handle, AnimationClipId clip, FrameDuration duration)
{
    if (duration.IsNegative()) return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_fade", "crossfade duration must not be negative"));
    AnimatorRecord* animator=FindAnimator(handle); if (!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found", "animator handle was not found for crossfade"));
    const auto revision=NextRevision(*animator); if (!revision) return foundation::Result<void>::Failure(revision.GetError());
    const auto clip_result=ResolveClip(clip); if (!clip_result) return foundation::Result<void>::Failure(clip_result.GetError());
    if (clip_result.Value().skeleton!=animator->desc.skeleton) return foundation::Result<void>::Failure(foundation::Error::Create("animation.skeleton_mismatch", "clip skeleton does not match animator skeleton"));
    if (!animator->playback.clip.IsValid()) return Play(AnimationPlaybackCommand{handle,clip,false,1.0});
    if (!duration.IsZero() && !event_storage_ready_) return foundation::Result<void>::Failure(foundation::Error::Create("animation.allocation_failed", "animation event storage is unavailable"));
    if (duration.IsZero()) { animator->playback=AnimatorPlayback{clip,false,1.0,FrameDuration{},0.0}; animator->exact_fractional_microseconds.fill(0); animator->playback_state=AnimatorPlaybackState::Playing; animator->crossfade.reset(); animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value(); return foundation::Result<void>::Success(); }
    animator->crossfade=CrossfadeState{animator->playback.clip,clip,animator->playback.local_time,FrameDuration{},FrameDuration{},duration,1.0f,0.0f}; animator->playback_state=AnimatorPlaybackState::Blending; animator->pose_state=dependencies_.evaluator?PoseState::Evaluating:PoseState::Dirty; animator->revision=revision.Value();
    QueueEvent(handle.id,"animation.crossfade",static_cast<float>(duration.value.count())/1000000.0f); return foundation::Result<void>::Success();
}


foundation::Result<std::size_t> AnimationRuntime::Tick(FrameDuration delta, std::size_t max_animators)
{
    if (delta.IsNegative()) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.invalid_delta", "animation frame duration must not be negative"));
    std::vector<AnimatorInstanceId> work_list; try { work_list=BuildAnimatorWorkList(); } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to build animator tick work list")); }
    const std::size_t limit=max_animators==0?animators_.size():max_animators; std::size_t transitioned=0;
    static_assert(std::is_nothrow_move_assignable_v<AnimatorRecord>, "animator commit must not throw after external pose publication");
    for (auto id:work_list)
    {
        if (transitioned >= limit) break;
        AnimatorRecord& live = animators_.at(id);
        AnimatorRecord staged; try { staged=live; } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to stage animator state")); }
        const auto revision=NextRevision(staged); if (!revision) return foundation::Result<std::size_t>::Failure(revision.GetError());
        const auto a=AdvancePlayback(staged,delta); if (!a) return foundation::Result<std::size_t>::Failure(a.GetError());
        if (staged.playback_state==AnimatorPlaybackState::Blending) { const auto b=AdvanceCrossfade(staged,delta); if (!b) return foundation::Result<std::size_t>::Failure(b.GetError()); }
        foundation::Result<PoseBuffer> evaluated=foundation::Result<PoseBuffer>::Failure(foundation::Error::Create("animation.evaluation_failed","evaluation unavailable"));
        try { evaluated=EvaluatePose(staged); } catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.evaluator_exception", "animation evaluator callback threw")); }
        if (!evaluated) return foundation::Result<std::size_t>::Failure(evaluated.GetError());
        staged.cached_pose=std::move(evaluated.Value()); staged.pose_state=staged.desc.lod==AnimationLodLevel::Frozen?PoseState::Clean:PoseState::Ready;
        bool looped=false, finished=false; const auto clip=ResolveClip(staged.playback.clip); if (!clip) return foundation::Result<std::size_t>::Failure(clip.GetError());
        const auto duration=CheckedSecondsToMicroseconds(static_cast<double>(clip.Value().duration_seconds)); if (!duration) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.clip_duration_overflow", "animation clip duration exceeds runtime time range"));
        if (staged.playback_state!=AnimatorPlaybackState::Blending && !duration->IsZero() && staged.playback.local_time.value>=duration->value) { if (staged.playback.loop) { staged.playback.local_time.value%=duration->value; looped=true; } else { staged.playback.local_time=*duration; staged.playback_state=AnimatorPlaybackState::Finished; finished=true; } }
        if ((looped || finished) && !event_storage_ready_) return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "animation event storage is unavailable"));
        staged.revision=revision.Value(); staged.cached_pose.revision=staged.revision;

        std::shared_ptr<const PoseBuffer> candidate_pose;
        if (dependencies_.pose_sink && staged.desc.lod!=AnimationLodLevel::Frozen)
        {
            try { candidate_pose=std::make_shared<const PoseBuffer>(staged.cached_pose); }
            catch (const std::bad_alloc&) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to stage pose publication")); }
            catch (...) { return foundation::Result<std::size_t>::Failure(foundation::Error::Create("animation.allocation_failed", "failed to stage pose publication")); }
            const auto published=PublishPose(candidate_pose);
            if(!published) return foundation::Result<std::size_t>::Failure(published.GetError());
        }

        live=std::move(staged);
        if (looped) QueueEvent(id, "animation.looped", static_cast<float>(live.playback.local_time.value.count()) / 1000000.0f);
        if (finished) QueueEvent(id, "animation.finished", static_cast<float>(live.playback.local_time.value.count()) / 1000000.0f);
        ++transitioned;
    }
    return foundation::Result<std::size_t>::Success(transitioned);
}

foundation::Result<AnimatorSnapshot> AnimationRuntime::GetAnimatorSnapshot(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<AnimatorSnapshot>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for snapshot"));
    }

    return foundation::Result<AnimatorSnapshot>::Success(ToSnapshot(*animator));
}

foundation::Result<void> AnimationRuntime::SetLod(AnimatorHandle handle, AnimationLodLevel lod)
{
    switch(lod){case AnimationLodLevel::Full:case AnimationLodLevel::Reduced:case AnimationLodLevel::Frozen:break;default:return foundation::Result<void>::Failure(foundation::Error::Create("animation.invalid_lod","animation LOD value is invalid"));}
    AnimatorRecord* animator=FindAnimator(handle); if(!animator) return foundation::Result<void>::Failure(foundation::Error::Create("animation.animator_not_found","animator was not found for LOD update"));
    const auto revision=NextRevision(*animator); if(!revision) return foundation::Result<void>::Failure(revision.GetError()); animator->desc.lod=lod; animator->pose_state=lod==AnimationLodLevel::Frozen?PoseState::Clean:PoseState::Dirty; animator->revision=revision.Value(); return foundation::Result<void>::Success();
}


foundation::Result<PoseState> AnimationRuntime::GetPoseState(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseState>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose state"));
    }

    return foundation::Result<PoseState>::Success(animator->pose_state);
}

foundation::Result<PoseSnapshot> AnimationRuntime::GetPoseSnapshot(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseSnapshot>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose snapshot"));
    }

    return foundation::Result<PoseSnapshot>::Success(PoseSnapshot{handle.id, animator->handle, animator->pose_state, animator->desc.lod, animator->revision});
}

foundation::Result<PoseBuffer> AnimationRuntime::GetPoseBuffer(AnimatorHandle handle) const
{
    const AnimatorRecord* animator = FindAnimator(handle);
    if (animator == nullptr)
    {
        return foundation::Result<PoseBuffer>::Failure(
            foundation::Error::Create("animation.animator_not_found", "animator handle was not found for pose buffer"));
    }

    return foundation::Result<PoseBuffer>::Success(animator->cached_pose);
}

std::span<const AnimationEvent> AnimationRuntime::Events() const
{
    return std::span<const AnimationEvent>{events_.data(), event_count_};
}

void AnimationRuntime::Clear()
{
    event_count_ = 0;
}

foundation::Result<SkeletonDesc> AnimationRuntime::ResolveSkeleton(SkeletonId id)
{
    const auto it=skeletons_.find(id); if(it!=skeletons_.end()) return foundation::Result<SkeletonDesc>::Success(it->second);
    if(!dependencies_.resources) return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.skeleton_not_found","skeleton was not registered"));
    foundation::Result<SkeletonDesc> loaded=foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.resource_exception","skeleton callback did not run")); try{loaded=dependencies_.resources->LoadSkeleton(id);}catch(...){return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.resource_exception","skeleton resource callback threw"));}
    if (!loaded)
    {
        return loaded;
    }
    const auto v = ValidateSkeleton(loaded.Value());
    if (!v || loaded.Value().id != id)
    {
        return foundation::Result<SkeletonDesc>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "resource source returned invalid skeleton descriptor"));
    }
    if(!registries_frozen_) { try{skeletons_.emplace(id,loaded.Value());}catch(...){return foundation::Result<SkeletonDesc>::Failure(foundation::Error::Create("animation.allocation_failed","failed to cache skeleton"));} } return foundation::Result<SkeletonDesc>::Success(loaded.Value());
}


foundation::Result<AnimationClipDesc> AnimationRuntime::ResolveClip(AnimationClipId id)
{
    const auto it=clips_.find(id); if(it!=clips_.end()) return foundation::Result<AnimationClipDesc>::Success(it->second);
    if(!dependencies_.resources) return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.clip_not_found","animation clip was not registered"));
    foundation::Result<AnimationClipDesc> loaded=foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.resource_exception","clip callback did not run")); try{loaded=dependencies_.resources->LoadClip(id);}catch(...){return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.resource_exception","clip resource callback threw"));}
    if (!loaded)
    {
        return loaded;
    }
    const auto v = ValidateClip(loaded.Value());
    if (!v || loaded.Value().id != id)
    {
        return foundation::Result<AnimationClipDesc>::Failure(
            foundation::Error::Create("animation.invalid_clip", "resource source returned invalid clip descriptor"));
    }
    if(!registries_frozen_) { try{clips_.emplace(id,loaded.Value());}catch(...){return foundation::Result<AnimationClipDesc>::Failure(foundation::Error::Create("animation.allocation_failed","failed to cache clip"));} } return foundation::Result<AnimationClipDesc>::Success(loaded.Value());
}


AnimatorSnapshot AnimationRuntime::ToSnapshot(const AnimatorRecord& animator) noexcept
{
    return AnimatorSnapshot{
        animator.handle,
        animator.lifecycle,
        animator.readiness,
        animator.playback_state,
        animator.desc.lod,
        animator.playback.clip,
        animator.playback.local_time,
        animator.revision};
}

AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle)
{
    const auto iterator = animators_.find(handle.id);
    if (iterator == animators_.end() || iterator->second.handle.generation != handle.generation)
    {
        return nullptr;
    }

    return &iterator->second;
}

const AnimationRuntime::AnimatorRecord* AnimationRuntime::FindAnimator(AnimatorHandle handle) const
{
    const auto iterator = animators_.find(handle.id);
    if (iterator == animators_.end() || iterator->second.handle.generation != handle.generation)
    {
        return nullptr;
    }

    return &iterator->second;
}

std::vector<AnimatorInstanceId> AnimationRuntime::BuildAnimatorWorkList() const
{
    std::vector<AnimatorInstanceId> work_list;
    work_list.reserve(animators_.size());
    for (const auto& [id, animator] : animators_)
    {
        if (animator.playback_state == AnimatorPlaybackState::Playing || animator.playback_state == AnimatorPlaybackState::Blending)
        {
            work_list.push_back(id);
        }
    }

    std::sort(work_list.begin(), work_list.end(), [](AnimatorInstanceId left, AnimatorInstanceId right) {
        return left.value < right.value;
    });
    return work_list;
}

PoseBuffer AnimationRuntime::BuildPoseBuffer(const AnimatorRecord& animator) const
{
    auto skeleton = skeletons_.find(animator.desc.skeleton);
    std::uint32_t bone_count = skeleton == skeletons_.end() ? 0 : skeleton->second.joint_count;
    if (bone_count == 0 && dependencies_.resources != nullptr)
    {
        const auto loaded = dependencies_.resources->LoadSkeleton(animator.desc.skeleton);
        if (loaded)
        {
            bone_count = loaded.Value().joint_count;
        }
    }

    PoseBuffer pose{animator.handle, animator.desc.owner, std::vector<Transform>(bone_count), animator.revision};
    const float sample = static_cast<float>(animator.playback.local_time.value.count()) / 1000000.0f;
    for (std::size_t index = 0; index < pose.bone_transforms.size(); ++index)
    {
        pose.bone_transforms[index].position.x = sample;
        pose.bone_transforms[index].position.y = static_cast<float>(index);
        if (animator.crossfade)
        {
            pose.bone_transforms[index].position.z = animator.crossfade->target_weight;
        }
    }
    return pose;
}

foundation::Result<PoseBuffer> AnimationRuntime::EvaluatePose(const AnimatorRecord& animator)
{
    const auto skeleton = ResolveSkeleton(animator.desc.skeleton);
    if (!skeleton)
    {
        return foundation::Result<PoseBuffer>::Failure(skeleton.GetError());
    }
    const auto source_clip = ResolveClip(animator.crossfade ? animator.crossfade->source_clip : animator.playback.clip);
    if (!source_clip)
    {
        return foundation::Result<PoseBuffer>::Failure(source_clip.GetError());
    }
    const auto target_clip = ResolveClip(animator.crossfade ? animator.crossfade->target_clip : animator.playback.clip);
    if (!target_clip)
    {
        return foundation::Result<PoseBuffer>::Failure(target_clip.GetError());
    }

    AnimationEvaluationRequest request{};
    request.animator = animator.handle;
    request.owner = animator.desc.owner;
    request.skeleton = skeleton.Value();
    request.source_clip = source_clip.Value();
    request.target_clip = target_clip.Value();
    request.source_time = animator.crossfade ? animator.crossfade->source_time : animator.playback.local_time;
    request.target_time = animator.crossfade ? animator.crossfade->target_time : animator.playback.local_time;
    request.source_weight = animator.crossfade ? animator.crossfade->source_weight : 1.0f;
    request.target_weight = animator.crossfade ? animator.crossfade->target_weight : 0.0f;
    request.lod = animator.desc.lod;
    request.revision = animator.revision;

    if (dependencies_.evaluator == nullptr && !options_.enable_mock_pose_evaluation)
    {
        return foundation::Result<PoseBuffer>::Failure(
            foundation::Error::Create("animation.evaluator_missing", "animation evaluator backend is required for pose publication"));
    }

    if (dependencies_.evaluator != nullptr)
    {
        auto evaluated = dependencies_.evaluator->EvaluatePose(request);
        if (!evaluated) return evaluated;
        if (evaluated.Value().animator != animator.handle || evaluated.Value().owner != animator.desc.owner ||
            evaluated.Value().bone_transforms.size() != request.skeleton.joint_count)
        {
            return foundation::Result<PoseBuffer>::Failure(
                foundation::Error::Create("animation.invalid_pose", "animation evaluator returned a pose for the wrong animator, owner, or skeleton"));
        }
        return evaluated;
    }

    return foundation::Result<PoseBuffer>::Success(BuildPoseBuffer(animator));
}

foundation::Result<void> AnimationRuntime::PublishPose(std::shared_ptr<const PoseBuffer> pose)
{
    if(!dependencies_.pose_sink) return foundation::Result<void>::Success();
    try { return dependencies_.pose_sink->Publish(std::move(pose)); }
    catch (...) { return foundation::Result<void>::Failure(foundation::Error::Create("animation.publish_exception","animation pose sink callback threw before confirmed commit")); }
}


foundation::Result<void> AnimationRuntime::AdvancePlayback(AnimatorRecord& animator, FrameDuration delta)
{
    const auto scaled = ScalePlaybackDelta(
        delta.value.count(),
        animator.playback.playback_rate,
        animator.exact_fractional_microseconds,
        animator.playback.fractional_microseconds);
    if (!scaled)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.time_overflow", "scaled playback delta exceeds runtime range"));
    }

    const auto next = CheckedAdd(animator.playback.local_time, FrameDuration{std::chrono::microseconds{scaled->whole_microseconds}});
    if (!next)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.time_overflow", "animation local time overflow"));
    }

    animator.exact_fractional_microseconds = scaled->exact_fractional_microseconds;
    animator.playback.fractional_microseconds = scaled->fractional_microseconds;
    animator.playback.local_time = *next;
    return foundation::Result<void>::Success();
}


foundation::Result<void> AnimationRuntime::AdvanceCrossfade(AnimatorRecord& animator, FrameDuration delta)
{
    if (!animator.crossfade)
    {
        return foundation::Result<void>::Success();
    }
    const auto target = CheckedAdd(animator.crossfade->target_time, delta);
    const auto elapsed = CheckedAdd(animator.crossfade->elapsed, delta);
    if (!target || !elapsed)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.time_overflow", "animation crossfade time overflow"));
    }
    animator.crossfade->source_time=animator.playback.local_time; animator.crossfade->target_time=*target; animator.crossfade->elapsed=*elapsed; const auto duration=std::max<std::int64_t>(1,animator.crossfade->duration.value.count()); const float t=std::clamp(static_cast<float>(animator.crossfade->elapsed.value.count())/static_cast<float>(duration),0.0f,1.0f); animator.crossfade->source_weight=1.0f-t; animator.crossfade->target_weight=t; if(t>=1.0f){animator.playback.clip=animator.crossfade->target_clip;animator.playback.local_time=animator.crossfade->target_time;animator.playback_state=AnimatorPlaybackState::Playing;animator.crossfade.reset();} return foundation::Result<void>::Success();
}


void AnimationRuntime::QueueEvent(AnimatorInstanceId animator, std::string_view name, float time) noexcept
{
    if (!event_storage_ready_ || events_.empty()) return;
    if (event_count_ == events_.size())
    {
        static_assert(std::is_nothrow_swappable_v<AnimationEvent>, "bounded event overflow rotation must not throw");
        std::rotate(events_.begin(), events_.begin() + 1, events_.end());
        --event_count_;
    }
    AnimationEvent& slot = events_[event_count_++];
    slot.animator = animator;
    slot.name.assign(name.data(), name.size());
    slot.time = time;
}

foundation::Result<std::uint64_t> AnimationRuntime::NextRevision(const AnimatorRecord& animator) const
{
    const auto next=CheckedRevisionIncrement(animator.revision); if(!next) return foundation::Result<std::uint64_t>::Failure(foundation::Error::Create("animation.revision_exhausted","animator revision is exhausted")); return foundation::Result<std::uint64_t>::Success(*next);
}

foundation::Result<void> AnimationRuntime::ValidateSkeleton(const SkeletonDesc& desc) const
{
    if (!desc.id.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_skeleton", "skeleton id must be valid"));
    }
    if (desc.joint_count == 0)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.empty_skeleton", "skeleton must contain at least one joint"));
    }
    if (options_.max_skeleton_joints != 0 && desc.joint_count > options_.max_skeleton_joints)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.skeleton_too_large", "skeleton joint count exceeds runtime limit"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> AnimationRuntime::ValidateClip(const AnimationClipDesc& desc) const
{
    if (!desc.id.IsValid() || !desc.skeleton.IsValid() || !IsFinitePositive(desc.duration_seconds))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.invalid_clip", "animation clip descriptor is invalid"));
    }
    if (!CheckedSecondsToMicroseconds(static_cast<double>(desc.duration_seconds)))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("animation.clip_duration_overflow", "animation clip duration exceeds runtime time range"));
    }
    return foundation::Result<void>::Success();
}

void AnimationRuntime::SetRevisionForTesting(AnimatorHandle handle, std::uint64_t revision) noexcept { if(auto* a=FindAnimator(handle)) a->revision=revision; }
void AnimationRuntime::SetNextIdentityForTesting(std::uint64_t id, std::uint32_t generation) noexcept { next_animator_value_=id; next_generation_=generation; }
void AnimationRuntime::SetFractionalMicrosecondsForTesting(AnimatorHandle handle, double fractional_microseconds) noexcept
{
    if (auto* animator = FindAnimator(handle))
    {
        animator->playback.fractional_microseconds = fractional_microseconds;
        animator->exact_fractional_microseconds.fill(0);
        if (std::isfinite(fractional_microseconds) && fractional_microseconds >= 0.0 && fractional_microseconds < 1.0)
        {
            AddFractionToGrid(animator->exact_fractional_microseconds, DecomposeFraction(fractional_microseconds));
        }
    }
}
double AnimationRuntime::FractionalMicrosecondsForTesting(AnimatorHandle handle) const noexcept { if(const auto* a=FindAnimator(handle)) return a->playback.fractional_microseconds; return 0.0; }
bool AnimationRuntime::HasExactFractionalMicrosecondsForTesting(AnimatorHandle handle) const noexcept
{
    if (const auto* animator = FindAnimator(handle)) return !GridIsZero(animator->exact_fractional_microseconds);
    return false;
}

std::array<std::uint64_t, 17> AnimationRuntime::ExactFractionalMicrosecondsForTesting(AnimatorHandle handle) const noexcept
{
    if (const auto* animator = FindAnimator(handle)) return animator->exact_fractional_microseconds;
    return {};
}

} // namespace epidemic::runtime::animation

