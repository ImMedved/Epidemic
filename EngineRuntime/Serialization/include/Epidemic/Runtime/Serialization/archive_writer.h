#pragma once

#include "Epidemic/Foundation/result.h"

#include <cstdint>
#include <string_view>

namespace epidemic::runtime
{
class IArchiveWriter
{
  public:
    virtual ~IArchiveWriter() = default;

    [[nodiscard]] virtual foundation::Result<void> BeginObject(std::string_view name) = 0;
    [[nodiscard]] virtual foundation::Result<void> EndObject() = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteString(std::string_view name, std::string_view value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteUInt64(std::string_view name, std::uint64_t value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteInt64(std::string_view name, std::int64_t value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteDouble(std::string_view name, double value) = 0;
    [[nodiscard]] virtual foundation::Result<void> WriteBool(std::string_view name, bool value) = 0;
};
} // namespace epidemic::runtime
