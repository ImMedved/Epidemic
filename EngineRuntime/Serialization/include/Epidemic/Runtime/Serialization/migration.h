#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <functional>

namespace epidemic::runtime
{
struct MigrationKey
{
    foundation::StringId type_id{};
    SchemaVersion from{};
    SchemaVersion to{};

    [[nodiscard]] constexpr bool operator==(const MigrationKey&) const noexcept = default;
};

class IMigration
{
  public:
    // Function note: Handles ~imigration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    virtual ~IMigration() = default;

    // Function note: Gets key.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual MigrationKey GetKey() const = 0;
    // Function note: Handles apply.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] virtual foundation::Result<void> Apply(IArchiveReader& input, IArchiveWriter& output) = 0;
};
} 

namespace std
{
template <> struct hash<epidemic::runtime::MigrationKey>
{
    [[nodiscard]] size_t operator()(const epidemic::runtime::MigrationKey& key) const noexcept
    {
        size_t seed = hash<std::uint64_t>{}(key.type_id.Raw());
        seed ^= hash<std::uint32_t>{}(key.from.major) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<std::uint32_t>{}(key.from.minor) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<std::uint32_t>{}(key.from.patch) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<std::uint32_t>{}(key.to.major) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<std::uint32_t>{}(key.to.minor) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hash<std::uint32_t>{}(key.to.patch) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
} // namespace std
