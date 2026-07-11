#include <Epidemic/Diagnostics/profiling.h>

#include <atomic>
#include <utility>

namespace epidemic::diagnostics
{
// This file implements the baseline profiling collector registry and RAII scope timing helper.
// The global collector pointer keeps the instrumentation surface small for early EngineBase stages.

namespace
{
std::mutex collector_mutex;
std::shared_ptr<IProfileCollector> collector;
std::atomic_bool profiling_enabled{true};
} // namespace

// Stores one profiling event in memory.
void InMemoryProfileCollector::Record(ProfileEvent event)
{
    std::scoped_lock lock(mutex_);
    events_.push_back(std::move(event));
}

// Returns a snapshot copy of all recorded profiling events.
std::vector<ProfileEvent> InMemoryProfileCollector::Snapshot() const
{
    std::scoped_lock lock(mutex_);
    return events_;
}

// Removes all recorded profiling events.
void InMemoryProfileCollector::Reset()
{
    std::scoped_lock lock(mutex_);
    events_.clear();
}

// Installs or clears the active process-wide profile collector.
void SetProfileCollector(std::shared_ptr<IProfileCollector> new_collector)
{
    std::scoped_lock lock(collector_mutex);
    collector = std::move(new_collector);
}

// Returns the currently installed process-wide profile collector.
std::shared_ptr<IProfileCollector> GetProfileCollector()
{
    std::scoped_lock lock(collector_mutex);
    return collector;
}

// Enables or disables profiling globally.
void SetProfilingEnabled(bool enabled) noexcept
{
    profiling_enabled.store(enabled, std::memory_order_relaxed);
}

// Returns whether profiling is globally enabled.
bool IsProfilingEnabled() noexcept
{
    return profiling_enabled.load(std::memory_order_relaxed);
}

// Starts timing a named scope when profiling is enabled and a collector exists.
ProfileScope::ProfileScope(std::string_view scope_name) : name_(scope_name)
{
    enabled_ = IsProfilingEnabled() && static_cast<bool>(GetProfileCollector());
    if (enabled_)
    {
        start_time_ = std::chrono::steady_clock::now();
    }
}

// Finishes the scope and records a completed profiling event if collection is still possible.
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