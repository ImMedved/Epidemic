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
} // namespace epidemic::runtime
