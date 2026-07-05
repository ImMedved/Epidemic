#pragma once

#include <Epidemic/Core/frame_context.h>
#include <Epidemic/Core/frame_phase.h>
#include <Epidemic/Core/module_registry.h>
#include <Epidemic/Core/service_container.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace epidemic::diagnostics
{
class ILogger;
}

namespace epidemic::core
{
struct ApplicationOptions
{
    std::string application_name = "Epidemic Engine v1.0";
    std::optional<std::uint64_t> frame_limit;
};

class Application
{
  public:
    using MainThreadTask = std::function<void()>;
    using FramePhaseCallback = std::function<void(const FrameContext &)>;

    explicit Application(ApplicationOptions options = {});
    ~Application();

    [[nodiscard]] ModuleRegistry &Modules() noexcept;
    [[nodiscard]] ServiceContainer &Services() noexcept;

    int Bootstrap();
    int Initialize();
    int Tick();
    int Run();
    int Shutdown();

    void RequestStop() noexcept;
    void ResetStopRequest() noexcept;
    [[nodiscard]] bool StopRequested() const noexcept;
    void SetFrameLimit(std::optional<std::uint64_t> frame_limit) noexcept;
    [[nodiscard]] std::optional<std::uint64_t> FrameLimit() const noexcept;
    void AddFramePhaseHandler(FramePhase phase, FramePhaseCallback callback, std::string debug_name = {});
    void ScheduleMainThreadTask(MainThreadTask task, std::string debug_name = {});
    [[nodiscard]] const FrameContext &CurrentFrameContext() const noexcept;

  private:
    struct PhaseHandler
    {
        FramePhaseCallback callback;
        std::string debug_name;
    };

    struct ScheduledMainThreadTask
    {
        MainThreadTask task;
        std::string debug_name;
    };

    enum class State
    {
        Constructed,
        Bootstrapped,
        Initialized,
        Running,
        Failed,
        ShutDown,
    };

    void ValidateCoreServices() const;
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;
    void ExecuteFrame();
    void ExecutePhase(FramePhase phase, const FrameContext &frame_context, diagnostics::ILogger &logger);
    void ExecuteRegisteredPhaseHandlers(FramePhase phase, const FrameContext &frame_context);
    [[nodiscard]] FrameContext BuildNextFrameContext();
    [[nodiscard]] bool ReachedFrameLimit() const noexcept;
    std::size_t RunScheduledMainThreadTasks();

    ApplicationOptions options_;
    ServiceContainer services_;
    ModuleRegistry modules_;
    State state_{State::Constructed};
    std::array<std::vector<PhaseHandler>, FramePhaseCount()> phase_handlers_{};
    mutable std::mutex main_thread_tasks_mutex_;
    std::queue<ScheduledMainThreadTask> main_thread_tasks_;
    FrameContext current_frame_context_{};
    foundation::TimePoint run_start_time_{};
    foundation::TimePoint previous_frame_time_{};
    std::uint64_t executed_frame_count_{0};
    bool frame_clock_initialized_{false};
    std::atomic<bool> stop_requested_{false};
};
} // namespace epidemic::core
