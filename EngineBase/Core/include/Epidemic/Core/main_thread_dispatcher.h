#pragma once

#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace epidemic::core
{
class IMainThreadDispatcher
{
  public:
    virtual ~IMainThreadDispatcher() = default;

    virtual void Post(std::function<void()> task, std::string debug_name = {}) = 0;
    [[nodiscard]] virtual std::size_t Drain() = 0;
    [[nodiscard]] virtual bool IsMainThread() const noexcept = 0;
};

class MainThreadDispatcher final : public IMainThreadDispatcher
{
  public:
    MainThreadDispatcher();

    void Post(std::function<void()> task, std::string debug_name = {}) override;
    [[nodiscard]] std::size_t Drain() override;
    [[nodiscard]] bool IsMainThread() const noexcept override;

  private:
    struct PendingTask
    {
        std::function<void()> task;
        std::string debug_name;
    };

    std::thread::id owner_thread_id_;
    std::mutex mutex_;
    std::queue<PendingTask> tasks_;
};
} // namespace epidemic::core