#pragma once

#include <string>
#include <string_view>

namespace epidemic::foundation
{
struct Error
{
    std::string code;
    std::string message;

    // Builds a lightweight error object with a stable machine-readable code.
    [[nodiscard]] static Error Create(std::string_view error_code, std::string_view error_message)
    {
        return Error{std::string(error_code), std::string(error_message)};
    }

    // Convenience helper used by tests and callers that branch on error kind.
    [[nodiscard]] bool HasCode(std::string_view expected_code) const noexcept
    {
        return code == expected_code;
    }
};
} // namespace epidemic::foundation
