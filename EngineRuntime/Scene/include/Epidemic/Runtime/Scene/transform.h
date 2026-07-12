#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
namespace epidemic::runtime
{
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Vec3&) const noexcept = default;
};

struct Quat
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    [[nodiscard]] constexpr bool operator==(const Quat&) const noexcept = default;
};

struct Transform
{
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] constexpr bool operator==(const Transform&) const noexcept = default;
};
} 
