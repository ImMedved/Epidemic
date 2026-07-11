#pragma once

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
} // namespace epidemic::runtime
