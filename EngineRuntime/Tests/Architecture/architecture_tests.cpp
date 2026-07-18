#include "Epidemic/Runtime/Animation/animation_runtime.h"
#include "Epidemic/Runtime/Assets/asset_services.h"
#include "Epidemic/Runtime/Audio/audio_runtime.h"
#include "Epidemic/Runtime/Environment/environment_services.h"
#include "Epidemic/Runtime/Foundation/runtime_foundation.h"
#include "Epidemic/Runtime/Navigation/navigation_runtime.h"
#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Physics/physics_scene.h"
#include "Epidemic/Runtime/Renderer/renderer_services.h"
#include "Epidemic/Runtime/Resources/resource_handle.h"
#include "Epidemic/Runtime/Scene/scene_services.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"
#include "Epidemic/Runtime/Simulation/simulation_runtime.h"
#include "Epidemic/Runtime/Streaming/streaming_runtime.h"
#include "Epidemic/Runtime/Support/runtime_support.h"
#include "Epidemic/Runtime/Time/time_runtime.h"
#include "Epidemic/Runtime/World/world_services.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
namespace fs = std::filesystem;

bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

fs::path RepositoryRoot()
{
    fs::path current = fs::path{__FILE__}.lexically_normal();
    for (int index = 0; index < 4; ++index)
    {
        current = current.parent_path();
    }
    return current;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream stream{path};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool Contains(const std::string& text, std::string_view needle)
{
    return text.find(needle) != std::string::npos;
}

bool TestNoPrivateCrossMajorIncludes()
{
    const fs::path root = RepositoryRoot();
    bool ok = true;
    for (const fs::directory_entry& major : fs::directory_iterator(root / "EngineRuntime"))
    {
        if (!major.is_directory())
        {
            continue;
        }

        const fs::path include_root = major.path() / "include";
        if (!fs::exists(include_root))
        {
            continue;
        }

        for (const fs::directory_entry& file : fs::recursive_directory_iterator(include_root))
        {
            if (!file.is_regular_file())
            {
                continue;
            }

            const std::string contents = ReadFile(file.path());
            ok &= Expect(!Contains(contents, "_impl.h"), "public header must not include private implementation headers");
            ok &= Expect(!Contains(contents, "/src/") && !Contains(contents, "\\src\\"),
                         "public header must not include private src paths");
        }
    }
    return ok;
}

bool TestNoMajorDependsOnSupport()
{
    const fs::path root = RepositoryRoot() / "EngineRuntime";
    bool ok = true;
    for (const fs::directory_entry& major : fs::directory_iterator(root))
    {
        if (!major.is_directory() || major.path().filename() == "Support" || major.path().filename() == "Tests")
        {
            continue;
        }

        const fs::path cmake = major.path() / "CMakeLists.txt";
        if (!fs::exists(cmake))
        {
            continue;
        }

        const std::string contents = ReadFile(cmake);
        ok &= Expect(Contains(contents, "EpidemicRuntimeSupport"), "major CMake must explicitly forbid Support dependency");
        ok &= Expect(!Contains(contents, "PUBLIC EpidemicRuntimeSupport") &&
                         !Contains(contents, "PRIVATE EpidemicRuntimeSupport"),
                     "runtime major must not link Support");
    }
    return ok;
}

bool TestAllMajorsHaveFactoriesAndHandlesAreGenerationAware()
{
    using namespace epidemic::runtime;

    static_assert(std::is_same_v<decltype(streaming::CreateStreamingServices()), epidemic::foundation::Result<streaming::StreamingServices>>);
    static_assert(std::is_same_v<decltype(physics::CreatePhysicsServices()), physics::PhysicsServices>);
    static_assert(std::is_same_v<decltype(navigation::CreateNavigationServices()), epidemic::foundation::Result<navigation::NavigationServices>>);
    static_assert(std::is_same_v<decltype(navigation::CreateMockNavigationServices()), navigation::NavigationServices>);
    static_assert(std::is_same_v<decltype(animation::CreateAnimationServices()), epidemic::foundation::Result<animation::AnimationServices>>);
    static_assert(std::is_same_v<decltype(animation::CreateReferenceAnimationServices()), animation::AnimationServices>);
    static_assert(std::is_same_v<decltype(animation::CreateMockAnimationServices()), animation::AnimationServices>);
    static_assert(std::is_same_v<decltype(audio::CreateAudioServices()), epidemic::foundation::Result<audio::AudioServices>>);
    static_assert(std::is_same_v<decltype(audio::CreateMockAudioServices()), epidemic::foundation::Result<audio::AudioServices>>);
    static_assert(std::is_same_v<decltype(simulation::CreateSimulationServices()), simulation::SimulationServices>);

    streaming::StreamingRequestHandle streaming_handle{{1}, 1};
    navigation::PathQueryHandle path_handle{{1}, 1};
    animation::AnimatorHandle animator_handle{{1}, 1};
    audio::AudioEmitterHandle emitter_handle{{1}, 1};
    simulation::SimulationJobHandle job_handle{{1}, 1};

    return streaming_handle.IsValid() && path_handle.IsValid() && animator_handle.IsValid() &&
           emitter_handle.IsValid() && job_handle.IsValid();
}

bool TestSnapshotsAreVersioned()
{
    using namespace epidemic::runtime;

    static_assert(std::is_same_v<decltype(TimeSnapshot{}.revision), std::uint64_t>);
    static_assert(std::is_same_v<decltype(streaming::StreamingProgress{}.revision), std::uint64_t>);
    static_assert(std::is_same_v<decltype(physics::PhysicsStepResult{}.revision), std::uint64_t>);
    static_assert(std::is_same_v<decltype(navigation::PathResult{}.revision), std::uint64_t>);
    static_assert(std::is_same_v<decltype(animation::PoseSnapshot{}.revision), std::uint64_t>);
    static_assert(std::is_same_v<decltype(audio::AudioEmitterSnapshot{}.revision), std::uint64_t>);
    return true;
}
} // namespace

int main()
{
    epidemic::runtime::ResourceHandle handle{};
    static_assert(!std::is_same_v<epidemic::runtime::ResourceId, epidemic::runtime::ResourceHandle>);

    bool ok = true;
    ok &= Expect(!handle.IsValid(), "default resource handle should be invalid");
    ok &= TestNoPrivateCrossMajorIncludes();
    ok &= TestNoMajorDependsOnSupport();
    ok &= TestAllMajorsHaveFactoriesAndHandlesAreGenerationAware();
    ok &= TestSnapshotsAreVersioned();
    return ok ? 0 : 1;
}
