#pragma once

namespace epidemic::layers::runtime
{
class IRenderer
{
  public:
    virtual ~IRenderer() = default;

    virtual void RequestFrame() = 0;
};
} // namespace epidemic::layers::runtime
