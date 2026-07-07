#pragma once

#include <cstdint>
#include <functional>
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

template <typename Tag> struct BasicId
{
    std::uint64_t value{};

    [[nodiscard]] static constexpr BasicId FromString(std::string_view source) noexcept
    {
        return source.empty() ? BasicId{0} : BasicId{HashString(source)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept
    {
        return value;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

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
} // namespace epidemic::foundation

namespace std
{
template <typename Tag> struct hash<epidemic::foundation::detail::BasicId<Tag>>
{
    [[nodiscard]] size_t operator()(const epidemic::foundation::detail::BasicId<Tag> &value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};
} // namespace std