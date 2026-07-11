#pragma once

#include <string>
#include <string_view>

namespace epidemic::foundation
{
// This file defines the smallest shared error payload used across EngineBase.
// Error is intentionally a plain value type so functions can return it through Result<T>
// without coupling callers to exceptions or subsystem-specific status objects.

// Describes an expected runtime failure in EngineBase.
// Fields:
// - code: stable machine-readable identifier for branching and diagnostics
// - message: human-readable description suitable for logs or UI
// - context: optional extra details such as subsystem or operation name
struct Error
{
    std::string code;
    std::string message;
    std::string context;

    // Builds an Error from borrowed string-like inputs.
    // Input: code/message/context views.
    // Output: a fully owned Error instance.
    // Relationship: this is the main convenience constructor used by Result-producing code.
    [[nodiscard]] static Error Create(std::string_view error_code, std::string_view error_message, std::string_view error_context = {})
    {
        return Error{std::string(error_code), std::string(error_message), std::string(error_context)};
    }

    // Checks whether the stored code matches the requested code exactly.
    // Input: expected code string.
    // Output: true when the codes are equal.
    [[nodiscard]] bool HasCode(std::string_view expected_code) const noexcept
    {
        return code == expected_code;
    }

    // Reports whether the error contains a non-empty message.
    [[nodiscard]] bool HasMessage() const noexcept
    {
        return !message.empty();
    }

    // Reports whether the error contains extra contextual details.
    [[nodiscard]] bool HasContext() const noexcept
    {
        return !context.empty();
    }
};
} // namespace epidemic::foundation