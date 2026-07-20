#pragma once

namespace epidemic::runtime
{
struct ClimateProfile
{
    float average_temperature = 0.0f;
    float average_humidity = 0.0f;
    float average_wind_speed = 0.0f;
    float annual_precipitation = 0.0f;

    [[nodiscard]] constexpr bool operator==(const ClimateProfile&) const noexcept = default;
};
} // namespace epidemic::runtime
