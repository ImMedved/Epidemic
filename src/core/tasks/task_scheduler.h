#pragma once

#include <condition_variable>
#include <cstddef>
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

    // Queues a task for background execution.
    virtual void Schedule(Task task) = 0;
    // Blocks until the scheduler has no queued or active tasks.
    virtual void WaitIdle() = 0;
};

class SimpleTaskScheduler final : public ITaskScheduler
{
  public:
    // Creates a small worker pool used by the bootstrap runtime.
    explicit SimpleTaskScheduler(std::size_t worker_count = 1);
    ~SimpleTaskScheduler() override;

    void Schedule(Task task) override;
    void WaitIdle() override;

  private:
    // Worker loop that pulls tasks until shutdown is requested.
    void WorkerLoop(std::stop_token stop_token);
    // Stops workers and prevents new task submission.
    void Shutdown();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::queue<Task> tasks_;
    std::vector<std::jthread> workers_;
    std::size_t active_tasks_{0};
    bool stopping_{false};
};
} // namespace epidemic::core::tasks
