#include <Epidemic/Core/main_thread_dispatcher.h>

#include <Epidemic/Diagnostics/counters.h>

#include <stdexcept>
#include <utility>

namespace epidemic::core
{
MainThreadDispatcher::MainThreadDispatcher() : owner_thread_id_(std::this_thread::get_id())
{
}

void MainThreadDispatcher::Post(std::function<void()> task, std::string debug_name)
{
    if (!task)
    {
        throw std::invalid_argument("Main-thread task must not be empty");
    }

    std::scoped_lock lock(mutex_);
    tasks_.push(PendingTask{std::move(task), std::move(debug_name)});
}

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

bool MainThreadDispatcher::IsMainThread() const noexcept
{
    return std::this_thread::get_id() == owner_thread_id_;
}
} // namespace epidemic::core