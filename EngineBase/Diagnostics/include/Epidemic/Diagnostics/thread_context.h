#pragma once

#include <string>
#include <string_view>

namespace epidemic::diagnostics
{
void SetCurrentThreadName(std::string name);
[[nodiscard]] std::string_view GetCurrentThreadName() noexcept;
} // namespace epidemic::diagnostics
