#pragma once

namespace epidemic::runtime
{
enum class WeatherKind
{
    Clear,
    Cloudy,
    Rain,
    Storm,
    Snow,
    Fog,
    Transitioning,
};

struct WeatherState
{
    WeatherKind kind = WeatherKind::Clear;
    float intensity = 0.0f;
    float cloudiness = 0.0f;
    float precipitation = 0.0f;
    float wind_speed = 0.0f;
    float wind_direction_degrees = 0.0f;

    [[nodiscard]] constexpr bool operator==(const WeatherState&) const noexcept = default;
};
} // namespace epidemic::runtime
