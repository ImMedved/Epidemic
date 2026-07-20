#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

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
    template <typename Rep, typename Period> [[nodiscard]] static constexpr FrameTime
    FromChrono(std::chrono::duration<Rep, Period> duration) noexcept
    {
        return FrameTime(std::chrono::duration_cast<Duration>(duration));
    }

    // Builds a FrameTime from seconds.
    [[nodiscard]] static FrameTime FromSeconds(double seconds) noexcept
    {
        return FromChrono(std::chrono::duration<double>(seconds));
    }

    // Builds a FrameTime from milliseconds.
    [[nodiscard]] static FrameTime FromMilliseconds(double milliseconds) noexcept
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
    [[nodiscard]] constexpr FrameIndex Next() const noexcept
    {
        return FrameIndex(value_ + 1);
    }

    // Prefix increment advances the frame index in place.
    constexpr FrameIndex &operator++() noexcept
    {
        ++value_;
        return *this;
    }

    // Postfix increment returns the previous value and then advances the index.
    constexpr FrameIndex operator++(int) noexcept
    {
        FrameIndex previous(*this);
        ++value_;
        return previous;
    }

    // Compares frame indices by raw numeric value.
    [[nodiscard]] constexpr bool operator==(const FrameIndex &) const noexcept = default;

  private:
    std::uint64_t value_{};
};
} 

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