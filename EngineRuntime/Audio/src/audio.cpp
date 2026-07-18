#include "audio_runtime_impl.h"

#include "Epidemic/Foundation/error.h"

#include <utility>

namespace epidemic::runtime::audio
{
std::unique_ptr<AudioRuntime> CreateAudioRuntime(AudioOptions options, AudioDependencies dependencies)
{
    return std::make_unique<AudioRuntime>(options, std::move(dependencies));
}

foundation::Result<AudioServices> CreateAudioServices(AudioOptions options, AudioDependencies dependencies)
{
    if (dependencies.backend == nullptr)
    {
        return foundation::Result<AudioServices>::Failure(
            foundation::Error::Create("audio.backend_missing", "audio backend is required by production audio services"));
    }

    const auto initialized = dependencies.backend->Initialize(AudioBackendOptions{true});
    if (!initialized)
    {
        return foundation::Result<AudioServices>::Failure(initialized.GetError());
    }

    auto backend = dependencies.backend;
    auto runtime = std::make_shared<AudioRuntime>(options, std::move(dependencies));

    AudioServices services{};
    services.sounds = runtime;
    services.runtime = runtime;
    services.listeners = runtime;
    services.events = runtime;
    services.mixer = runtime;
    services.backend = std::move(backend);
    return foundation::Result<AudioServices>::Success(std::move(services));
}

foundation::Result<AudioServices> CreateMockAudioServices(AudioOptions options)
{
    auto runtime = std::make_shared<AudioRuntime>(options);

    AudioServices services{};
    services.sounds = runtime;
    services.runtime = runtime;
    services.listeners = runtime;
    services.events = runtime;
    services.mixer = runtime;
    services.backend = runtime;
    return foundation::Result<AudioServices>::Success(std::move(services));
}
} // namespace epidemic::runtime::audio
