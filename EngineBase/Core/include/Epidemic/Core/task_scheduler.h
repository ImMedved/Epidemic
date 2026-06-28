#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <vector>

namespace epidemic::core::tasks
{
class ITaskScheduler
{
  public:
    using Task = std::function<void()>;

    virtual ~ITaskScheduler() = default;

    virtual void Schedule(Task task) = 0;
    virtual void WaitIdle() = 0;
};

class SimpleTaskScheduler final : public ITaskScheduler
{
  public:
    explicit SimpleTaskScheduler(std::size_t worker_count = 1);
    ~SimpleTaskScheduler() override;

    void Schedule(Task task) override;
    void WaitIdle() override;

  private:
    void WorkerLoop(std::stop_token stop_token);
    void Shutdown();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::queue<Task> tasks_;
    std::vector<std::jthread> workers_;
    std::size_t active_tasks_{0};
    std::exception_ptr first_exception_;
    bool stopping_{false};
};
} // namespace epidemic::core::tasks