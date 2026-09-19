#include <Epidemic/Diagnostics/thread_context.h>

namespace epidemic::diagnostics
{
// This file implements thread-local naming used by the logging and profiling layers.
// The state is purely in-process and does not attempt to synchronize with OS thread names.

namespace
{
thread_local std::string current_thread_name;
} // namespace

// Replaces the current thread's stored display name.
void SetCurrentThreadName(std::string_view name) noexcept
{
    try
    {
        current_thread_name.assign(name);
    }
    catch (...)
    {
        // Thread names are observational diagnostics and must never terminate worker entry.
    }
}

// Returns the current thread's stored display name.
std::string GetCurrentThreadName()
{
    return current_thread_name;
}
} 
