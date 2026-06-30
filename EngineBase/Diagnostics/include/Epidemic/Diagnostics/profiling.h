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
struct ProfileEvent
{
    std::string name;
    std::chrono::nanoseconds duration{};
    std::thread::id thread_id{std::this_thread::get_id()};
    std::string thread_name;
};

class IProfileCollector
{
  public:
    virtual ~IProfileCollector() = default;

    virtual void Record(ProfileEvent event) = 0;
};

class InMemoryProfileCollector final : public IProfileCollector
{
  public:
    void Record(ProfileEvent event) override;
    [[nodiscard]] std::vector<ProfileEvent> Snapshot() const;
    void Reset();

  private:
    mutable std::mutex mutex_;
    std::vector<ProfileEvent> events_;
};

void SetProfileCollector(std::shared_ptr<IProfileCollector> collector);
[[nodiscard]] std::shared_ptr<IProfileCollector> GetProfileCollector();
void SetProfilingEnabled(bool enabled) noexcept;
[[nodiscard]] bool IsProfilingEnabled() noexcept;

class ProfileScope
{
  public:
    explicit ProfileScope(std::string_view scope_name);
    ~ProfileScope();

    ProfileScope(const ProfileScope &) = delete;
    ProfileScope &operator=(const ProfileScope &) = delete;

  private:
    std::string name_;
    std::chrono::steady_clock::time_point start_time_{};
    bool enabled_{false};
};
} // namespace epidemic::diagnostics

#define EPIDEMIC_PROFILE_SCOPE(name) ::epidemic::diagnostics::ProfileScope epidemic_profile_scope_##__LINE__(name)
