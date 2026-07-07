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

    [[nodiscard]] bool IsValid() const noexcept;

  private:
    struct State;

    explicit TaskHandle(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class SimpleTaskScheduler;
};

class TaskGroup
{
  public:
    TaskGroup();
    ~TaskGroup();

    TaskGroup(const TaskGroup &) = default;
    TaskGroup(TaskGroup &&) noexcept = default;
    TaskGroup &operator=(const TaskGroup &) = default;
    TaskGroup &operator=(TaskGroup &&) noexcept = default;

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

    [[nodiscard]] virtual TaskHandle Schedule(Task task, std::string debug_name = {}) = 0;
    [[nodiscard]] virtual TaskHandle Schedule(Task task, TaskGroup &group, std::string debug_name = {}) = 0;
    virtual void Wait(const TaskHandle &handle) = 0;
    virtual void Wait(const TaskGroup &group) = 0;
    virtual void WaitIdle() = 0;
    virtual void Shutdown() = 0;
    [[nodiscard]] virtual std::size_t WorkerCount() const noexcept = 0;
    [[nodiscard]] virtual TaskDiagnostics GetDiagnostics() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> WorkerThreadNames() const = 0;
};

class SimpleTaskScheduler final : public ITaskScheduler
{
  public:
    explicit SimpleTaskScheduler(std::size_t worker_count = 1);
    ~SimpleTaskScheduler() override;

    [[nodiscard]] TaskHandle Schedule(Task task, std::string debug_name = {}) override;
    [[nodiscard]] TaskHandle Schedule(Task task, TaskGroup &group, std::string debug_name = {}) override;
    void Wait(const TaskHandle &handle) override;
    void Wait(const TaskGroup &group) override;
    void WaitIdle() override;
    void Shutdown() override;
    [[nodiscard]] std::size_t WorkerCount() const noexcept override;
    [[nodiscard]] TaskDiagnostics GetDiagnostics() const noexcept override;
    [[nodiscard]] std::vector<std::string> WorkerThreadNames() const override;

  private:
    struct QueuedTask
    {
        Task task;
        std::shared_ptr<TaskHandle::State> handle_state;
        std::shared_ptr<TaskGroup::State> group_state;
    };

    [[nodiscard]] TaskHandle ScheduleImpl(Task task, std::shared_ptr<TaskGroup::State> group_state, std::string debug_name);
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
