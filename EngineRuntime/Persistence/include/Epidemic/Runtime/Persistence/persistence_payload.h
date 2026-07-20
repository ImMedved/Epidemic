#pragma once

#include "Epidemic/Foundation/string_id.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace epidemic::runtime
{
struct PersistencePayload
{
    foundation::StringId schema_id{};
    std::uint32_t schema_version = 0;
    std::vector<std::byte> bytes;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return schema_id.IsValid() && schema_version != 0 && !bytes.empty();
    }
};
} // namespace epidemic::runtime
