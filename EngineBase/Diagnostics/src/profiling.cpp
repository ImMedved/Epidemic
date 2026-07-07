#include <Epidemic/Diagnostics/profiling.h>

#include <atomic>
#include <utility>

namespace epidemic::diagnostics
{
namespace
{
std::mutex collector_mutex;
std::shared_ptr<IProfileCollector> collector;
std::atomic_bool profiling_enabled{true};
} // namespace

void InMemoryProfileCollector::Record(ProfileEvent event)
{
    std::scoped_lock lock(mutex_);
    events_.push_back(std::move(event));
}

std::vector<ProfileEvent> InMemoryProfileCollector::Snapshot() const
{
    std::scoped_lock lock(mutex_);
    return events_;
}

void InMemoryProfileCollector::Reset()
{
    std::scoped_lock lock(mutex_);
    events_.clear();
}

void SetProfileCollector(std::shared_ptr<IProfileCollector> new_collector)
{
    std::scoped_lock lock(collector_mutex);
    collector = std::move(new_collector);
}

std::shared_ptr<IProfileCollector> GetProfileCollector()
{
    std::scoped_lock lock(collector_mutex);
    return collector;
}

void SetProfilingEnabled(bool enabled) noexcept
{
    profiling_enabled.store(enabled, std::memory_order_relaxed);
}

bool IsProfilingEnabled() noexcept
{
    return profiling_enabled.load(std::memory_order_relaxed);
}

ProfileScope::ProfileScope(std::string_view scope_name) : name_(scope_name)
{
    enabled_ = IsProfilingEnabled() && static_cast<bool>(GetProfileCollector());
    if (enabled_)
    {
        start_time_ = std::chrono::steady_clock::now();
    }
}

ProfileScope::~ProfileScope()
{
    if (!enabled_)
    {
        return;
    }

    auto current_collector = GetProfileCollector();
    if (!current_collector)
    {
        return;
    }

    current_collector->Record(ProfileEvent{name_, std::chrono::steady_clock::now() - start_time_, std::this_thread::get_id(),
                                           std::string(GetCurrentThreadName())});
}
} // namespace epidemic::diagnostics
