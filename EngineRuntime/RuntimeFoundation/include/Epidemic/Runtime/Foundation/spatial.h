#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace epidemic::runtime
{
constexpr float kSpatialEpsilon = 0.000001f;

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

using Quaternion = Quat;

struct Transform
{
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] constexpr bool operator==(const Transform&) const noexcept = default;
};

struct Aabb
{
    Vec3 min{};
    Vec3 max{};

    [[nodiscard]] constexpr bool operator==(const Aabb&) const noexcept = default;
};

struct Sphere
{
    Vec3 center{};
    float radius = 0.0f;

    [[nodiscard]] constexpr bool operator==(const Sphere&) const noexcept = default;
};

[[nodiscard]] constexpr Vec3 operator+(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr Vec3 operator-(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr Vec3 operator*(Vec3 value, float scalar) noexcept
{
    return Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] constexpr Vec3 Multiply(Vec3 left, Vec3 right) noexcept
{
    return Vec3{left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] inline Quaternion Normalize(const Quaternion& value) noexcept
{
    const float length_squared = (value.x * value.x) + (value.y * value.y) + (value.z * value.z) + (value.w * value.w);
    if (!std::isfinite(length_squared) || length_squared <= kSpatialEpsilon)
    {
        return {};
    }

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return Quaternion{value.x * inverse_length, value.y * inverse_length, value.z * inverse_length, value.w * inverse_length};
}

[[nodiscard]] inline Quaternion Multiply(const Quaternion& left, const Quaternion& right) noexcept
{
    return Normalize(Quaternion{
        (left.w * right.x) + (left.x * right.w) + (left.y * right.z) - (left.z * right.y),
        (left.w * right.y) - (left.x * right.z) + (left.y * right.w) + (left.z * right.x),
        (left.w * right.z) + (left.x * right.y) - (left.y * right.x) + (left.z * right.w),
        (left.w * right.w) - (left.x * right.x) - (left.y * right.y) - (left.z * right.z),
    });
}

[[nodiscard]] inline Vec3 RotateVector(const Quaternion& rotation, Vec3 value) noexcept
{
    const Quaternion normalized = Normalize(rotation);
    const Vec3 q{normalized.x, normalized.y, normalized.z};
    const Vec3 uv{
        (q.y * value.z) - (q.z * value.y),
        (q.z * value.x) - (q.x * value.z),
        (q.x * value.y) - (q.y * value.x),
    };
    const Vec3 uuv{
        (q.y * uv.z) - (q.z * uv.y),
        (q.z * uv.x) - (q.x * uv.z),
        (q.x * uv.y) - (q.y * uv.x),
    };
    return value + (uv * (2.0f * normalized.w)) + (uuv * 2.0f);
}

[[nodiscard]] inline Vec3 TransformPoint(const Transform& transform, Vec3 point) noexcept
{
    return transform.position + RotateVector(transform.rotation, Multiply(point, transform.scale));
}

[[nodiscard]] inline Transform ComposeTransform(const Transform& parent, const Transform& local) noexcept
{
    // Contract: this runtime foundation stores spatial hierarchy as simplified TRS.
    // Non-uniform parent scale combined with rotated children may produce shear in a full matrix model;
    // ComposeTransform preserves a deterministic approximate TRS representation instead of shear.
    return Transform{
        TransformPoint(parent, local.position),
        Multiply(parent.rotation, local.rotation),
        Multiply(parent.scale, local.scale),
    };
}

[[nodiscard]] inline Aabb TransformAabb(const Transform& transform, const Aabb& bounds) noexcept
{
    const Vec3 corners[] = {
        Vec3{bounds.min.x, bounds.min.y, bounds.min.z},
        Vec3{bounds.min.x, bounds.min.y, bounds.max.z},
        Vec3{bounds.min.x, bounds.max.y, bounds.min.z},
        Vec3{bounds.min.x, bounds.max.y, bounds.max.z},
        Vec3{bounds.max.x, bounds.min.y, bounds.min.z},
        Vec3{bounds.max.x, bounds.min.y, bounds.max.z},
        Vec3{bounds.max.x, bounds.max.y, bounds.min.z},
        Vec3{bounds.max.x, bounds.max.y, bounds.max.z},
    };

    Aabb transformed{TransformPoint(transform, corners[0]), TransformPoint(transform, corners[0])};
    for (std::size_t index = 1; index < std::size(corners); ++index)
    {
        const Vec3 point = TransformPoint(transform, corners[index]);
        transformed.min.x = std::min(transformed.min.x, point.x);
        transformed.min.y = std::min(transformed.min.y, point.y);
        transformed.min.z = std::min(transformed.min.z, point.z);
        transformed.max.x = std::max(transformed.max.x, point.x);
        transformed.max.y = std::max(transformed.max.y, point.y);
        transformed.max.z = std::max(transformed.max.z, point.z);
    }

    return transformed;
}

[[nodiscard]] inline bool IsFinite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool IsFinite(Quat value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] inline bool IsNormalized(Quat value) noexcept
{
    const float length_squared = (value.x * value.x) + (value.y * value.y) + (value.z * value.z) + (value.w * value.w);
    return std::isfinite(length_squared) && std::abs(length_squared - 1.0f) <= 0.001f;
}

[[nodiscard]] inline bool IsValidTransform(const Transform& transform) noexcept
{
    return IsFinite(transform.position) && IsFinite(transform.rotation) && IsFinite(transform.scale) &&
           IsNormalized(transform.rotation) && transform.scale.x != 0.0f && transform.scale.y != 0.0f &&
           transform.scale.z != 0.0f;
}

[[nodiscard]] inline bool IsValidAabb(const Aabb& bounds) noexcept
{
    return IsFinite(bounds.min) && IsFinite(bounds.max) && bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
           bounds.min.z <= bounds.max.z;
}

[[nodiscard]] constexpr Aabb TranslateBounds(const Aabb& bounds, Vec3 offset) noexcept
{
    return Aabb{bounds.min + offset, bounds.max + offset};
}
} // namespace epidemic::runtime
