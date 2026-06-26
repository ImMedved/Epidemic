#include "apps/epidemic_app/demo_modules.h"
#include "core/application/application.h"

#include <exception>
#include <iostream>

int main()
{
    try
    {
        epidemic::core::Application application;
        auto modules = epidemic::apps::epidemic_app::CreateDemoModules();
        for (auto &module : modules)
        {
            application.Modules().Register(std::move(module));
        }

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
