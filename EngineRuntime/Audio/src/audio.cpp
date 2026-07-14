#include "audio_runtime_impl.h"

namespace epidemic::runtime::audio
{
std::unique_ptr<AudioRuntime> CreateAudioRuntime(AudioOptions options)
{
    return std::make_unique<AudioRuntime>(options);
}
} // namespace epidemic::runtime::audio
