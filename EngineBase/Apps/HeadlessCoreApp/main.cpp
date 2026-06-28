#include <Epidemic/Core/application.h>

#include <exception>
#include <iostream>

int main()
{
    try
    {
        epidemic::core::Application application;
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