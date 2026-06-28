#include "apps/epidemic_app/application_composition.h"
#include "core/application/application.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        epidemic::core::Application application;
        epidemic::apps::epidemic_app::RegisterApplicationServices(application.Services());
        epidemic::apps::epidemic_app::RegisterApplicationModules(application.Modules());

        application.Bootstrap();
        application.Initialize();
        application.Run();
        application.Shutdown();
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "epidemic_app failed: " << exception.what() << '\n';
        return 1;
    }
}
