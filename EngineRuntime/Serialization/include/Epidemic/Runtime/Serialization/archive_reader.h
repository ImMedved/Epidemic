#pragma once

#include "Epidemic/Foundation/result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace epidemic::runtime
{
class IArchiveReader
{
  public:
    virtual ~IArchiveReader() = default;

    [[nodiscard]] virtual foundation::Result<void> BeginObject(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndObject() = 0;
    [[nodiscard]] virtual foundation::Result<std::string> ReadString(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<std::uint64_t> ReadUInt64(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<std::int64_t> ReadInt64(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<double> ReadDouble(std::string_view name) const = 0;
    [[nodiscard]] virtual foundation::Result<bool> ReadBool(std::string_view name) const = 0;
};
} // namespace epidemic::runtime
