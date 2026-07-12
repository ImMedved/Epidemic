#include <Epidemic/Diagnostics/thread_context.h>

#include <utility>

namespace epidemic::diagnostics
{
namespace
{
thread_local std::string current_thread_name;
} // namespace

void SetCurrentThreadName(std::string name)
{
    current_thread_name = std::move(name);
}

std::string_view GetCurrentThreadName() noexcept
{
    return current_thread_name;
}
} 
