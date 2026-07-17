#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

#include "physics_runtime_impl.h"

// File note:
// Umbrella translation unit that anchors the public Physics contracts in the build.

namespace epidemic::runtime::physics
{
PhysicsServices CreatePhysicsServices()
{
    auto runtime = std::make_shared<PhysicsRuntime>();

    PhysicsServices services{};
    services.shapes = runtime;
    services.scene = runtime;
    services.stepper = runtime;
    services.query = runtime;
    services.events = runtime;
    services.backend = runtime;
    return services;
}
} // namespace epidemic::runtime::physics
