#include <Epidemic/EngineBase/engine_base_support.h>
#include <Epidemic/Core/application.h>

#include <exception>
#include <iostream>

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