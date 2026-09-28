#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <ratio>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace epidemic::foundation
{
// This file defines small time and frame-count wrappers shared by the frame loop.
// The wrappers make time conversions explicit and keep frame indexing distinct from
// raw integers in higher-level code.

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

// Lightweight wrapper around frame-relative durations.
class FrameTime
{
  public:
    // Wraps an engine-native Duration into FrameTime.
    [[nodiscard]] static constexpr FrameTime FromDuration(Duration duration) noexcept
    {
        return FrameTime(duration);
    }

    // Converts any std::chrono duration into the engine-native duration domain.
    // Non-finite floating inputs and values outside Duration's representable range are rejected.
    template <typename Rep, typename Period> [[nodiscard]] static constexpr FrameTime
    FromChrono(std::chrono::duration<Rep, Period> duration)
    {
        using source_duration = std::chrono::duration<Rep, Period>;
        using destination_rep = Duration::rep;

        if constexpr (std::is_same_v<source_duration, Duration>)
        {
            return FrameTime(duration);
        }
        else if constexpr (std::is_integral_v<Rep> && std::is_integral_v<destination_rep> &&
                           std::ratio_equal_v<Period, Duration::period>)
        {
            if (std::cmp_less(duration.count(), std::numeric_limits<destination_rep>::min()) ||
                std::cmp_greater(duration.count(), std::numeric_limits<destination_rep>::max()))
            {
                throw std::out_of_range("FrameTime duration is outside the native range");
            }
            return FrameTime(Duration(static_cast<destination_rep>(duration.count())));
        }
        else
        {
            using checked_duration = std::chrono::duration<long double, Duration::period>;
            if constexpr (std::is_floating_point_v<Rep>)
            {
                if (!std::isfinite(static_cast<long double>(duration.count())))
                {
                    throw std::invalid_argument("FrameTime duration must be finite");
                }
            }

            const long double converted = checked_duration(duration).count();
            if (!std::isfinite(converted))
            {
                throw std::out_of_range("FrameTime duration is outside the native range");
            }

            const long double minimum = static_cast<long double>(std::numeric_limits<destination_rep>::min());
            const long double maximum = static_cast<long double>(std::numeric_limits<destination_rep>::max());
            if (converted < minimum || converted > maximum)
            {
                throw std::out_of_range("FrameTime duration is outside the native range");
            }

            return FrameTime(std::chrono::duration_cast<Duration>(duration));
        }
    }

    // Builds a FrameTime from seconds.
    [[nodiscard]] static FrameTime FromSeconds(double seconds)
    {
        return FromChrono(std::chrono::duration<double>(seconds));
    }

    // Builds a FrameTime from milliseconds.
    [[nodiscard]] static FrameTime FromMilliseconds(double milliseconds)
    {
        return FromChrono(std::chrono::duration<double, std::milli>(milliseconds));
    }

    // Returns the underlying engine-native duration.
    [[nodiscard]] constexpr Duration Raw() const noexcept
    {
        return duration_;
    }

    // Returns the duration in seconds as a floating-point value.
    [[nodiscard]] double Seconds() const noexcept
    {
        return std::chrono::duration<double>(duration_).count();
    }

    // Returns the duration in milliseconds as a floating-point value.
    [[nodiscard]] double Milliseconds() const noexcept
    {
        return std::chrono::duration<double, std::milli>(duration_).count();
    }

    // Returns true when the wrapped duration is zero.
    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return duration_ == Duration::zero();
    }

    // Explicit conversion back to the raw duration type.
    [[nodiscard]] constexpr explicit operator Duration() const noexcept
    {
        return duration_;
    }

    // Compares the wrapped duration values directly.
    [[nodiscard]] constexpr bool operator==(const FrameTime &) const noexcept = default;

  private:
    // Internal constructor used by the static factories above.
    explicit constexpr FrameTime(Duration duration) noexcept : duration_(duration)
    {
    }

    Duration duration_{};
};

// Monotonic frame counter wrapper.
class FrameIndex
{
  public:
    // Builds frame index 0.
    constexpr FrameIndex() noexcept = default;

    // Builds a frame index from an explicit raw value.
    explicit constexpr FrameIndex(std::uint64_t value) noexcept : value_(value)
    {
    }

    // Returns the raw frame number.
    [[nodiscard]] constexpr std::uint64_t Value() const noexcept
    {
        return value_;
    }

    // Returns the next frame index without mutating the current instance.
    // Throws when UINT64_MAX has no representable successor.
    [[nodiscard]] constexpr FrameIndex Next() const
    {
        if (value_ == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("FrameIndex overflow");
        }
        return FrameIndex(value_ + 1);
    }

    // Prefix increment advances the frame index in place.
    // Throws without modifying the value when UINT64_MAX is reached.
    constexpr FrameIndex &operator++()
    {
        if (value_ == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("FrameIndex overflow");
        }
        ++value_;
        return *this;
    }

    // Postfix increment returns the previous value and then advances the index.
    // Throws without modifying the value when UINT64_MAX is reached.
    constexpr FrameIndex operator++(int)
    {
        FrameIndex previous(*this);
        ++(*this);
        return previous;
    }

    // Compares frame indices by raw numeric value.
    [[nodiscard]] constexpr bool operator==(const FrameIndex &) const noexcept = default;

  private:
    std::uint64_t value_{};
};
} // namespace epidemic::foundation

namespace std
{
template <> struct hash<epidemic::foundation::FrameIndex>
{
    // Hashes the raw frame index value for unordered container support.
    [[nodiscard]] size_t operator()(const epidemic::foundation::FrameIndex &value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Value());
    }
};
} // namespace std
