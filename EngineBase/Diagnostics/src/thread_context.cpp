#include <Epidemic/Diagnostics/thread_context.h>

#include <utility>

namespace epidemic::diagnostics
{
// This file implements thread-local naming used by the logging and profiling layers.
// The state is purely in-process and does not attempt to synchronize with OS thread names.

namespace
{
thread_local std::string current_thread_name;
} // namespace

// Replaces the current thread's stored display name.
void SetCurrentThreadName(std::string name)
{
    current_thread_name = std::move(name);
}

// Returns the current thread's stored display name.
std::string_view GetCurrentThreadName() noexcept
{
    return current_thread_name;
}
} // namespace epidemic::diagnostics