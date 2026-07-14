#pragma once

#include <cmath>

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

[[nodiscard]] constexpr Vec3 operator+(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr Vec3 Multiply(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] constexpr Transform ComposeTransform(const Transform& parent, const Transform& local) noexcept
{
    return Transform{parent.position + local.position, local.rotation, Multiply(parent.scale, local.scale)};
}

[[nodiscard]] inline bool IsFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool IsFinite(Quat value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] inline bool IsValidTransform(const Transform& transform) noexcept
{
    return IsFinite(transform.position) && IsFinite(transform.rotation) && IsFinite(transform.scale) &&
           transform.scale.x != 0.0f && transform.scale.y != 0.0f && transform.scale.z != 0.0f;
}
} // namespace epidemic::runtime
