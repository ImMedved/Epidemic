#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::foundation
{
namespace detail
{
constexpr std::uint64_t HashString(std::string_view value) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const char character : value)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace detail

struct StringId
{
    std::uint64_t value{};

    [[nodiscard]] static constexpr StringId FromString(std::string_view source) noexcept
    {
        return StringId{detail::HashString(source)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] constexpr bool operator==(const StringId &) const noexcept = default;
};

struct NameId
{
    std::uint64_t value{};

    [[nodiscard]] static constexpr NameId FromString(std::string_view source) noexcept
    {
        return NameId{detail::HashString(source)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] constexpr bool operator==(const NameId &) const noexcept = default;
};
} // namespace epidemic::foundation
