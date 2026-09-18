#pragma once

#include <string>

namespace epidemic::diagnostics
{
// Stores a human-readable name for the current thread. The name is thread-local and may be empty.
void SetCurrentThreadName(std::string name);

// Returns an owning copy of the current thread name. The copy remains valid after a later rename
// and after the originating thread-local string changes storage.
[[nodiscard]] std::string GetCurrentThreadName();
} // namespace epidemic::diagnostics
