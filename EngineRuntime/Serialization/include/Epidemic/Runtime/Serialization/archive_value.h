#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace epidemic::runtime
{
enum class ArchiveValueKind
{
    Null,
    String,
    UInt64,
    Int64,
    Double,
    Bool,
    Bytes,
    Object,
    Array,
};

struct ArchiveObject;
struct ArchiveArray;
using ArchiveObjectPtr = std::shared_ptr<ArchiveObject>;
using ArchiveArrayPtr = std::shared_ptr<ArchiveArray>;

struct ArchiveValue
{
    using Storage = std::variant<std::monostate, std::string, std::uint64_t, std::int64_t, double, bool,
                                 std::vector<std::byte>, ArchiveObjectPtr, ArchiveArrayPtr>;

    Storage storage{};

    [[nodiscard]] ArchiveValueKind Kind() const noexcept
    {
        switch (storage.index())
        {
        case 0: return ArchiveValueKind::Null;
        case 1: return ArchiveValueKind::String;
        case 2: return ArchiveValueKind::UInt64;
        case 3: return ArchiveValueKind::Int64;
        case 4: return ArchiveValueKind::Double;
        case 5: return ArchiveValueKind::Bool;
        case 6: return ArchiveValueKind::Bytes;
        case 7: return ArchiveValueKind::Object;
        default: return ArchiveValueKind::Array;
        }
    }
};

struct ArchiveObject
{
    std::unordered_map<std::string, ArchiveValue> fields;
};

struct ArchiveArray
{
    std::vector<ArchiveValue> elements;
};

struct SerializedDocument
{
    foundation::StringId type_id{};
    SchemaVersion schema_version{};
    std::uint32_t format_version = 1;
    ArchiveObjectPtr root{};
};
} // namespace epidemic::runtime
