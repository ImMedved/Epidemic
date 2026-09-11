#include "allocation_fault_injection.h"
#include "Epidemic/GameFramework/Environment/environment.h"

#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>

#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::environment;

constexpr EnvironmentValueMask Field(EnvironmentValueField field)
{
    return static_cast<EnvironmentValueMask>(field);
}

static EnvironmentService MakeService(EnvironmentLayerTypeId weather, EnvironmentLayerTypeId fog,
                                      EnvironmentHazardTypeId toxic)
{
    EnvironmentService environment;
    CHECK(environment.RegisterLayerType(weather, "game.weather"));
    CHECK(environment.RegisterLayerType(fog, "game.fog"));
    CHECK(environment.RegisterHazardType(toxic, "game.hazard.toxic"));
    return environment;
}

int main()
{
    const auto weather = EnvironmentLayerTypeId::FromString("game.weather");
    const auto fog = EnvironmentLayerTypeId::FromString("game.fog");
    const auto toxic = EnvironmentHazardTypeId::FromString("game.hazard.toxic");

    EnvironmentService environment = MakeService(weather, fog, toxic);
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

    EnvironmentLayer visibility;
    visibility.type = fog;
    visibility.priority = 20;
    visibility.blend = EnvironmentBlendPolicy::Override;
    visibility.values.present = Field(EnvironmentValueField::Visibility);
    visibility.values.visibility = 200;
    const auto visibility_id = environment.AddLayer(visibility);
    CHECK(visibility_id);

    auto before_rain = environment.Sample({100, 100, 100}, GameplayTimePoint{4});
    CHECK(before_rain);
    CHECK(before_rain.Value().values.temperature_milli_c == 10000);
    CHECK(before_rain.Value().values.humidity == 500);
    CHECK(before_rain.Value().values.precipitation == 0);
    CHECK(before_rain.Value().values.visibility == 200);

    auto during_rain = environment.Sample({100, 100, 100}, GameplayTimePoint{5});
    CHECK(during_rain);
    CHECK(during_rain.Value().values.temperature_milli_c == 10000);
    CHECK(during_rain.Value().values.humidity == 800);
    CHECK(during_rain.Value().values.precipitation == 700);
    CHECK(during_rain.Value().values.visibility == 200);
    CHECK(during_rain.Value().values.light_exposure == 900);

    auto at_expiration = environment.Sample({100, 100, 100}, GameplayTimePoint{10});
    CHECK(at_expiration);
    CHECK(at_expiration.Value().values.precipitation == 0 && at_expiration.Value().values.humidity == 500);

    const auto city = GameplayObjectRef{GameplayDomainId::FromString("framework.world"),
                                        GameplayObjectId::FromString("scope.city")};
    const auto room = GameplayObjectRef{GameplayDomainId::FromString("framework.world"),
                                        GameplayObjectId::FromString("scope.room")};
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
    const auto room_layer_id = environment.AddLayer(room_layer);
    CHECK(room_layer_id);

    const std::array scopes{city, room};
    const auto scoped = environment.Sample({5000, 0, 0}, GameplayTimePoint{1}, scopes);
    CHECK(scoped);
    CHECK(scoped.Value().values.temperature_milli_c == 11000);
    CHECK(scoped.Value().values.light_exposure == 700);
    CHECK(scoped.Value().hazards.size() == 1);
    CHECK(scoped.Value().hazards.front().source_layer == room_layer_id.Value());
    CHECK(scoped.Value().hazards.front().hazard.type == toxic);
    CHECK(scoped.Value().hazards.front().hazard.intensity == 600);

    // Equal hazard types from different layers remain distinct and preserve provenance.
    EnvironmentLayer second_hazard_layer;
    second_hazard_layer.type = weather;
    second_hazard_layer.area_ref = room;
    second_hazard_layer.priority = 41;
    second_hazard_layer.values.present = 0;
    second_hazard_layer.hazards.push_back(EnvironmentHazard{toxic, 300, {}});
    const auto second_hazard_id = environment.AddLayer(second_hazard_layer);
    CHECK(second_hazard_id);
    const auto two_hazards = environment.Sample({5000, 0, 0}, GameplayTimePoint{1}, scopes);
    CHECK(two_hazards && two_hazards.Value().hazards.size() == 2);
    CHECK(two_hazards.Value().hazards[0].source_layer == room_layer_id.Value());
    CHECK(two_hazards.Value().hazards[1].source_layer == second_hazard_id.Value());

    // Invalid temporal ranges and normalized value ranges are rejected before state mutation.
    EnvironmentLayer invalid_time;
    invalid_time.type = weather;
    invalid_time.created_at = GameplayTimePoint{10};
    invalid_time.expires_at = GameplayTimePoint{10};
    CHECK(!environment.AddLayer(invalid_time));

    EnvironmentLayer invalid_values;
    invalid_values.type = weather;
    invalid_values.values.humidity = kEnvironmentOne + 1;
    CHECK(!environment.AddLayer(invalid_values));
    invalid_values.values.humidity = 0;
    invalid_values.values.wind_x = kEnvironmentOne + 1;
    CHECK(!environment.AddLayer(invalid_values));

    // Invalid enum values are rejected before any authoritative mutation.
    const auto before_invalid_enum_revision = environment.CurrentRevision();
    EnvironmentLayer invalid_blend;
    invalid_blend.type = weather;
    invalid_blend.blend = static_cast<EnvironmentBlendPolicy>(999);
    CHECK(!environment.AddLayer(invalid_blend));
    CHECK(environment.CurrentRevision() == before_invalid_enum_revision);

    // Spatial updates are incremental: moving a layer removes its old cell membership and adds the new one.
    EnvironmentLayer moving;
    moving.type = fog;
    moving.priority = 100;
    moving.bounds = EnvironmentAabb{{200000, 0, 0}, {200100, 100, 100}};
    moving.values.present = Field(EnvironmentValueField::Visibility);
    moving.values.visibility = 111;
    const auto moving_id = environment.AddLayer(moving);
    CHECK(moving_id);
    CHECK(environment.Sample({200050, 50, 50}, GameplayTimePoint{1}).Value().values.visibility == 111);
    moving.id = moving_id.Value();
    moving.bounds = EnvironmentAabb{{400000, 0, 0}, {400100, 100, 100}};
    CHECK(environment.UpdateLayer(moving));
    CHECK(environment.Sample({200050, 50, 50}, GameplayTimePoint{1}).Value().values.visibility != 111);
    CHECK(environment.Sample({400050, 50, 50}, GameplayTimePoint{1}).Value().values.visibility == 111);

    // Caller supplied IDs in the service scope advance the generator past that ID.
    EnvironmentService ids_service = MakeService(weather, fog, toxic);
    ids_service.Freeze();
    EnvironmentLayer first;
    first.type = weather;
    const auto generated = ids_service.AddLayer(first);
    CHECK(generated);
    const auto own_scope = generated.Value().value.High();
    EnvironmentLayer requested;
    requested.id = EnvironmentLayerId::FromRaw(own_scope, generated.Value().value.Low() + 50);
    requested.type = weather;
    const auto requested_result = ids_service.AddLayer(requested);
    CHECK(requested_result);
    EnvironmentLayer next;
    next.type = weather;
    const auto next_result = ids_service.AddLayer(next);
    CHECK(next_result);
    CHECK(next_result.Value().value.Low() > requested_result.Value().value.Low());

    // Custom blend failures, invalid outputs and C++ exceptions are returned as controlled failures.
    const auto custom_id = EnvironmentBlendHandlerId::FromString("game.blend.custom");
    EnvironmentService custom = MakeService(weather, fog, toxic);
    CHECK(custom.RegisterBlendHandler(
        custom_id, "game.blend.custom",
        [](const EnvironmentValues &, const EnvironmentValues &) {
            EnvironmentValues values;
            values.humidity = kEnvironmentOne + 1;
            return epidemic::foundation::Result<EnvironmentValues>::Success(values);
        }));
    custom.Freeze();
    EnvironmentLayer custom_layer;
    custom_layer.type = weather;
    custom_layer.blend = EnvironmentBlendPolicy::CustomRegistered;
    custom_layer.custom_blend = custom_id;
    CHECK(custom.AddLayer(custom_layer));
    CHECK(!custom.Sample({}, GameplayTimePoint{}));
    CHECK(custom.GetDiagnostics().blend_failures == 1);

    const auto throwing_id = EnvironmentBlendHandlerId::FromString("game.blend.throwing");
    EnvironmentService throwing = MakeService(weather, fog, toxic);
    CHECK(throwing.RegisterBlendHandler(
        throwing_id, "game.blend.throwing",
        [](const EnvironmentValues &, const EnvironmentValues &) -> epidemic::foundation::Result<EnvironmentValues> {
            throw std::runtime_error("boom");
        }));
    throwing.Freeze();
    EnvironmentLayer throwing_layer;
    throwing_layer.type = weather;
    throwing_layer.blend = EnvironmentBlendPolicy::CustomRegistered;
    throwing_layer.custom_blend = throwing_id;
    CHECK(throwing.AddLayer(throwing_layer));
    CHECK(!throwing.Sample({}, GameplayTimePoint{}));
    CHECK(throwing.GetDiagnostics().blend_failures == 1);

    const auto pre_restore_cursor = environment.LatestChangeCursor();
    CHECK(pre_restore_cursor.sequence >= 2);
    const auto snapshot = environment.CaptureSnapshot();
    CHECK(snapshot.layers.size() == 1); // only the persistent base layer

    EnvironmentService restored = MakeService(weather, fog, toxic);
    restored.Freeze();
    CHECK(restored.RestoreSnapshot(snapshot));
    CHECK(restored.GetLayer(base_id.Value()).has_value());
    CHECK(restored.ReadChangesSince(pre_restore_cursor).snapshot_required);
    CHECK(restored.ReadChangesSince(restored.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required);
    EnvironmentLayer epoch_layer;
    epoch_layer.type = weather;
    CHECK(restored.AddLayer(epoch_layer));
    CHECK(restored.ReadChangesSince(pre_restore_cursor).snapshot_required);
    const auto environment_epoch = restored.ReadChangesSince(ChangeCursor{});
    CHECK(!environment_epoch.snapshot_required && !environment_epoch.changes.empty());
    const auto environment_current = restored.ReadChangesSince(environment_epoch.latest_cursor);
    CHECK(!environment_current.snapshot_required && environment_current.changes.empty());

    // Restore validates the entire snapshot before replacing live state.
    auto corrupt = snapshot;
    corrupt.layers.push_back(corrupt.layers.front());
    const auto before_corrupt_revision = restored.CurrentRevision();
    CHECK(!restored.RestoreSnapshot(corrupt));
    CHECK(restored.CurrentRevision() == before_corrupt_revision && restored.GetLayer(base_id.Value()).has_value());

    auto bad_generator = snapshot;
    bad_generator.ids.next = snapshot.layers.front().id.value.Low();
    CHECK(!restored.RestoreSnapshot(bad_generator));
    CHECK(restored.CurrentRevision() == before_corrupt_revision);

    // Journal is bounded and reports when a consumer fell behind retention.
    EnvironmentService journal = MakeService(weather, fog, toxic);
    journal.Freeze();
    for (std::size_t i = 0; i < 5000; ++i)
    {
        EnvironmentLayer layer;
        layer.type = weather;
        const auto id = journal.AddLayer(layer);
        CHECK(id);
        CHECK(journal.RemoveLayer(id.Value()));
    }
    const auto diagnostics = journal.GetDiagnostics();
    CHECK(diagnostics.changes <= 4096);
    const auto stale_batch = journal.ReadChangesSince(ChangeCursor{});
    CHECK(stale_batch.snapshot_required);
    CHECK(journal.ReadChangesSince(journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required);
    const auto current_batch = journal.ReadChangesSince(journal.LatestChangeCursor());
    CHECK(!current_batch.snapshot_required && current_batch.changes.empty());

    // Milestone 2: RestoreSnapshot preserves live state at every allocation failure.
    const auto allocation_before = restored.CaptureSnapshot();
    bool saw_restore_allocation_failure = false;
    for (long long fail_after = 0; fail_after < 32; ++fail_after)
    {
        auto allocation_target = allocation_before;
        bool failed = false;
        try
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            const auto restored_under_fault = restored.RestoreSnapshot(std::move(allocation_target));
            failed = !restored_under_fault;
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        if (!failed)
            break;
        saw_restore_allocation_failure = true;
        if (restored.CaptureSnapshot().revision != allocation_before.revision)
            return 907;
    }
    if (!saw_restore_allocation_failure)
        return 908;
    return 0;
}
