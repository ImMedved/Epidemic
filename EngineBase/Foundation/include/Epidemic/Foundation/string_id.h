#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace epidemic::foundation
{
// This file defines the small typed identifier family used across EngineBase.
// The ids are non-owning hashes derived from strings and are meant for stable comparisons,
// not for recovering original names.

namespace detail
{
// Computes a deterministic FNV-1a style hash for string-based ids.
// Input: borrowed string view.
// Output: stable 64-bit hash value.
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

// Generic strongly typed id wrapper.
// Relationship: the public id aliases below use this template with distinct tag types.
template <typename Tag> struct BasicId
{
    std::uint64_t value{};

    // Creates an id from a source string.
    // Input: source string view.
    // Output: hashed id, or 0 when the source is empty.
    [[nodiscard]] static constexpr BasicId FromString(std::string_view source) noexcept
    {
        return source.empty() ? BasicId{0} : BasicId{HashString(source)};
    }

    // Returns true when the id is non-zero.
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    // Returns the raw numeric id value.
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept
    {
        return value;
    }

    // Convenience boolean conversion for `if (id)` style checks.
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

    // Compares ids by raw value.
    [[nodiscard]] constexpr bool operator==(const BasicId &) const noexcept = default;
};

struct StringIdTag
{
};
struct NameIdTag
{
};
struct ModuleIdTag
{
};
struct ServiceIdTag
{
};
struct EventTypeIdTag
{
};
} // namespace detail

using StringId = detail::BasicId<detail::StringIdTag>;
using NameId = detail::BasicId<detail::NameIdTag>;
using ModuleId = detail::BasicId<detail::ModuleIdTag>;
using ServiceId = detail::BasicId<detail::ServiceIdTag>;
using EventTypeId = detail::BasicId<detail::EventTypeIdTag>;
} 

namespace std
{
template <typename Tag> struct hash<epidemic::foundation::detail::BasicId<Tag>>
{
    // Hashes the raw id so typed ids can be used in unordered containers.
    [[nodiscard]] size_t operator()(const epidemic::foundation::detail::BasicId<Tag> &value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};
} // namespace std