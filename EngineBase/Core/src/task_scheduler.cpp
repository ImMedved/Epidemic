#include <Epidemic/Core/task_scheduler.h>

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/Diagnostics/thread_context.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace epidemic::core::tasks
{
// This file implements the baseline worker-thread scheduler.
// It captures completion and failure state for individual handles, task groups, and scheduler-wide idle waits.

// Shared completion state for one scheduled task.
struct TaskHandle::State
{
    std::mutex mutex;
    std::condition_variable cv;
    bool completed{false};
    std::exception_ptr exception;
    std::string debug_name;
};

// Shared aggregation state for a set of related scheduled tasks.
struct TaskGroup::State
{
    std::mutex mutex;
    std::condition_variable cv;
    std::size_t remaining_tasks{0};
    std::exception_ptr first_exception;
};

// Returns whether this handle references live scheduler state.
bool TaskHandle::IsValid() const noexcept
{
    return static_cast<bool>(state_);
}

// Stores the shared state backing the public handle.
TaskHandle::TaskHandle(std::shared_ptr<State> state) noexcept : state_(std::move(state))
{
}

// Creates a new empty aggregation state for grouped scheduling.
TaskGroup::TaskGroup() : state_(std::make_shared<State>())
{
}

// Defaulted because group state is reference-counted.
TaskGroup::~TaskGroup() = default;

// Returns whether this group references live aggregation state.
bool TaskGroup::IsValid() const noexcept
{
    return static_cast<bool>(state_);
}

// Starts worker threads and assigns stable diagnostic names.
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
        worker_thread_ids_.push_back(workers_.back().get_id());
    }

    diagnostics::GlobalCounters().Set(diagnostics::CounterId::WorkerCount, static_cast<std::int64_t>(worker_names_.size()));
}

// Requests shutdown during destruction so owned workers are released.
SimpleTaskScheduler::~SimpleTaskScheduler()
{
    RequestStop();
    if (!IsWorkerThread(std::this_thread::get_id()))
    {
        try
        {
            Join();
        }
        catch (...)
        {
        }
    }
}

// Queues an ungrouped task.
TaskHandle SimpleTaskScheduler::Schedule(Task task, std::string debug_name)
{
    return ScheduleImpl(std::move(task), {}, std::move(debug_name));
}

// Queues a task and associates it with a task group.
TaskHandle SimpleTaskScheduler::Schedule(Task task, TaskGroup &group, std::string debug_name)
{
    return ScheduleImpl(std::move(task), group.state_, std::move(debug_name));
}

// Blocks until the task completes and then rethrows its failure, if any.
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

// Blocks until the group becomes empty and then rethrows the first task failure, if any.
void SimpleTaskScheduler::Wait(const TaskGroup &group)
{
    if (!group.IsValid())
    {
        return;
    }

    std::unique_lock lock(group.state_->mutex);
    group.state_->cv.wait(lock, [&group] { return group.state_->remaining_tasks == 0; });
    const auto exception = std::exchange(group.state_->first_exception, nullptr);
    lock.unlock();

    if (exception)
    {
        std::rethrow_exception(exception);
    }
}

// Blocks until the scheduler has no queued or active tasks and then rethrows the first scheduler-level failure.
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

// Requests worker shutdown without joining any thread. Safe from worker callbacks.
void SimpleTaskScheduler::RequestStop() noexcept
{
    try
    {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
            for (auto &worker : workers_)
            {
                worker.request_stop();
            }
        }

        cv_.notify_all();
        idle_cv_.notify_all();
    }
    catch (...)
    {
    }
}

// Joins worker threads after stop has been requested. Calling from a worker is a controlled error.
void SimpleTaskScheduler::Join()
{
    std::vector<std::jthread> workers_to_join;
    {
        std::scoped_lock lock(mutex_);
        if (IsWorkerThread(std::this_thread::get_id()))
        {
            throw std::runtime_error("Task scheduler Join/Shutdown cannot be called from a scheduler worker thread");
        }

        if (workers_.empty())
        {
            return;
        }

        stopping_ = true;
        for (auto &worker : workers_)
        {
            worker.request_stop();
        }
        workers_to_join = std::move(workers_);
        worker_thread_ids_.clear();
    }

    cv_.notify_all();
    workers_to_join.clear();
    idle_cv_.notify_all();
}

// Stops accepting new work, wakes workers, and joins the worker thread list.
void SimpleTaskScheduler::Shutdown()
{
    RequestStop();
    Join();
}

// Returns the number of worker threads owned by the scheduler.
std::size_t SimpleTaskScheduler::WorkerCount() const noexcept
{
    std::scoped_lock lock(mutex_);
    return worker_names_.size();
}

// Returns a snapshot of pending, active, completed, and worker counts.
TaskDiagnostics SimpleTaskScheduler::GetDiagnostics() const noexcept
{
    std::scoped_lock lock(mutex_);
    return TaskDiagnostics{tasks_.size(), active_tasks_, completed_tasks_, worker_names_.size()};
}

// Returns the worker names assigned during construction.
std::vector<std::string> SimpleTaskScheduler::WorkerThreadNames() const
{
    std::scoped_lock lock(mutex_);
    return worker_names_;
}

// Shared implementation for grouped and ungrouped scheduling requests.
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

        bool group_task_reserved = false;
        if (group_state)
        {
            std::scoped_lock group_lock(group_state->mutex);
            ++group_state->remaining_tasks;
            group_task_reserved = true;
        }

        try
        {
            tasks_.push(QueuedTask{std::move(task), handle_state, group_state});
            diagnostics::GlobalCounters().Increment(diagnostics::CounterId::TasksScheduled);
        }
        catch (...)
        {
            if (group_state && group_task_reserved)
            {
                std::scoped_lock group_lock(group_state->mutex);
                --group_state->remaining_tasks;
                group_state->cv.notify_all();
            }
            throw;
        }
    }

    cv_.notify_one();
    return TaskHandle(std::move(handle_state));
}

// Waits for work, executes tasks, records failures, and updates handle/group completion state.
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
// Returns whether a thread id belongs to one of the scheduler-owned workers.
bool SimpleTaskScheduler::IsWorkerThread(std::thread::id thread_id) const noexcept
{
    return std::find(worker_thread_ids_.begin(), worker_thread_ids_.end(), thread_id) != worker_thread_ids_.end();
}
} // namespace epidemic::core::tasks
