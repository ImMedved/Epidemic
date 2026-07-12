#pragma once

#include "Epidemic/Foundation/string_id.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <cstdint>
#include <functional>
#include <string_view>

namespace epidemic::runtime
{
namespace detail
{
struct AssetIdTag
{
};
struct ResourceIdTag
{
};
struct RuntimeObjectIdTag
{
};
struct PersistentObjectIdTag
{
};
struct RegionIdTag
{
};
struct ChunkIdTag
{
};
struct SurfaceIdTag
{
};
struct SimulationZoneIdTag
{
};

template <typename Tag> struct NumericRuntimeId
{
    // Function note: Handles numeric runtime id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    constexpr NumericRuntimeId() noexcept = default;
    constexpr explicit NumericRuntimeId(std::uint64_t raw_value) noexcept : value(raw_value)
    {
    }

    std::uint64_t value = 0;

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

    [[nodiscard]] constexpr bool operator==(const NumericRuntimeId&) const noexcept = default;
};
} // namespace detail

struct AssetId
{
    // Function note: Handles asset id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    constexpr AssetId() noexcept = default;
    constexpr explicit AssetId(foundation::StringId raw_value) noexcept : value(raw_value)
    {
    }

    [[nodiscard]] static constexpr AssetId FromString(std::string_view source) noexcept
    {
        return AssetId{foundation::StringId::FromString(source)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }

    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept
    {
        return value.Raw();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

    [[nodiscard]] constexpr bool operator==(const AssetId&) const noexcept = default;

    foundation::StringId value{};
};

struct ResourceId
{
    // Function note: Handles resource id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    constexpr ResourceId() noexcept = default;
    constexpr explicit ResourceId(foundation::StringId raw_value) noexcept : value(raw_value)
    {
    }

    [[nodiscard]] static constexpr ResourceId FromString(std::string_view source) noexcept
    {
        return ResourceId{foundation::StringId::FromString(source)};
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }

    [[nodiscard]] constexpr std::uint64_t Raw() const noexcept
    {
        return value.Raw();
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }

    [[nodiscard]] constexpr bool operator==(const ResourceId&) const noexcept = default;

    foundation::StringId value{};
};

struct RuntimeObjectId : detail::NumericRuntimeId<detail::RuntimeObjectIdTag>
{
    using detail::NumericRuntimeId<detail::RuntimeObjectIdTag>::NumericRuntimeId;
};

struct PersistentObjectId : detail::NumericRuntimeId<detail::PersistentObjectIdTag>
{
    using detail::NumericRuntimeId<detail::PersistentObjectIdTag>::NumericRuntimeId;
};

struct RegionId : detail::NumericRuntimeId<detail::RegionIdTag>
{
    using detail::NumericRuntimeId<detail::RegionIdTag>::NumericRuntimeId;
};

struct ChunkId : detail::NumericRuntimeId<detail::ChunkIdTag>
{
    using detail::NumericRuntimeId<detail::ChunkIdTag>::NumericRuntimeId;
};

struct SurfaceId : detail::NumericRuntimeId<detail::SurfaceIdTag>
{
    using detail::NumericRuntimeId<detail::SurfaceIdTag>::NumericRuntimeId;
};

struct SimulationZoneId : detail::NumericRuntimeId<detail::SimulationZoneIdTag>
{
    using detail::NumericRuntimeId<detail::SimulationZoneIdTag>::NumericRuntimeId;
};
} 

namespace std
{
template <typename Tag> struct hash<epidemic::runtime::detail::NumericRuntimeId<Tag>>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::detail::NumericRuntimeId<Tag> value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::AssetId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::AssetId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::ResourceId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::ResourceId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::RuntimeObjectId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::RuntimeObjectId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::PersistentObjectId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::PersistentObjectId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::RegionId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::RegionId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::ChunkId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::ChunkId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::SurfaceId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::SurfaceId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};

template <> struct hash<epidemic::runtime::SimulationZoneId>
{
    [[nodiscard]] size_t operator()(epidemic::runtime::SimulationZoneId value) const noexcept
    {
        return hash<std::uint64_t>{}(value.Raw());
    }
};
} // namespace std
