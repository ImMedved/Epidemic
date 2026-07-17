#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

#include "physics_runtime_impl.h"

#include <utility>

// File note:
// Umbrella translation unit that anchors the public Physics contracts in the build.

namespace epidemic::runtime::physics
{
PhysicsServices CreatePhysicsServices()
{
    return CreatePhysicsServices(nullptr);
}

PhysicsServices CreatePhysicsServices(std::shared_ptr<IPhysicsBackend> backend)
{
    auto runtime = std::make_shared<PhysicsRuntime>(std::move(backend));

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
