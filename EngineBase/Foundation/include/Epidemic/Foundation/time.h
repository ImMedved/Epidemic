#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace epidemic::foundation
{
using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;

class FrameTime
{
  public:
    [[nodiscard]] static constexpr FrameTime FromDuration(Duration duration) noexcept
    {
        return FrameTime(duration);
    }

    template <typename Rep, typename Period> [[nodiscard]] static constexpr FrameTime
    FromChrono(std::chrono::duration<Rep, Period> duration) noexcept
    {
        return FrameTime(std::chrono::duration_cast<Duration>(duration));
    }

    [[nodiscard]] static FrameTime FromSeconds(double seconds) noexcept
    {
        return FromChrono(std::chrono::duration<double>(seconds));
    }

    [[nodiscard]] static FrameTime FromMilliseconds(double milliseconds) noexcept
    {
        return FromChrono(std::chrono::duration<double, std::milli>(milliseconds));
    }

    [[nodiscard]] constexpr Duration Raw() const noexcept
    {
        return duration_;
    }

    [[nodiscard]] double Seconds() const noexcept
    {
        return std::chrono::duration<double>(duration_).count();
    }

    [[nodiscard]] double Milliseconds() const noexcept
    {
        return std::chrono::duration<double, std::milli>(duration_).count();
    }

    [[nodiscard]] constexpr bool IsZero() const noexcept
    {
        return duration_ == Duration::zero();
    }

    [[nodiscard]] constexpr explicit operator Duration() const noexcept
    {
        return duration_;
    }

    [[nodiscard]] constexpr bool operator==(const FrameTime &) const noexcept = default;

  private:
    explicit constexpr FrameTime(Duration duration) noexcept : duration_(duration)
    {
    }

    Duration duration_{};
};

class FrameIndex
{
  public:
    constexpr FrameIndex() noexcept = default;
    explicit constexpr FrameIndex(std::uint64_t value) noexcept : value_(value)
    {
    }

    [[nodiscard]] constexpr std::uint64_t Value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr FrameIndex Next() const noexcept
    {
        return FrameIndex(value_ + 1);
    }

    constexpr FrameIndex &operator++() noexcept
    {
        ++value_;
        return *this;
    }

    constexpr FrameIndex operator++(int) noexcept
    {
        FrameIndex previous(*this);
        ++value_;
        return previous;
    }

    [[nodiscard]] constexpr bool operator==(const FrameIndex &) const noexcept = default;

  private:
    std::uint64_t value_{};
};
} // namespace epidemic::foundation

namespace std
{
template <> struct hash<epidemic::foundation::FrameIndex>
{
    [[nodiscard]] size_t operator()(const epidemic::foundation::FrameIndex &value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Value());
    }
};
} // namespace std