#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <cmath>

namespace epidemic::runtime
{
struct RuntimeFrameDuration
{
    std::chrono::microseconds value{};

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return value.count() == 0;
    }

    [[nodiscard]] constexpr bool IsNegative() const noexcept
    {
        return value.count() < 0;
    }

    [[nodiscard]] constexpr bool operator==(const RuntimeFrameDuration&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const RuntimeFrameDuration&) const noexcept = default;
};


[[nodiscard]] inline std::optional<RuntimeFrameDuration> CheckedScaleDuration(
    RuntimeFrameDuration duration, double rate) noexcept
{
    if (duration.IsNegative() || !std::isfinite(rate) || rate < 0.0)
    {
        return std::nullopt;
    }

    // `double` is binary on every supported toolchain. Reconstruct its exact significand, multiply that
    // by the non-negative integer duration as a portable two-word product, then apply the binary exponent.
    // Right shifts implement truncation exactly, and every left shift is range-checked before conversion.
    const auto checked_product = [](std::uint64_t integer, double factor) noexcept -> std::optional<std::int64_t> {
        static_assert(std::numeric_limits<double>::radix == 2);
        static_assert(std::numeric_limits<double>::digits <= 63);
        if (integer == 0 || factor == 0.0)
        {
            return std::int64_t{0};
        }

        int exponent = 0;
        const double fraction = std::frexp(factor, &exponent);
        const int digits = std::numeric_limits<double>::digits;
        const auto significand = static_cast<std::uint64_t>(std::ldexp(fraction, digits));
        const int binary_exponent = exponent - digits;

        const std::uint64_t mask = 0xffffffffULL;
        const std::uint64_t a0 = integer & mask;
        const std::uint64_t a1 = integer >> 32;
        const std::uint64_t b0 = significand & mask;
        const std::uint64_t b1 = significand >> 32;
        const std::uint64_t p00 = a0 * b0;
        const std::uint64_t p01 = a0 * b1;
        const std::uint64_t p10 = a1 * b0;
        const std::uint64_t p11 = a1 * b1;
        const std::uint64_t middle = (p00 >> 32) + (p01 & mask) + (p10 & mask);
        const std::uint64_t low = (middle << 32) | (p00 & mask);
        const std::uint64_t high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
        constexpr std::uint64_t maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

        if (binary_exponent >= 0)
        {
            if (high != 0 || binary_exponent >= 63 || low > (maximum >> binary_exponent))
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(low << binary_exponent);
        }

        const int shift = -binary_exponent;
        std::uint64_t truncated = 0;
        if (shift < 64)
        {
            if ((high >> shift) != 0)
            {
                return std::nullopt;
            }
            truncated = (high << (64 - shift)) | (low >> shift);
        }
        else if (shift < 128)
        {
            truncated = high >> (shift - 64);
        }

        if (truncated > maximum)
        {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(truncated);
    };

    const auto scaled = checked_product(static_cast<std::uint64_t>(duration.value.count()), rate);
    if (!scaled)
    {
        return std::nullopt;
    }
    return RuntimeFrameDuration{std::chrono::microseconds{*scaled}};
}

[[nodiscard]] inline std::optional<RuntimeFrameDuration> CheckedSecondsToMicroseconds(double seconds) noexcept
{
    if (!std::isfinite(seconds) || seconds < 0.0)
    {
        return std::nullopt;
    }

    // One second is exactly one million microseconds. Reuse the exact checked `integer * double` path so the
    // represented input is truncated mathematically without requiring a wider floating-point type.
    return CheckedScaleDuration(RuntimeFrameDuration{std::chrono::microseconds{1000000}}, seconds);
}

[[nodiscard]] constexpr std::optional<RuntimeFrameDuration> CheckedAdd(
    RuntimeFrameDuration left, RuntimeFrameDuration right) noexcept
{
    const auto a = left.value.count();
    const auto b = right.value.count();
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
    {
        return std::nullopt;
    }
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)
    {
        return std::nullopt;
    }
    return RuntimeFrameDuration{std::chrono::microseconds{a + b}};
}


struct GameDuration
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return ticks == 0;
    }

    [[nodiscard]] constexpr bool operator==(const GameDuration&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameDuration&) const noexcept = default;
};

struct GameTimePoint
{
    std::int64_t ticks = 0;

    [[nodiscard]] constexpr bool operator==(const GameTimePoint&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameTimePoint&) const noexcept = default;
};

[[nodiscard]] constexpr std::optional<GameTimePoint> CheckedAdd(GameTimePoint point, GameDuration duration) noexcept
{
    if (duration.ticks > 0 && point.ticks > std::numeric_limits<std::int64_t>::max() - duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks < std::numeric_limits<std::int64_t>::min() - duration.ticks)
    {
        return std::nullopt;
    }
    return GameTimePoint{point.ticks + duration.ticks};
}

[[nodiscard]] constexpr std::optional<GameTimePoint> CheckedSubtract(GameTimePoint point, GameDuration duration) noexcept
{
    if (duration.ticks > 0 && point.ticks < std::numeric_limits<std::int64_t>::min() + duration.ticks)
    {
        return std::nullopt;
    }
    if (duration.ticks < 0 && point.ticks > std::numeric_limits<std::int64_t>::max() + duration.ticks)
    {
        return std::nullopt;
    }
    return GameTimePoint{point.ticks - duration.ticks};
}

[[nodiscard]] constexpr std::optional<GameDuration> CheckedDifference(GameTimePoint end, GameTimePoint begin) noexcept
{
    if (begin.ticks < 0 && end.ticks > std::numeric_limits<std::int64_t>::max() + begin.ticks)
    {
        return std::nullopt;
    }
    if (begin.ticks > 0 && end.ticks < std::numeric_limits<std::int64_t>::min() + begin.ticks)
    {
        return std::nullopt;
    }
    return GameDuration{end.ticks - begin.ticks};
}

[[nodiscard]] constexpr GameTimePoint SaturatingAdd(GameTimePoint point, GameDuration duration) noexcept
{
    if (const auto checked = CheckedAdd(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameTimePoint{std::numeric_limits<std::int64_t>::max()}
                               : GameTimePoint{std::numeric_limits<std::int64_t>::min()};
}

[[nodiscard]] constexpr GameTimePoint SaturatingSubtract(GameTimePoint point, GameDuration duration) noexcept
{
    if (const auto checked = CheckedSubtract(point, duration))
    {
        return *checked;
    }
    return duration.ticks >= 0 ? GameTimePoint{std::numeric_limits<std::int64_t>::min()}
                               : GameTimePoint{std::numeric_limits<std::int64_t>::max()};
}

[[nodiscard]] constexpr GameDuration SaturatingDifference(GameTimePoint end, GameTimePoint begin) noexcept
{
    if (const auto checked = CheckedDifference(end, begin))
    {
        return *checked;
    }
    return end.ticks >= begin.ticks ? GameDuration{std::numeric_limits<std::int64_t>::max()}
                                    : GameDuration{std::numeric_limits<std::int64_t>::min()};
}

// Convenience operators use saturating semantics. Use CheckedAdd/CheckedSubtract/
// CheckedDifference when overflow must be reported instead of clamped.
[[nodiscard]] constexpr GameTimePoint operator+(GameTimePoint point, GameDuration duration) noexcept
{
    return SaturatingAdd(point, duration);
}

[[nodiscard]] constexpr GameTimePoint operator-(GameTimePoint point, GameDuration duration) noexcept
{
    return SaturatingSubtract(point, duration);
}

[[nodiscard]] constexpr GameDuration operator-(GameTimePoint end, GameTimePoint begin) noexcept
{
    return SaturatingDifference(end, begin);
}
} // namespace epidemic::runtime
