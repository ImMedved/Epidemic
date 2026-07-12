#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
enum class ArchiveValueKind
{
    String,
    UInt64,
    Int64,
    Double,
    Bool,
    Object,
};

struct ArchiveObject;
using ArchiveObjectPtr = std::shared_ptr<ArchiveObject>;

struct ArchiveValue
{
    using Storage = std::variant<std::string, std::uint64_t, std::int64_t, double, bool, ArchiveObjectPtr>;

    Storage storage{};

    [[nodiscard]] ArchiveValueKind Kind() const noexcept
    {
        switch (storage.index())
        {
        case 0:
            return ArchiveValueKind::String;
        case 1:
            return ArchiveValueKind::UInt64;
        case 2:
            return ArchiveValueKind::Int64;
        case 3:
            return ArchiveValueKind::Double;
        case 4:
            return ArchiveValueKind::Bool;
        default:
            return ArchiveValueKind::Object;
        }
    }
};

struct ArchiveObject
{
    std::unordered_map<std::string, ArchiveValue> fields;
};
} 
