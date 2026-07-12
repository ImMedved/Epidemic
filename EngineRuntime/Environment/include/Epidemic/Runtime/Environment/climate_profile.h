#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
} 
