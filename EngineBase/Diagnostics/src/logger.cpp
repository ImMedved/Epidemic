#include <Epidemic/Diagnostics/logger.h>

#include <atomic>

namespace epidemic::diagnostics
{
namespace
{
std::atomic_bool logging_enabled{true};
} // namespace

void SetLoggingEnabled(bool enabled) noexcept
{
    logging_enabled.store(enabled, std::memory_order_relaxed);
}

bool IsLoggingEnabled() noexcept
{
    return logging_enabled.load(std::memory_order_relaxed);
}
} // namespace epidemic::diagnostics
