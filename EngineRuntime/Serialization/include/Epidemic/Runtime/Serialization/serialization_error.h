#pragma once

#include "Epidemic/Foundation/error.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

#include <string_view>

namespace epidemic::runtime
{
[[nodiscard]] inline foundation::Error CreateSerializationError(
    std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Error::Create(code, message, context);
}
} 
