#include <Epidemic/Core/application.h>
#include <Epidemic/Platform/iplatform_runtime.h>
#include <Epidemic/Platform/windows_platform_runtime.h>

#include <exception>
#include <iostream>

int main()
{
    try
    {
        epidemic::core::Application application;
        application.Services().Emplace<epidemic::platform::IPlatformRuntime, epidemic::platform::WindowsPlatformRuntime>();

        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "EpidemicInputSmokeApp failed: " << exception.what() << '\n';
        return 1;
    }
}