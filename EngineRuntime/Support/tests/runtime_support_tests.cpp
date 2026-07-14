#include "Epidemic/Runtime/Support/runtime_support.h"

#include <iostream>
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
    ok &= Expect(app.Services().Contains<RuntimeFoundationRegistration>(), "foundation marker should exist");
    ok &= Expect(app.Services().Contains<AssetServices>(), "asset services should exist");
    ok &= Expect(app.Services().Contains<SerializationServices>(), "serialization services should exist");
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
    ok &= Expect(app.Services().Contains<renderer::RendererServices>(), "renderer services should exist");
    ok &= Expect(app.Services().Get<renderer::RendererServices>()->scene != nullptr, "renderer scene service should be real");
    return ok;
}

bool TestDefaultRegistrationOrder()
{
    Application app{};
    const auto result = RegisterDefaultEngineRuntime(app);
    bool ok = Expect(result.HasValue(), "default runtime should register");
    ok &= Expect(result.HasValue() && result.Value().registered_majors.size() == 8, "default runtime should register current freeze majors");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[0] == "RuntimeFoundation", "foundation should be first");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[1] == "Assets", "assets should follow foundation");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[2] == "Serialization", "serialization should follow assets");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[3] == "Resources", "resources should follow serialization");
    ok &= Expect(result.HasValue() && result.Value().registered_majors[7] == "Renderer", "renderer should be last");
    ok &= Expect(app.Services().Contains<PersistenceServices>(), "persistence services should exist");
    ok &= Expect(app.Services().Contains<EnvironmentServices>(), "environment services should exist");
    ok &= Expect(app.Services().Contains<SceneServices>(), "scene services should exist");
    return ok;
}

bool TestDefaultRegistrationReportsMissingDependency()
{
    Application app{};
    EngineRuntimeOptions options{};
    options.enable_scene = false;
    const auto result = RegisterDefaultEngineRuntime(app, options);
    bool ok = Expect(!result.HasValue(), "default runtime should fail when renderer misses scene");
    ok &= Expect(app.Services().Contains<RuntimeFoundationRegistration>(), "partial registration should keep completed foundation");
    ok &= Expect(!app.Services().Contains<renderer::RendererServices>(), "failed dependent renderer should not be registered");
    return ok;
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
    return ok ? 0 : 1;
}
