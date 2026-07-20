#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/archive_value.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace epidemic::runtime
{
class IArchiveReader
{
  public:
    virtual ~IArchiveReader() = default;

    [[nodiscard]] virtual foundation::StringId GetTypeId() const = 0;
    [[nodiscard]] virtual SchemaVersion GetSchemaVersion() const = 0;
    [[nodiscard]] virtual std::uint32_t GetFormatVersion() const = 0;

    [[nodiscard]] virtual foundation::Result<void> BeginObject(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndObject() = 0;
    [[nodiscard]] virtual foundation::Result<std::size_t> BeginArray(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<void> BeginArrayElement(std::size_t index) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndArrayElement() = 0;
    [[nodiscard]] virtual foundation::Result<void> EndArray() = 0;

    [[nodiscard]] virtual foundation::Result<std::string> ReadString(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<std::uint64_t> ReadUInt64(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<std::int64_t> ReadInt64(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<double> ReadDouble(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<bool> ReadBool(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<std::vector<std::byte>> ReadBytes(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<bool> IsNull(std::string_view name) const = 0;
};
} // namespace epidemic::runtime
