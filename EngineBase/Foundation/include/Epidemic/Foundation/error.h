#pragma once

#include <string>
#include <string_view>

namespace epidemic::foundation
{
struct Error
{
    std::string code;
    std::string message;
    std::string context;

    [[nodiscard]] static Error Create(std::string_view error_code, std::string_view error_message, std::string_view error_context = {})
    {
        return Error{std::string(error_code), std::string(error_message), std::string(error_context)};
    }

    [[nodiscard]] bool HasCode(std::string_view expected_code) const noexcept
    {
        return code == expected_code;
    }

    [[nodiscard]] bool HasMessage() const noexcept
    {
        return !message.empty();
    }

    [[nodiscard]] bool HasContext() const noexcept
    {
        return !context.empty();
    }
};
} // namespace epidemic::foundation