#pragma once

#include <Epidemic/Foundation/time.h>

namespace epidemic::core
{
// This file defines the immutable timing payload passed through one frame execution.
// FrameContext connects Application, modules, and frame-phase handlers through a shared snapshot.

struct FrameContext
{
    foundation::FrameIndex frame_index{};
    foundation::FrameTime delta_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());
    foundation::FrameTime absolute_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());
    foundation::FrameTime raw_delta_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());

    // Returns true for the very first executed frame.
    [[nodiscard]] bool IsFirstFrame() const noexcept
    {
        return frame_index.Value() == 0;
    }
};
} // namespace epidemic::core