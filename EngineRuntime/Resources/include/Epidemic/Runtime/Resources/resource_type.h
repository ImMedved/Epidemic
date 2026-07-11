#pragma once

#include "Epidemic/Foundation/string_id.h"

namespace epidemic::runtime
{
struct ResourceType
{
    foundation::StringId value{};

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }

    [[nodiscard]] constexpr bool operator==(const ResourceType&) const noexcept = default;
};
} // namespace epidemic::runtime
