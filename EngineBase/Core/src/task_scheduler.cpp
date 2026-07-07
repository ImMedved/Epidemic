#include <Epidemic/Core/task_scheduler.h>

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/Diagnostics/thread_context.h>

#include <stdexcept>
#include <utility>

namespace epidemic::core::tasks
{
struct TaskHandle::State
{
    std::mutex mutex;
    std::condition_variable cv;
    bool completed{false};
    std::exception_ptr exception;
    std::string debug_name;
};

struct TaskGroup::State
{
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t remaining_tasks{0};
    std::exception_ptr first_exception;
};

bool TaskHandle::IsValid() const noexcept
{
    return static_cast<bool>(state_);
}

TaskHandle::TaskHandle(std::shared_ptr<State> state) noexcept : state_(std::move(state))
{
}

TaskGroup::TaskGroup() : state_(std::make_shared<State>())
{
}

TaskGroup::~TaskGroup() = default;

bool TaskGroup::IsValid() const noexcept
{
    return static_cast<bool>(state_);
}

SimpleTaskScheduler::SimpleTaskScheduler(std::size_t worker_count)
{
    if (worker_count == 0)
    {
        worker_count = 1;
    }

    workers_.reserve(worker_count);
    worker_names_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index)
    {
        const auto worker_name = "EpidemicWorker-" + std::to_string(index);
        worker_names_.push_back(worker_name);
        workers_.emplace_back([this, worker_name](std::stop_token stop_token) {
            diagnostics::SetCurrentThreadName(worker_name);
            WorkerLoop(stop_token);
        });
    }

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::WorkerCount, static_cast<std::int64_t>(worker_names_.size()));
}

SimpleTaskScheduler::~SimpleTaskScheduler()
{
    Shutdown();
}

TaskHandle SimpleTaskScheduler::Schedule(Task task, std::string debug_name)
{
    return ScheduleImpl(std::move(task), {}, std::move(debug_name));
}

TaskHandle SimpleTaskScheduler::Schedule(Task task, TaskGroup &group, std::string debug_name)
{
    return ScheduleImpl(std::move(task), group.state_, std::move(debug_name));
}

void SimpleTaskScheduler::Wait(const TaskHandle &handle)
{
    if (!handle.IsValid())
    {
        return;
    }

    std::unique_lock lock(handle.state_->mutex);
    handle.state_->cv.wait(lock, [&handle] { return handle.state_->completed; });
    const auto exception = handle.state_->exception;
    lock.unlock();

    if (exception)
    {
        std::rethrow_exception(exception);
    }
}

void SimpleTaskScheduler::Wait(const TaskGroup &group)
{
    if (!group.IsValid())
    {
        return;
    }

    std::unique_lock lock(group.state_->mutex);
    group.state_->cv.wait(lock, [&group] { return group.state_->remaining_tasks == 0; });
    const auto exception = group.state_->first_exception;
    lock.unlock();

    if (exception)
    {
        std::rethrow_exception(exception);
    }
}

void SimpleTaskScheduler::WaitIdle()
{
    EPIDEMIC_PROFILE_SCOPE("TaskScheduler::WaitIdle");
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });

    const auto first_exception = first_exception_;
    first_exception_ = nullptr;
    lock.unlock();

    if (first_exception)
    {
        std::rethrow_exception(first_exception);
    }
}

void SimpleTaskScheduler::Shutdown()
{
    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
        {
            return;
        }

        stopping_ = true;
    }

    cv_.notify_all();
    workers_.clear();
    idle_cv_.notify_all();
}

std::size_t SimpleTaskScheduler::WorkerCount() const noexcept
{
    std::scoped_lock lock(mutex_);
    return worker_names_.size();
}

TaskDiagnostics SimpleTaskScheduler::GetDiagnostics() const noexcept
{
    std::scoped_lock lock(mutex_);
    return TaskDiagnostics{tasks_.size(), active_tasks_, completed_tasks_, worker_names_.size()};
}

std::vector<std::string> SimpleTaskScheduler::WorkerThreadNames() const
{
    std::scoped_lock lock(mutex_);
    return worker_names_;
}

TaskHandle SimpleTaskScheduler::ScheduleImpl(Task task, std::shared_ptr<TaskGroup::State> group_state, std::string debug_name)
{
    if (!task)
    {
        throw std::invalid_argument("Scheduled task must be valid");
    }

    auto handle_state = std::make_shared<TaskHandle::State>();
    handle_state->debug_name = std::move(debug_name);

    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
        {
            throw std::runtime_error("Task scheduler is shutting down");
        }

        if (group_state)
        {
            std::scoped_lock group_lock(group_state->mutex);
            ++group_state->remaining_tasks;
        }

        tasks_.push(QueuedTask{std::move(task), handle_state, std::move(group_state)});
        diagnostics::GlobalCounters().Increment(diagnostics::CounterId::TasksScheduled);
    }

    cv_.notify_one();
    return TaskHandle(std::move(handle_state));
}

void SimpleTaskScheduler::WorkerLoop(std::stop_token stop_token)
{
    while (true)
    {
        QueuedTask queued_task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this, &stop_token] { return stopping_ || stop_token.stop_requested() || !tasks_.empty(); });

            if ((stopping_ || stop_token.stop_requested()) && tasks_.empty())
            {
                return;
            }

            queued_task = std::move(tasks_.front());
            tasks_.pop();
            ++active_tasks_;
        }

        std::exception_ptr task_exception;
        try
        {
            queued_task.task();
        }
        catch (...)
        {
            task_exception = std::current_exception();
        }

        if (queued_task.handle_state)
        {
            std::scoped_lock handle_lock(queued_task.handle_state->mutex);
            queued_task.handle_state->exception = task_exception;
            queued_task.handle_state->completed = true;
            queued_task.handle_state->cv.notify_all();
        }

        if (queued_task.group_state)
        {
            std::scoped_lock group_lock(queued_task.group_state->mutex);
            if (task_exception && queued_task.group_state->first_exception == nullptr)
            {
                queued_task.group_state->first_exception = task_exception;
            }
            --queued_task.group_state->remaining_tasks;
            queued_task.group_state->cv.notify_all();
        }

        {
            std::scoped_lock lock(mutex_);
            if (task_exception && first_exception_ == nullptr)
            {
                first_exception_ = task_exception;
            }
            --active_tasks_;
            ++completed_tasks_;
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::TasksCompleted);
        }

        idle_cv_.notify_all();
    }
}
} // namespace epidemic::core::tasks
