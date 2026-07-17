#include "Epidemic/Runtime/Support/runtime_support.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

using epidemic::core::Application;
using namespace epidemic::runtime;

namespace
{
bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

bool TestIndividualRegistrationCreatesRealServices()
{
    Application app{};
    bool ok = Expect(RegisterRuntimeFoundation(app).HasValue(), "foundation should register");
    ok &= Expect(RegisterAssets(app).HasValue(), "assets should register after foundation");
    ok &= Expect(RegisterSerialization(app).HasValue(), "serialization should register after foundation");
    ok &= Expect(RegisterTime(app).HasValue(), "time should register after foundation");
    ok &= Expect(RegisterResources(app).HasValue(), "resources should register after foundation");
    ok &= Expect(RegisterPersistence(app).HasValue(), "persistence should register after foundation");
    ok &= Expect(RegisterEnvironment(app).HasValue(), "environment should register after foundation");
    ok &= Expect(RegisterScene(app).HasValue(), "scene should register after foundation");
    ok &= Expect(RegisterWorld(app).HasValue(), "world should register after foundation");
    ok &= Expect(RegisterStreaming(app).HasValue(), "streaming should register after world/resources/persistence");
    ok &= Expect(RegisterSimulation(app).HasValue(), "simulation should register after time");
    ok &= Expect(RegisterNavigation(app).HasValue(), "navigation should register after environment");
    ok &= Expect(RegisterAnimation(app).HasValue(), "animation should register after resources");
    ok &= Expect(RegisterPhysics(app).HasValue(), "physics should register after scene");
    ok &= Expect(RegisterAudio(app).HasValue(), "audio should register after resources/scene/environment");

    ok &= Expect(app.Services().Contains<RuntimeFoundationRegistration>(), "foundation marker should exist");
    ok &= Expect(app.Services().Contains<AssetServices>(), "asset services should exist");
    ok &= Expect(app.Services().Contains<SerializationServices>(), "serialization services should exist");
    ok &= Expect(app.Services().Contains<TimeServices>(), "time services should exist");
    ok &= Expect(app.Services().Contains<streaming::StreamingServices>(), "streaming services should exist");
    ok &= Expect(app.Services().Contains<physics::PhysicsServices>(), "physics services should exist");
    ok &= Expect(app.Services().Contains<navigation::NavigationServices>(), "navigation services should exist");
    ok &= Expect(app.Services().Contains<animation::AnimationServices>(), "animation services should exist");
    ok &= Expect(app.Services().Contains<audio::AudioServices>(), "audio services should exist");
    ok &= Expect(app.Services().Contains<simulation::SimulationServices>(), "simulation services should exist");
    ok &= Expect(app.Services().Get<AssetServices>()->catalog != nullptr, "asset catalog should be real");
    ok &= Expect(app.Services().Get<SerializationServices>()->archives != nullptr, "archive factory should be real");
    return ok;
}

bool TestDuplicateRegistrationFails()
{
    Application app{};
    bool ok = Expect(RegisterRuntimeFoundation(app).HasValue(), "first foundation registration should pass");
    ok &= Expect(!RegisterRuntimeFoundation(app).HasValue(), "second foundation registration should fail");
    return ok;
}

bool TestMissingDependencyFails()
{
    Application app{};
    bool ok = Expect(RegisterRuntimeFoundation(app).HasValue(), "foundation should register");
    ok &= Expect(!RegisterRenderer(app).HasValue(), "renderer should fail without resources and scene");
    ok &= Expect(RegisterResources(app).HasValue(), "resources should register");
    ok &= Expect(!RegisterRenderer(app).HasValue(), "renderer should still fail without scene");
    ok &= Expect(RegisterScene(app).HasValue(), "scene should register");
    ok &= Expect(RegisterRenderer(app).HasValue(), "renderer should register after dependencies");
    ok &= Expect(!RegisterStreaming(app).HasValue(), "streaming should fail without world and persistence");
    ok &= Expect(RegisterPersistence(app).HasValue(), "persistence should register");
    ok &= Expect(RegisterWorld(app).HasValue(), "world should register");
    ok &= Expect(RegisterStreaming(app).HasValue(), "streaming should register after dependencies");
    ok &= Expect(app.Services().Contains<renderer::RendererServices>(), "renderer services should exist");
    ok &= Expect(app.Services().Get<renderer::RendererServices>()->scene != nullptr, "renderer scene service should be real");
    return ok;
}

bool TestDefaultRegistrationOrder()
{
    Application app{};
    const auto result = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(result.HasValue(), "default runtime should register");
    ok &= Expect(result.HasValue() && result.Value().registered_majors.size() == 16, "default runtime should register current freeze majors");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[0] == "RuntimeFoundation", "foundation should be first");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[1] == "Assets", "assets should follow foundation");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[2] == "Serialization", "serialization should follow assets");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[3] == "Resources", "resources should follow serialization");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[5] == "Time", "time should follow persistence");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[8] == "World", "world should follow scene");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[9] == "Streaming", "streaming should follow world");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[15] == "Renderer", "renderer should be last");
    ok &= Expect(app.Services().Contains<PersistenceServices>(), "persistence services should exist");
    ok &= Expect(app.Services().Contains<EnvironmentServices>(), "environment services should exist");
    ok &= Expect(app.Services().Contains<SceneServices>(), "scene services should exist");
    ok &= Expect(app.Services().Contains<streaming::StreamingServices>(), "streaming services should exist");
    ok &= Expect(app.Services().Contains<physics::PhysicsServices>(), "physics services should exist");
    ok &= Expect(app.Services().Contains<navigation::NavigationServices>(), "navigation services should exist");
    ok &= Expect(app.Services().Contains<animation::AnimationServices>(), "animation services should exist");
    ok &= Expect(app.Services().Contains<audio::AudioServices>(), "audio services should exist");
    ok &= Expect(app.Services().Contains<simulation::SimulationServices>(), "simulation services should exist");
    return ok;
}

bool TestDefaultRegistrationReportsMissingDependency()
{
    Application app{};
    EngineRuntimeOptions options{};
    options.enable_scene = false;
    const auto result = RegisterDefaultEngineRuntime(app, options);
    bool ok = Expect(!result.HasValue(), "default runtime should fail preflight when scene-dependent majors miss scene");
    ok &= Expect(!app.Services().Contains<RuntimeFoundationRegistration>(), "preflight failure should not start registration");
    ok &= Expect(!app.Services().Contains<renderer::RendererServices>(), "failed dependent renderer should not be registered");
    return ok;
}

bool TestUpdateShutdownAndAdapterContracts()
{
    const auto update = GetRuntimeUpdateOrder();
    const auto shutdown = GetRuntimeShutdownOrder();
    const auto adapters = GetAllowedRuntimeAdapters();

    bool ok = Expect(update.size() == 12, "update order should list twelve steps");
    ok &= Expect(update[0] == RuntimeUpdateStep::Time, "update should start with Time");
    ok &= Expect(update[3] == RuntimeUpdateStep::Streaming, "streaming should be fourth update step");
    ok &= Expect(update[11] == RuntimeUpdateStep::DiagnosticsEvents, "diagnostics/events should be last update step");
    ok &= Expect(shutdown.size() == 9, "shutdown order should list nine steps");
    ok &= Expect(shutdown[0] == RuntimeShutdownStep::StopNewWork, "shutdown should stop new work first");
    ok &= Expect(shutdown[8] == RuntimeShutdownStep::DestroyServices, "shutdown should destroy services last");
    ok &= Expect(adapters.size() == 11, "allowed adapter list should match stage 10");
    ok &= Expect(adapters[0] == RuntimeAdapterKind::SceneToRenderer, "first adapter should be scene to renderer");
    ok &= Expect(adapters[10] == RuntimeAdapterKind::EnvironmentToNavigation, "last adapter should be environment to navigation");
    return ok;
}

bool TestSupportDoesNotIncludePrivateHeaders()
{
    std::ifstream header{"EngineRuntime/Support/include/Epidemic/Runtime/Support/runtime_support.h"};
    std::ifstream source{"EngineRuntime/Support/src/runtime_support.cpp"};
    std::string contents{std::istreambuf_iterator<char>(header), std::istreambuf_iterator<char>()};
    contents.append(std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>());

    return Expect(contents.find("_impl.h") == std::string::npos && contents.find("/src/") == std::string::npos &&
                      contents.find("\\src\\") == std::string::npos,
                  "support public composition should not include private implementation headers");
}
} // namespace

int main()
{
    bool ok = true;
    ok &= TestIndividualRegistrationCreatesRealServices();
    ok &= TestDuplicateRegistrationFails();
    ok &= TestMissingDependencyFails();
    ok &= TestDefaultRegistrationOrder();
    ok &= TestDefaultRegistrationReportsMissingDependency();
    ok &= TestUpdateShutdownAndAdapterContracts();
    ok &= TestSupportDoesNotIncludePrivateHeaders();
    return ok ? 0 : 1;
}
