#pragma once

#include <string>
#include <string_view>

namespace epidemic::diagnostics
{
// This file exposes the minimal per-thread naming helpers used by logs and profiling.
// The implementation is thread-local and intentionally independent from OS-level thread naming APIs.

// Stores a human-readable name for the current thread.
void SetCurrentThreadName(std::string name);

// Returns the current thread's stored name, or an empty view when none was assigned.
[[nodiscard]] std::string_view GetCurrentThreadName() noexcept;
} // namespace epidemic::diagnostics