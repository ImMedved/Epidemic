#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace epidemic::core::tasks
{
// This file defines the baseline worker-thread scheduler used by EngineBase.
// The scheduler supports individual task waiting, grouped task waiting, diagnostics snapshots,
// and shutdown-aware queuing without introducing job graphs or task stealing yet.

struct TaskDiagnostics
{
    std::size_t pending_tasks{};
    std::size_t active_tasks{};
    std::size_t completed_tasks{};
    std::size_t worker_count{};
};

class TaskHandle
{
  public:
    TaskHandle() = default;

    // Returns whether this handle references a scheduled task state.
    [[nodiscard]] bool IsValid() const noexcept;

  private:
    struct State;

    // Binds the public handle to the scheduler-owned shared state.
    explicit TaskHandle(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class SimpleTaskScheduler;
};

class TaskGroup
{
  public:
    // Creates an empty task group that can aggregate multiple scheduled tasks.
    TaskGroup();

    // Defaulted because group state is shared and self-managing.
    ~TaskGroup();

    TaskGroup(const TaskGroup &) = default;
    TaskGroup(TaskGroup &&) noexcept = default;
    TaskGroup &operator=(const TaskGroup &) = default;
    TaskGroup &operator=(TaskGroup &&) noexcept = default;

    // Returns whether this group references valid shared state.
    [[nodiscard]] bool IsValid() const noexcept;

  private:
    struct State;

    std::shared_ptr<State> state_;

    friend class SimpleTaskScheduler;
};

class ITaskScheduler
{
  public:
    using Task = std::function<void()>;

    virtual ~ITaskScheduler() = default;

    // Queues a task and returns a handle for direct waiting.
    [[nodiscard]] virtual TaskHandle Schedule(Task task, std::string debug_name = {}) = 0;

    // Queues a task into a group and returns a handle for optional direct waiting.
    [[nodiscard]] virtual TaskHandle Schedule(Task task, TaskGroup &group, std::string debug_name = {}) = 0;

    // Blocks until the specified task finishes and rethrows its exception, if any.
    virtual void Wait(const TaskHandle &handle) = 0;

    // Blocks until every task in the group finishes and rethrows the first captured exception, if any.
    virtual void Wait(const TaskGroup &group) = 0;

    // Blocks until the scheduler has no queued or active tasks.
    virtual void WaitIdle() = 0;

    // Stops accepting new work and requests worker shutdown.
    virtual void Shutdown() = 0;

    // Returns the number of worker threads owned by the scheduler.
    [[nodiscard]] virtual std::size_t WorkerCount() const noexcept = 0;

    // Returns a snapshot of scheduler queue and activity counters.
    [[nodiscard]] virtual TaskDiagnostics GetDiagnostics() const noexcept = 0;

    // Returns the configured worker thread names.
    [[nodiscard]] virtual std::vector<std::string> WorkerThreadNames() const = 0;
};

class SimpleTaskScheduler final : public ITaskScheduler
{
  public:
    // Starts the requested number of worker threads, normalizing zero to one worker.
    explicit SimpleTaskScheduler(std::size_t worker_count = 1);

    // Requests shutdown so worker threads are released with the scheduler.
    ~SimpleTaskScheduler() override;

    // Queues one task outside of any group.
    [[nodiscard]] TaskHandle Schedule(Task task, std::string debug_name = {}) override;

    // Queues one task into a group.
    [[nodiscard]] TaskHandle Schedule(Task task, TaskGroup &group, std::string debug_name = {}) override;

    // Waits for one task and propagates its failure.
    void Wait(const TaskHandle &handle) override;

    // Waits for a task group and propagates the first task failure.
    void Wait(const TaskGroup &group) override;

    // Waits for all currently queued and active tasks and then rethrows the first scheduler-level failure.
    void WaitIdle() override;

    // Prevents new scheduling and joins worker threads.
    void Shutdown() override;

    // Returns the number of worker threads.
    [[nodiscard]] std::size_t WorkerCount() const noexcept override;

    // Returns a snapshot of queue depth and completion counters.
    [[nodiscard]] TaskDiagnostics GetDiagnostics() const noexcept override;

    // Returns the names assigned to worker threads.
    [[nodiscard]] std::vector<std::string> WorkerThreadNames() const override;

  private:
    struct QueuedTask
    {
        Task task;
        std::shared_ptr<TaskHandle::State> handle_state;
        std::shared_ptr<TaskGroup::State> group_state;
    };

    // Shared implementation for grouped and ungrouped scheduling requests.
    [[nodiscard]] TaskHandle ScheduleImpl(Task task, std::shared_ptr<TaskGroup::State> group_state, std::string debug_name);

    // Worker-thread loop that waits for queued tasks, executes them, and updates completion state.
    void WorkerLoop(std::stop_token stop_token);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    std::queue<QueuedTask> tasks_;
    std::vector<std::jthread> workers_;
    std::vector<std::string> worker_names_;
    std::size_t active_tasks_{0};
    std::size_t completed_tasks_{0};
    std::exception_ptr first_exception_;
    bool stopping_{false};
};
} // namespace epidemic::core::tasks