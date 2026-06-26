#include "core/tasks/task_scheduler.h"

#include <stdexcept>
#include <utility>

namespace epidemic::core::tasks
{
SimpleTaskScheduler::SimpleTaskScheduler(std::size_t worker_count)
{
    if (worker_count == 0)
    {
        worker_count = 1;
    }

    workers_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index)
    {
        workers_.emplace_back([this](std::stop_token stop_token) { WorkerLoop(stop_token); });
    }
}

SimpleTaskScheduler::~SimpleTaskScheduler()
{
    Shutdown();
}

void SimpleTaskScheduler::Schedule(Task task)
{
    if (!task)
    {
        throw std::invalid_argument("Scheduled task must be valid");
    }

    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
        {
            throw std::runtime_error("Task scheduler is shutting down");
        }
        tasks_.push(std::move(task));
    }

    cv_.notify_one();
}

void SimpleTaskScheduler::WaitIdle()
{
    std::unique_lock lock(mutex_);
    idle_cv_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
}

void SimpleTaskScheduler::WorkerLoop(std::stop_token stop_token)
{
    while (true)
    {
        Task task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this, &stop_token] { return stopping_ || stop_token.stop_requested() || !tasks_.empty(); });

            if ((stopping_ || stop_token.stop_requested()) && tasks_.empty())
            {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
            ++active_tasks_;
        }

        try
        {
            task();
        }
        catch (...)
        {
        }

        {
            std::scoped_lock lock(mutex_);
            --active_tasks_;
        }

        idle_cv_.notify_all();
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
} // namespace epidemic::core::tasks
