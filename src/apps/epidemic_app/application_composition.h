#pragma once

#include "core/application/application.h"

namespace epidemic::apps::epidemic_app
{
// Registers concrete platform and runtime placeholder services for the demo host.
void RegisterApplicationServices(core::ServiceContainer &services);
// Registers the demo module set for the demo host.
void RegisterApplicationModules(core::ModuleRegistry &modules);
} // namespace epidemic::apps::epidemic_app
