#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"

#include <limits>
#include <string_view>
#include <type_traits>

namespace epidemic::runtime
{
template <typename TInteger>
[[nodiscard]] constexpr bool CanAllocateMonotonicId(TInteger next_value) noexcept
{
    static_assert(std::is_integral_v<TInteger> && std::is_unsigned_v<TInteger>);
    return next_value != 0;
}

template <typename TInteger>
[[nodiscard]] foundation::Result<TInteger> AllocateMonotonicId(
    TInteger& next_value,
    std::string_view error_code = "runtime.id_exhausted",
    std::string_view error_message = "runtime monotonic id allocator is exhausted")
{
    static_assert(std::is_integral_v<TInteger> && std::is_unsigned_v<TInteger>);

    if (next_value == 0)
    {
        return foundation::Result<TInteger>::Failure(foundation::Error::Create(error_code, error_message));
    }

    const TInteger allocated = next_value;
    if (next_value == std::numeric_limits<TInteger>::max())
    {
        next_value = 0;
    }
    else
    {
        ++next_value;
    }
    return foundation::Result<TInteger>::Success(allocated);
}

template <typename TInteger>
[[nodiscard]] foundation::Result<TInteger> PeekMonotonicId(
    TInteger next_value,
    std::string_view error_code = "runtime.id_exhausted",
    std::string_view error_message = "runtime monotonic id allocator is exhausted")
{
    static_assert(std::is_integral_v<TInteger> && std::is_unsigned_v<TInteger>);
    if (next_value == 0)
    {
        return foundation::Result<TInteger>::Failure(foundation::Error::Create(error_code, error_message));
    }
    return foundation::Result<TInteger>::Success(next_value);
}

template <typename TInteger>
constexpr void CommitMonotonicId(TInteger& next_value, TInteger reserved_value) noexcept
{
    static_assert(std::is_integral_v<TInteger> && std::is_unsigned_v<TInteger>);
    if (next_value != reserved_value || reserved_value == 0)
    {
        return;
    }
    next_value = reserved_value == std::numeric_limits<TInteger>::max() ? 0 : static_cast<TInteger>(reserved_value + 1);
}

template <typename TInteger>
class MonotonicIdReservation
{
public:
    MonotonicIdReservation() = default;
    MonotonicIdReservation(TInteger* owner, TInteger value) noexcept : owner_(owner), value_(value) {}

    [[nodiscard]] TInteger Value() const noexcept { return value_; }
    [[nodiscard]] bool IsValid() const noexcept { return owner_ != nullptr && value_ != 0; }
    void Commit() noexcept
    {
        if (owner_ != nullptr)
        {
            CommitMonotonicId(*owner_, value_);
            owner_ = nullptr;
        }
    }
    void Rollback() noexcept { owner_ = nullptr; }

private:
    TInteger* owner_ = nullptr;
    TInteger value_ = 0;
};

template <typename TInteger>
[[nodiscard]] foundation::Result<MonotonicIdReservation<TInteger>> ReserveMonotonicId(
    TInteger& next_value,
    std::string_view error_code = "runtime.id_exhausted",
    std::string_view error_message = "runtime monotonic id allocator is exhausted")
{
    const auto peeked = PeekMonotonicId(next_value, error_code, error_message);
    if (!peeked)
    {
        return foundation::Result<MonotonicIdReservation<TInteger>>::Failure(peeked.GetError());
    }
    return foundation::Result<MonotonicIdReservation<TInteger>>::Success(
        MonotonicIdReservation<TInteger>{&next_value, peeked.Value()});
}

} // namespace epidemic::runtime
