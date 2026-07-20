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
#include <optional>
#include <string>
#include <vector>

namespace epidemic::diagnostics
{
class ILogger;
}

namespace epidemic::core
{
// This file declares the central EngineBase application shell.
// Application owns the service container, module registry, frame loop orchestration,
// stop request state, and the phase hooks used by support wiring.

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

    // Creates an application shell with optional name and frame-limit defaults.
    explicit Application(ApplicationOptions options = {});

    // Attempts a best-effort shutdown if the caller forgot to do it explicitly.
    ~Application();

    // Returns mutable access to the module registry used during composition and lifecycle.
    [[nodiscard]] ModuleRegistry &Modules() noexcept;

    // Returns mutable access to the service container used during composition and bootstrap.
    [[nodiscard]] ServiceContainer &Services() noexcept;

    // Validates required core services, fills default configuration, and bootstraps all modules.
    int Bootstrap();

    // Initializes all modules and seals the service container against further registrations.
    int Initialize();

    // Executes exactly one frame while the application is in the initialized state.
    int Tick();

    // Repeatedly executes frames until stop is requested or the frame limit is reached.
    int Run();

    // Shuts down modules and drains scheduler work.
    int Shutdown();

    // Requests that Run() stop at the next loop boundary.
    void RequestStop() noexcept;

    // Clears a previously requested stop flag.
    void ResetStopRequest() noexcept;

    // Returns whether the application has been asked to stop.
    [[nodiscard]] bool StopRequested() const noexcept;

    // Overrides the active frame limit used by Run().
    void SetFrameLimit(std::optional<std::uint64_t> frame_limit) noexcept;

    // Returns the currently configured frame limit, if any.
    [[nodiscard]] std::optional<std::uint64_t> FrameLimit() const noexcept;

    // Registers a callback for one frame phase.
    // Relationship: support wiring uses this to plug platform, input, and RHI hooks into the frame loop.
    void AddFramePhaseHandler(FramePhase phase, FramePhaseCallback callback, std::string debug_name = {});

    // Queues a task onto the main-thread dispatcher service.
    void ScheduleMainThreadTask(MainThreadTask task, std::string debug_name = {});

    // Returns the context for the frame that is currently executing or most recently executed.
    [[nodiscard]] const FrameContext &CurrentFrameContext() const noexcept;

  private:
    struct PhaseHandler
    {
        FramePhaseCallback callback;
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

    // Verifies that the minimal core services required by Application are registered.
    void ValidateCoreServices() const;

    // Returns the registered logger service.
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;

    // Executes one full frame including all configured phases.
    void ExecuteFrame();

    // Executes one specific frame phase and updates related diagnostics counters.
    void ExecutePhase(FramePhase phase, const FrameContext &frame_context, diagnostics::ILogger &logger);

    // Invokes handlers registered through AddFramePhaseHandler for the specified phase.
    void ExecuteRegisteredPhaseHandlers(FramePhase phase, const FrameContext &frame_context);

    // Builds the frame context for the next frame from runtime clocks and counters.
    [[nodiscard]] FrameContext BuildNextFrameContext();

    // Returns true when the configured frame limit has already been reached.
    [[nodiscard]] bool ReachedFrameLimit() const noexcept;

    // Drains and executes main-thread tasks through the dispatcher service.
    [[nodiscard]] std::size_t RunScheduledMainThreadTasks();

    ApplicationOptions options_;
    ServiceContainer services_;
    ModuleRegistry modules_;
    State state_{State::Constructed};
    std::array<std::vector<PhaseHandler>, FramePhaseCount()> phase_handlers_{};
    FrameContext current_frame_context_{};
    foundation::TimePoint run_start_time_{};
    foundation::TimePoint previous_frame_time_{};
    std::uint64_t executed_frame_count_{0};
    bool frame_clock_initialized_{false};
    std::atomic<bool> stop_requested_{false};
};
} // namespace epidemic::core