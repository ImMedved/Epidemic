#pragma once

#include "Epidemic/Foundation/error.h"

#include <string_view>

namespace epidemic::runtime
{
[[nodiscard]] inline foundation::Error CreateSerializationError(
    std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Error::Create(code, message, context);
}
} // namespace epidemic::runtime
