// This file defines the smallest smoke app for the EngineBase application shell.
// It verifies that baseline service wiring and one frame of execution succeed without platform or graphics dependencies.

#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Core/application.h>

#include <exception>
#include <iostream>

// Boots the baseline application, runs one frame, and reports process-level success or failure.
int main()
{
    try
    {
        epidemic::core::Application application;
        static_cast<void>(epidemic::enginebase::RegisterEngineBase(
            application, {.runtime_name = "EpidemicHeadlessCoreApp", .log_module = "HeadlessCoreApp"}));
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