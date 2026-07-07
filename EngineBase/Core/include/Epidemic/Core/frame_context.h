#pragma once

#include <Epidemic/Foundation/time.h>

namespace epidemic::core
{
struct FrameContext
{
    foundation::FrameIndex frame_index{};
    foundation::FrameTime delta_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());
    foundation::FrameTime absolute_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());
    foundation::FrameTime raw_delta_time = foundation::FrameTime::FromDuration(foundation::Duration::zero());

    [[nodiscard]] bool IsFirstFrame() const noexcept
    {
        return frame_index.Value() == 0;
    }
};
} // namespace epidemic::core
