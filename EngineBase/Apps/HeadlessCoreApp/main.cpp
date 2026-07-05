#include <Epidemic/Core/application.h>
#include <Epidemic/Core/basic_configuration.h>
#include <Epidemic/Core/configuration.h>
#include <Epidemic/Core/event_bus.h>
#include <Epidemic/Core/task_scheduler.h>
#include <Epidemic/Diagnostics/console_logger.h>
#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/thread_context.h>

#include <exception>
#include <iostream>

namespace
{
void RegisterCoreServices(epidemic::core::Application &application, std::string runtime_name)
{
    epidemic::diagnostics::SetCurrentThreadName("Main");
    application.Services().Emplace<epidemic::diagnostics::ILogger, epidemic::diagnostics::ConsoleLogger>();
    const auto configuration =
        application.Services().Emplace<epidemic::core::config::IConfiguration, epidemic::core::config::BasicConfiguration>();
    configuration->SetRuntimeName(std::move(runtime_name));
    configuration->SetWorkerCount(1);
    configuration->SetMemoryTrackingEnabled(true);
    configuration->SetRhiDebugEnabled(false);
    configuration->SetDefaultWindowWidth(1280);
    configuration->SetDefaultWindowHeight(720);
    application.Services().Emplace<epidemic::core::events::IEventBus, epidemic::core::events::EventBus>();
    application.Services().Emplace<epidemic::core::tasks::ITaskScheduler, epidemic::core::tasks::SimpleTaskScheduler>(1);

    epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::MemoryUsed, 0);
    const auto logger = application.Services().Get<epidemic::diagnostics::ILogger>();
    logger->Info("HeadlessCoreApp", "Startup", "Diagnostics baseline initialized");
    logger->Info("HeadlessCoreApp", "Startup", "Worker threads: 1");
    logger->Info("HeadlessCoreApp", "Startup", "Memory tracking: enabled");
}
} // namespace

int main()
{
    try
    {
        epidemic::core::Application application;
        RegisterCoreServices(application, "EpidemicHeadlessCoreApp");
        application.SetFrameLimit(1);
        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "EpidemicHeadlessCoreApp failed: " << exception.what() << '\n';
        return 1;
    }
}
