#pragma once

#include <cstdint>
#include <string_view>

namespace epidemic::foundation
{
namespace detail
{
// Stable compile-time friendly FNV-1a hash used for lightweight engine identifiers.
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

    // Converts a string into a stable hashed identifier.
    [[nodiscard]] static constexpr StringId FromString(std::string_view source) noexcept
    {
        return StringId{detail::HashString(source)};
    }

    // Reports whether the identifier contains a non-zero hash value.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] constexpr bool operator==(const StringId &) const noexcept = default;
};

struct NameId
{
    std::uint64_t value{};

    // Converts a string into a stable hashed identifier for name-like contracts.
    [[nodiscard]] static constexpr NameId FromString(std::string_view source) noexcept
    {
        return NameId{detail::HashString(source)};
    }

    // Reports whether the identifier contains a non-zero hash value.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] constexpr bool operator==(const NameId &) const noexcept = default;
};
} // namespace epidemic::foundation
