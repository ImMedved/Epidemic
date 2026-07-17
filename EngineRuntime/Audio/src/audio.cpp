#include "audio_runtime_impl.h"

namespace epidemic::runtime::audio
{
std::unique_ptr<AudioRuntime> CreateAudioRuntime(AudioOptions options)
{
    return std::make_unique<AudioRuntime>(options);
}

AudioServices CreateMockAudioServices(AudioOptions options)
{
    auto runtime = std::make_shared<AudioRuntime>(options);

    AudioServices services{};
    services.sounds = runtime;
    services.runtime = runtime;
    services.listeners = runtime;
    services.events = runtime;
    services.mixer = runtime;
    services.backend = runtime;
    return services;
}
} // namespace epidemic::runtime::audio
