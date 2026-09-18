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

// Этот файл объявляет центральную оболочку приложения EngineBase.
// Приложение владеет контейнером сервисов, реестром модулей, оркестрацией цикла кадра,
// состоянием запроса остановки и хуками фаз, используемыми вспомогательной обвязкой.

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

    struct FramePhaseHandlerRegistration
    {
        FramePhase phase{FramePhase::BeginFrame};
        FramePhaseCallback callback;
        std::string debug_name;
    };

    // Creates an application shell with optional name and frame-limit defaults.
    // Создает оболочку приложения с необязательным именем и лимитом кадров по умолчанию.
    explicit Application(ApplicationOptions options = {});

    // Attempts a best-effort shutdown if the caller forgot to do it explicitly.
    // Пытается корректно завершить работу, если вызывающий код забыл сделать это явно.
    ~Application();

    // Returns mutable access to the module registry used during composition and lifecycle.
    // Возвращает изменяемый доступ к реестру модулей, используемому при компоновке и в жизненном цикле.
    [[nodiscard]] ModuleRegistry &Modules() noexcept;

    // Returns mutable access to the service container used during composition and bootstrap.
    // Возвращает изменяемый доступ к контейнеру сервисов, используемому при компоновке и начальной настройке.
    [[nodiscard]] ServiceContainer &Services() noexcept;

    // Validates required core services, fills default configuration, and bootstraps all modules.
    // Проверяет требуемые сервисы ядра, заполняет конфигурацию по умолчанию и подготавливает все модули.
    int Bootstrap();

    // Initializes all modules and seals the service container against further registrations.
    // Инициализирует все модули и закрывает контейнер сервисов для дальнейших регистраций.
    int Initialize();

    // Executes exactly one frame while the application is in the initialized state.
    // Выполняет ровно один кадр, пока приложение находится в инициализированном состоянии.
    int Tick();

    // Repeatedly executes frames until stop is requested or the frame limit is reached.
    // Повторяет выполнение кадров, пока не запрошена остановка или не достигнут лимит кадров.
    int Run();

    // Shuts down modules and drains scheduler work.
    // Завершает работу модулей и опустошает очередь задач планировщика.
    int Shutdown();

    // Requests that Run() stop at the next loop boundary.
    // Запрашивает остановку Run() на следующей границе цикла.
    void RequestStop() noexcept;

    // Clears a previously requested stop flag.
    // Убирает ранее запрошенный флаг остановки.
    void ResetStopRequest() noexcept;

    // Returns whether the application has been asked to stop.
    // Возвращает true, если для приложения была запрошена остановка.
    [[nodiscard]] bool StopRequested() const noexcept;

    // Overrides the active frame limit used by Run().
    // Переопределяет текущий лимит кадров, используемый Run().
    void SetFrameLimit(std::optional<std::uint64_t> frame_limit) noexcept;

    // Returns the currently configured frame limit, if any.
    // Возвращает текущий лимит кадров, если он задан.
    [[nodiscard]] std::optional<std::uint64_t> FrameLimit() const noexcept;

    // Registers a callback for one frame phase.
    // Relationship: support wiring uses this to plug platform, input, and RHI hooks into the frame loop.
    // Регистрирует колбэк для одной фазы кадра.
    // Связь: вспомогательная обвязка использует это для подключения хуков платформы, ввода и RHI к циклу кадра.
    void AddFramePhaseHandler(FramePhase phase, FramePhaseCallback callback, std::string debug_name = {});

    // Registers a batch of frame handlers atomically. All allocation/copy work happens on a candidate
    // handler table, and the live table is updated only after the complete batch is valid and built.
    void AddFramePhaseHandlersAtomic(std::vector<FramePhaseHandlerRegistration> registrations);

    // Queues a task onto the main-thread dispatcher service.
    // Добавляет задачу в очередь диспетчера главного потока.
    void ScheduleMainThreadTask(MainThreadTask task, std::string debug_name = {});

    // Returns the context for the frame that is currently executing or most recently executed.
    // Возвращает контекст кадра, который сейчас выполняется или был выполнен последним.
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
    // Проверяет, что минимальный набор сервисов приложения зарегистрирован.
    void ValidateCoreServices() const;

    // Returns the registered logger service.
    // Возвращает зарегистрированный сервис логирования.
    [[nodiscard]] std::shared_ptr<diagnostics::ILogger> Logger() const;

    // Executes one full frame including all configured phases.
    // Выполняет один полный кадр, включая все настроенные фазы.
    void ExecuteFrame();

    // Executes one specific frame phase and updates related diagnostics counters.
    // Выполняет одну конкретную фазу кадра и обновляет относящиеся к ней диагностические счетчики.
    void ExecutePhase(FramePhase phase, const FrameContext &frame_context, diagnostics::ILogger &logger);

    // Invokes handlers registered through AddFramePhaseHandler for the specified phase.
    // Выполняет обработчики, зарегистрированные через AddFramePhaseHandler для конкретной фазы.
    void ExecuteRegisteredPhaseHandlers(FramePhase phase, const FrameContext &frame_context);

    // Builds the frame context for the next frame from runtime clocks and counters.
    // Строит контекст следующего кадра из времени выполнения и счетчиков.
    [[nodiscard]] FrameContext BuildNextFrameContext();

    // Returns true when the configured frame limit has already been reached.
    // Возвращает true, когда настроенный лимит кадров уже достигнут.
    [[nodiscard]] bool ReachedFrameLimit() const noexcept;

    // Drains and executes main-thread tasks through the dispatcher service.
    // Очищает и выполняет задачи главного потока через сервис диспатчера.
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
