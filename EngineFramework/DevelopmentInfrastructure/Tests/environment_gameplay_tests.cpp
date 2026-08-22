#include "Epidemic/GameFramework/Environment/environment.h"

#include <array>
#include <cstdlib>

#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::environment;

constexpr EnvironmentValueMask Field(EnvironmentValueField field)
{
    return static_cast<EnvironmentValueMask>(field);
}

int main()
{
    EnvironmentService environment;
    const auto weather = EnvironmentLayerTypeId::FromString("game.weather");
    const auto fog = EnvironmentLayerTypeId::FromString("game.fog");
    const auto toxic = EnvironmentHazardTypeId::FromString("game.hazard.toxic");
    CHECK(environment.RegisterLayerType(weather, "game.weather"));
    CHECK(environment.RegisterLayerType(fog, "game.fog"));
    CHECK(environment.RegisterHazardType(toxic, "game.hazard.toxic"));
    environment.Freeze();
    CHECK(!environment.RegisterLayerType(EnvironmentLayerTypeId::FromString("x"), "x"));

    EnvironmentLayer base;
    base.type = weather;
    base.priority = 0;
    base.blend = EnvironmentBlendPolicy::Override;
    base.values.temperature_milli_c = 10000;
    base.values.humidity = 500;
    base.values.visibility = 1000;
    base.values.light_exposure = 900;
    base.persistence = EnvironmentPersistence::Persistent;
    const auto base_id = environment.AddLayer(base);
    CHECK(base_id);

    EnvironmentLayer rain;
    rain.type = weather;
    rain.priority = 10;
    rain.blend = EnvironmentBlendPolicy::Add;
    rain.bounds = EnvironmentAabb{{0, 0, 0}, {1000, 1000, 1000}};
    rain.values.present = Field(EnvironmentValueField::Precipitation) | Field(EnvironmentValueField::Humidity);
    rain.values.precipitation = 700;
    rain.values.humidity = 300;
    rain.created_at = GameplayTimePoint{5};
    rain.expires_at = GameplayTimePoint{10};
    const auto rain_id = environment.AddLayer(rain);
    CHECK(rain_id);

    // An Override layer touches only explicitly present fields.
    EnvironmentLayer visibility;
    visibility.type = fog;
    visibility.priority = 20;
    visibility.blend = EnvironmentBlendPolicy::Override;
    visibility.values.present = Field(EnvironmentValueField::Visibility);
    visibility.values.visibility = 200;
    const auto visibility_id = environment.AddLayer(visibility);
    CHECK(visibility_id);

    auto before_rain = environment.Sample({100, 100, 100}, GameplayTimePoint{4});
    CHECK(before_rain.values.temperature_milli_c == 10000);
    CHECK(before_rain.values.humidity == 500);
    CHECK(before_rain.values.precipitation == 0);
    CHECK(before_rain.values.visibility == 200);

    auto during_rain = environment.Sample({100, 100, 100}, GameplayTimePoint{5});
    CHECK(during_rain.values.temperature_milli_c == 10000);
    CHECK(during_rain.values.humidity == 800);
    CHECK(during_rain.values.precipitation == 700);
    CHECK(during_rain.values.visibility == 200);
    CHECK(during_rain.values.light_exposure == 900);

    auto at_expiration = environment.Sample({100, 100, 100}, GameplayTimePoint{10});
    CHECK(at_expiration.values.precipitation == 0 && at_expiration.values.humidity == 500);

    // Multiple semantic scopes may contribute simultaneously.
    const auto city = GameplayObjectRef{GameplayDomainId::FromString("framework.world"), GameplayObjectId::FromString("scope.city")};
    const auto room = GameplayObjectRef{GameplayDomainId::FromString("framework.world"), GameplayObjectId::FromString("scope.room")};
    EnvironmentLayer city_layer;
    city_layer.type = weather;
    city_layer.area_ref = city;
    city_layer.priority = 30;
    city_layer.blend = EnvironmentBlendPolicy::Add;
    city_layer.values.present = Field(EnvironmentValueField::Temperature);
    city_layer.values.temperature_milli_c = 1000;
    CHECK(environment.AddLayer(city_layer));

    EnvironmentLayer room_layer;
    room_layer.type = weather;
    room_layer.area_ref = room;
    room_layer.priority = 40;
    room_layer.blend = EnvironmentBlendPolicy::Override;
    room_layer.values.present = Field(EnvironmentValueField::LightExposure);
    room_layer.values.light_exposure = 700;
    room_layer.hazards.push_back(EnvironmentHazard{toxic, 600, {}});
    CHECK(environment.AddLayer(room_layer));

    const std::array scopes{city, room};
    const auto scoped = environment.Sample({5000, 0, 0}, GameplayTimePoint{1}, scopes);
    CHECK(scoped.values.temperature_milli_c == 11000);
    CHECK(scoped.values.light_exposure == 700);
    CHECK(scoped.hazards.size() == 1 && scoped.hazards.front().type == toxic && scoped.hazards.front().intensity == 600);

    // Invalid temporal ranges are rejected before state mutation.
    EnvironmentLayer invalid_time;
    invalid_time.type = weather;
    invalid_time.created_at = GameplayTimePoint{10};
    invalid_time.expires_at = GameplayTimePoint{10};
    CHECK(!environment.AddLayer(invalid_time));

    const auto snapshot = environment.CaptureSnapshot();
    CHECK(snapshot.layers.size() == 1); // only the persistent base layer

    EnvironmentService restored;
    CHECK(restored.RegisterLayerType(weather, "game.weather"));
    CHECK(restored.RegisterLayerType(fog, "game.fog"));
    CHECK(restored.RegisterHazardType(toxic, "game.hazard.toxic"));
    restored.Freeze();
    CHECK(restored.RestoreSnapshot(snapshot));
    CHECK(restored.GetLayer(base_id.Value()).has_value());

    // Restore validates the entire snapshot before replacing live state.
    auto corrupt = snapshot;
    corrupt.layers.push_back(corrupt.layers.front());
    const auto before_corrupt_revision = restored.CurrentRevision();
    CHECK(!restored.RestoreSnapshot(corrupt));
    CHECK(restored.CurrentRevision() == before_corrupt_revision && restored.GetLayer(base_id.Value()).has_value());

    return 0;
}
