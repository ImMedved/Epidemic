#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Runtime/Serialization/archive_value.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace epidemic::runtime
{
class IArchiveWriter
{
  public:
    virtual ~IArchiveWriter() = default;

    [[nodiscard]] virtual foundation::Result<void> BeginObject(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndObject() = 0;
    [[nodiscard]] virtual foundation::Result<void> BeginArray(std::string_view name, std::size_t size) = 0;
    [[nodiscard]] virtual foundation::Result<void> BeginArrayElement(std::size_t index) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndArrayElement() = 0;
    [[nodiscard]] virtual foundation::Result<void> EndArray() = 0;

    [[nodiscard]] virtual foundation::Result<void> WriteString(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteUInt64(std::string_view name, std::uint64_t value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteInt64(std::string_view name, std::int64_t value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteDouble(std::string_view name, double value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteBool(std::string_view name, bool value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteBytes(std::string_view name, std::span<const std::byte> value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteNull(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<SerializedDocument> Finalize(foundation::StringId type_id, SchemaVersion schema_version) = 0;
};
} // namespace epidemic::runtime
