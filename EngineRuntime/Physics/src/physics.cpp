#include "Epidemic/Runtime/Physics/physics_event_buffer.h"
#include "Epidemic/Runtime/Physics/physics_query.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Physics/physics_types.h"

#include "physics_runtime_impl.h"

#include <exception>
#include <utility>

// Umbrella translation unit that anchors the public Physics contracts in the build.

namespace epidemic::runtime::physics
{
foundation::Result<PhysicsServices> CreatePhysicsServices()
{
    return CreatePhysicsServices(PhysicsDependencies{});
}

foundation::Result<PhysicsServices> CreatePhysicsServices(std::shared_ptr<IPhysicsBackend> backend)
{
    return CreatePhysicsServices(PhysicsDependencies{std::move(backend), nullptr, nullptr});
}

foundation::Result<PhysicsServices> CreatePhysicsServices(PhysicsDependencies dependencies)
{
    if (dependencies.backend)
    {
        foundation::Result<void> initialized = foundation::Result<void>::Failure(
            foundation::Error::Create("physics.backend_exception", "physics backend initialization did not run"));
        try
        {
            initialized = dependencies.backend->Initialize(PhysicsBackendOptions{});
        }
        catch (const std::exception& exception)
        {
            return foundation::Result<PhysicsServices>::Failure(
                foundation::Error::Create("physics.backend_exception",
                                          "physics backend threw during initialization",
                                          exception.what()));
        }
        catch (...)
        {
            return foundation::Result<PhysicsServices>::Failure(
                foundation::Error::Create("physics.backend_exception",
                                          "physics backend threw during initialization"));
        }
        if (!initialized)
        {
            return foundation::Result<PhysicsServices>::Failure(initialized.GetError());
        }
    }
    std::shared_ptr<IPhysicsBackend> external_backend = dependencies.backend;
    auto runtime = std::make_shared<PhysicsRuntime>(std::move(dependencies));

    PhysicsServices services{};
    services.shapes = runtime;
    services.scene = runtime;
    services.stepper = runtime;
    services.query = runtime;
    services.events = runtime;
    services.backend = external_backend ? external_backend : std::static_pointer_cast<IPhysicsBackend>(runtime);
    services.lifecycle = runtime;
    return foundation::Result<PhysicsServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::physics
