#include <Epidemic/Core/main_thread_dispatcher.h>

#include <Epidemic/Diagnostics/counters.h>

#include <stdexcept>
#include <utility>

namespace epidemic::core
{
// This file implements the baseline main-thread task queue.
// The dispatcher captures an owner thread at construction and enforces that Drain runs only there.

// Captures the constructing thread as the owner.
MainThreadDispatcher::MainThreadDispatcher() : owner_thread_id_(std::this_thread::get_id())
{
}

// Queues a task for later execution on the owner thread.
void MainThreadDispatcher::Post(std::function<void()> task, std::string debug_name)
{
    if (!task)
    {
        throw std::invalid_argument("Main-thread task must not be empty");
    }

    std::scoped_lock lock(mutex_);
    tasks_.push(PendingTask{std::move(task), std::move(debug_name)});
}

// Executes queued tasks in FIFO order on the owner thread and updates diagnostics counters.
std::size_t MainThreadDispatcher::Drain()
{
    if (!IsMainThread())
    {
        throw std::runtime_error("MainThreadDispatcher::Drain must execute on the owning main thread");
    }

    std::queue<PendingTask> pending_tasks;
    {
        std::scoped_lock lock(mutex_);
        std::swap(pending_tasks, tasks_);
    }

    std::size_t executed_tasks = 0;
    try
    {
        while (!pending_tasks.empty())
        {
            auto task = std::move(pending_tasks.front());
            pending_tasks.pop();
            task.task();
            ++executed_tasks;
        }
    }
    catch (...)
    {
        epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::MainThreadTasksExecuted,
                                                    static_cast<std::int64_t>(executed_tasks));
        throw;
    }

    epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::MainThreadTasksExecuted,
                                                static_cast<std::int64_t>(executed_tasks));
    return executed_tasks;
}

// Returns whether the caller is the owning thread.
bool MainThreadDispatcher::IsMainThread() const noexcept
{
    return std::this_thread::get_id() == owner_thread_id_;
}
} // namespace epidemic::core