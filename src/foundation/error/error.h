#pragma once

#include <string>
#include <string_view>

namespace epidemic::foundation
{
struct Error
{
    std::string code;
    std::string message;

    [[nodiscard]] static Error Create(std::string_view error_code, std::string_view error_message)
    {
        return Error{std::string(error_code), std::string(error_message)};
    }

    [[nodiscard]] bool HasCode(std::string_view expected_code) const noexcept
    {
        return code == expected_code;
    }
};
} // namespace epidemic::foundation
