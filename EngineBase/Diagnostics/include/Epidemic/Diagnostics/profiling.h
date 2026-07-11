#pragma once

#include <Epidemic/Diagnostics/thread_context.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace epidemic::diagnostics
{
// This file defines the minimal profiling primitives used by EngineBase.
// Profiling is scope-based: producers emit completed duration events, while collectors decide
// whether to store them, aggregate them, or ignore them.

// Represents one completed profiling scope sample.
struct ProfileEvent
{
    std::string name;
    std::chrono::nanoseconds duration{};
    std::thread::id thread_id{std::this_thread::get_id()};
    std::string thread_name;
};

// Abstract sink for profiling events.
class IProfileCollector
{
  public:
    virtual ~IProfileCollector() = default;

    // Accepts one completed scope event.
    virtual void Record(ProfileEvent event) = 0;
};

// Test-friendly collector that stores every event in memory.
class InMemoryProfileCollector final : public IProfileCollector
{
  public:
    // Appends one event to the internal event list.
    void Record(ProfileEvent event) override;

    // Returns a copy of the currently stored profiling events.
    [[nodiscard]] std::vector<ProfileEvent> Snapshot() const;

    // Erases all stored events.
    void Reset();

  private:
    mutable std::mutex mutex_;
    std::vector<ProfileEvent> events_;
};

// Installs the active process-wide collector.
void SetProfileCollector(std::shared_ptr<IProfileCollector> collector);

// Returns the active process-wide collector, if any.
[[nodiscard]] std::shared_ptr<IProfileCollector> GetProfileCollector();

// Globally enables or disables scope emission.
void SetProfilingEnabled(bool enabled) noexcept;

// Returns whether profiling emission is currently enabled.
[[nodiscard]] bool IsProfilingEnabled() noexcept;

// RAII helper that measures the lifetime of a scope and records it on destruction.
class ProfileScope
{
  public:
    // Starts a new named profiling scope.
    explicit ProfileScope(std::string_view scope_name);

    // Finishes the scope and records an event when profiling is enabled and a collector exists.
    ~ProfileScope();

    ProfileScope(const ProfileScope &) = delete;
    ProfileScope &operator=(const ProfileScope &) = delete;

  private:
    std::string name_;
    std::chrono::steady_clock::time_point start_time_{};
    bool enabled_{false};
};
} // namespace epidemic::diagnostics

// Declares a uniquely named stack scope profiler at the current source line.
#define EPIDEMIC_PROFILE_SCOPE(name) ::epidemic::diagnostics::ProfileScope epidemic_profile_scope_##__LINE__(name)