#pragma once

namespace epidemic::enginebase
{
// This file defines the graphics backend choices exposed by EngineBase support wiring.
// Higher layers choose one of these values during composition and remain backend-agnostic afterward.

enum class GraphicsBackend
{
    Null,
    D3D11,
};
} // namespace epidemic::enginebase