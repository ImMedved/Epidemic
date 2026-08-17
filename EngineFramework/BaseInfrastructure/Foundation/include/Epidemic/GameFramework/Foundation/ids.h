#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string_view>

namespace epidemic::gameplay
{
namespace detail
{
constexpr std::uint64_t Hash64(std::string_view value, std::uint64_t seed) noexcept
{
    std::uint64_t hash = seed;
    for (const char character : value)
    {
        hash ^= static_cast<std::uint8_t>(character);
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] constexpr bool IsCanonicalNameCharacter(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
}

[[nodiscard]] constexpr bool IsCanonicalName(std::string_view value) noexcept
{
    if (value.empty() || value.front() == '.' || value.back() == '.')
    {
        return false;
    }

    bool previous_was_separator = false;
    for (const char character : value)
    {
        if (character == '.')
        {
            if (previous_was_separator)
            {
                return false;
            }
            previous_was_separator = true;
            continue;
        }
        if (!IsCanonicalNameCharacter(character))
        {
            return false;
        }
        previous_was_separator = false;
    }
    return true;
}

struct StableId128
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return high != 0 || low != 0; }
    [[nodiscard]] constexpr bool operator==(const StableId128&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StableId128&) const noexcept = default;
};

template <typename Tag> struct StrongId128
{
    StableId128 value{};

    [[nodiscard]] static constexpr StrongId128 FromRaw(std::uint64_t high, std::uint64_t low) noexcept
    {
        return StrongId128{StableId128{high, low}};
    }

    [[nodiscard]] static constexpr StrongId128 FromString(std::string_view source) noexcept
    {
        if (source.empty())
        {
            return {};
        }
        constexpr std::uint64_t kSeedA = 14695981039346656037ull;
        constexpr std::uint64_t kSeedB = 1099511628211ull ^ 0x9E3779B97F4A7C15ull;
        auto high = Hash64(source, kSeedA);
        auto low = Hash64(source, kSeedB);
        if (high == 0 && low == 0)
        {
            low = 1;
        }
        return FromRaw(high, low);
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value.IsValid(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr std::uint64_t High() const noexcept { return value.high; }
    [[nodiscard]] constexpr std::uint64_t Low() const noexcept { return value.low; }
    [[nodiscard]] constexpr bool operator==(const StrongId128&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StrongId128&) const noexcept = default;
};

template <typename Tag> struct StrongTypeId
{
    std::uint64_t value = 0;

    [[nodiscard]] static constexpr StrongTypeId FromRaw(std::uint64_t raw) noexcept { return StrongTypeId{raw}; }

    [[nodiscard]] static constexpr StrongTypeId FromString(std::string_view source) noexcept
    {
        if (source.empty())
        {
            return {};
        }

        auto raw = Hash64(source, 14695981039346656037ull);
        if (raw == 0)
        {
            raw = 1;
        }
        return StrongTypeId{raw};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept { return value; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const StrongTypeId&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const StrongTypeId&) const noexcept = default;
};

struct GameplayObjectIdTag {};
struct OperationIdTag {};
struct CorrelationIdTag {};
struct QueryIdTag {};
struct EventIdTag {};
struct FactIdTag {};
struct ScheduleIdTag {};

struct GameplayDomainIdTag {};
struct TypeIdTag {};
struct QueryTypeIdTag {};
struct EventTypeIdTag {};
struct FactTypeIdTag {};
struct ActionTypeIdTag {};
struct TagIdTag {};
struct ClockIdTag {};
struct SubscriberIdTag {};
struct ProducerIdTag {};
struct IdScopeIdTag {};
} // namespace detail

using GameplayObjectId = detail::StrongId128<detail::GameplayObjectIdTag>;
using OperationId = detail::StrongId128<detail::OperationIdTag>;
using CorrelationId = detail::StrongId128<detail::CorrelationIdTag>;
using QueryId = detail::StrongId128<detail::QueryIdTag>;
using EventId = detail::StrongId128<detail::EventIdTag>;
using FactId = detail::StrongId128<detail::FactIdTag>;
using ScheduleId = detail::StrongId128<detail::ScheduleIdTag>;

using GameplayDomainId = detail::StrongTypeId<detail::GameplayDomainIdTag>;
using TypeId = detail::StrongTypeId<detail::TypeIdTag>;
using QueryTypeId = detail::StrongTypeId<detail::QueryTypeIdTag>;
using EventTypeId = detail::StrongTypeId<detail::EventTypeIdTag>;
using FactTypeId = detail::StrongTypeId<detail::FactTypeIdTag>;
using ActionTypeId = detail::StrongTypeId<detail::ActionTypeIdTag>;
using TagId = detail::StrongTypeId<detail::TagIdTag>;
using ClockId = detail::StrongTypeId<detail::ClockIdTag>;
using SubscriberId = detail::StrongTypeId<detail::SubscriberIdTag>;
using ProducerId = detail::StrongTypeId<detail::ProducerIdTag>;
using IdScopeId = detail::StrongTypeId<detail::IdScopeIdTag>;

struct GameplayObjectRef
{
    GameplayDomainId domain{};
    GameplayObjectId id{};

    [[nodiscard]] constexpr bool IsValid() const noexcept { return domain.IsValid() && id.IsValid(); }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsValid(); }
    [[nodiscard]] constexpr bool operator==(const GameplayObjectRef&) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const GameplayObjectRef&) const noexcept = default;
};
} // namespace epidemic::gameplay

namespace std
{
template <typename Tag> struct hash<epidemic::gameplay::detail::StrongId128<Tag>>
{
    [[nodiscard]] size_t operator()(const epidemic::gameplay::detail::StrongId128<Tag>& id) const noexcept
    {
        const auto a = hash<std::uint64_t>{}(id.High());
        const auto b = hash<std::uint64_t>{}(id.Low());
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
};

template <typename Tag> struct hash<epidemic::gameplay::detail::StrongTypeId<Tag>>
{
    [[nodiscard]] size_t operator()(const epidemic::gameplay::detail::StrongTypeId<Tag>& id) const noexcept
    {
        return hash<std::uint64_t>{}(id.Raw());
    }
};

template <> struct hash<epidemic::gameplay::GameplayObjectRef>
{
    [[nodiscard]] size_t operator()(const epidemic::gameplay::GameplayObjectRef& ref) const noexcept
    {
        const auto a = hash<epidemic::gameplay::GameplayDomainId>{}(ref.domain);
        const auto b = hash<epidemic::gameplay::GameplayObjectId>{}(ref.id);
        return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
    }
};
} // namespace std