#pragma once

namespace epidemic::layers::runtime
{
class IRenderer
{
  public:
    virtual ~IRenderer() = default;

    // Requests one frame of renderer work from the active backend.
    virtual void RequestFrame() = 0;
};
} // namespace epidemic::layers::runtime
