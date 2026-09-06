#include "Epidemic/GameFramework/Environment/environment.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::environment
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

EnvironmentFixed Saturate(std::int64_t value)
{
    return static_cast<EnvironmentFixed>(std::clamp<std::int64_t>(
        value, std::numeric_limits<EnvironmentFixed>::min(), std::numeric_limits<EnvironmentFixed>::max()));
}

bool SameHazard(const EnvironmentHazard &a, const EnvironmentHazard &b)
{
    return a.type == b.type && a.intensity == b.intensity && a.tags.Values() == b.tags.Values();
}

bool SameContext(const GameplayContext &a, const GameplayContext &b)
{
    return a.tick == b.tick && a.time == b.time && a.operation == b.operation && a.correlation == b.correlation &&
           a.actor == b.actor && a.instigator == b.instigator && a.source == b.source &&
           a.parent_operation == b.parent_operation && a.cause_event == b.cause_event;
}

bool SameLayer(const EnvironmentLayer &a, const EnvironmentLayer &b)
{
    return a.id == b.id && a.type == b.type && a.bounds == b.bounds && a.area_ref == b.area_ref &&
           a.priority == b.priority && a.blend == b.blend && a.custom_blend == b.custom_blend &&
           a.values == b.values && a.tags.Values() == b.tags.Values() && a.created_at == b.created_at &&
           a.expires_at == b.expires_at && a.persistence == b.persistence && SameContext(a.context, b.context) &&
           a.hazards.size() == b.hazards.size() &&
           std::equal(a.hazards.begin(), a.hazards.end(), b.hazards.begin(), SameHazard);
}

std::int64_t FloorDiv(std::int64_t value, std::int64_t divisor) noexcept
{
    const auto quotient = value / divisor;
    const auto remainder = value % divisor;
    return (remainder != 0 && ((remainder < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

template <typename TValue> void InsertSortedUnique(std::vector<TValue> &values, TValue value)
{
    const auto position = std::lower_bound(values.begin(), values.end(), value);
    if (position == values.end() || *position != value)
        values.insert(position, value);
}

template <typename TValue> void EraseSorted(std::vector<TValue> &values, TValue value) noexcept
{
    const auto position = std::lower_bound(values.begin(), values.end(), value);
    if (position != values.end() && *position == value)
        values.erase(position);
}
} // namespace

EnvironmentService::EnvironmentService()
    : ids_(GameplayObjectId::FromString("framework.environment.layer.seed").High())
{
}

EnvironmentService::CellKey EnvironmentService::CellFor(EnvironmentPosition position) const noexcept
{
    return {FloorDiv(position.x_mm, kSpatialCellMm), FloorDiv(position.y_mm, kSpatialCellMm),
            FloorDiv(position.z_mm, kSpatialCellMm)};
}

std::vector<EnvironmentService::CellKey> EnvironmentService::CellsFor(const EnvironmentAabb &bounds) const
{
    const auto low = CellFor(bounds.min);
    const auto high = CellFor(bounds.max);
    if (high.x < low.x || high.y < low.y || high.z < low.z)
        return {};

    const auto nx = static_cast<std::uint64_t>(high.x - low.x + 1);
    const auto ny = static_cast<std::uint64_t>(high.y - low.y + 1);
    const auto nz = static_cast<std::uint64_t>(high.z - low.z + 1);
    if (nx == 0 || ny == 0 || nz == 0 || nx > kMaxIndexedCells || ny > kMaxIndexedCells ||
        nz > kMaxIndexedCells || nx > kMaxIndexedCells / ny || nx * ny > kMaxIndexedCells / nz)
        return {};

    std::vector<CellKey> cells;
    cells.reserve(static_cast<std::size_t>(nx * ny * nz));
    for (auto x = low.x; x <= high.x; ++x)
        for (auto y = low.y; y <= high.y; ++y)
            for (auto z = low.z; z <= high.z; ++z)
                cells.push_back({x, y, z});
    return cells;
}

void EnvironmentService::IndexLayer(const EnvironmentLayer &layer)
{
    if (!layer.bounds)
    {
        InsertSortedUnique(global_layers_, layer.id);
        return;
    }

    auto cells = CellsFor(*layer.bounds);
    if (cells.empty())
    {
        InsertSortedUnique(large_layers_, layer.id);
        return;
    }

    for (const auto &cell : cells)
        InsertSortedUnique(spatial_index_[cell], layer.id);
}

void EnvironmentService::UnindexLayer(const EnvironmentLayer &layer) noexcept
{
    if (!layer.bounds)
    {
        EraseSorted(global_layers_, layer.id);
        return;
    }

    const auto cells = CellsFor(*layer.bounds);
    if (cells.empty())
    {
        EraseSorted(large_layers_, layer.id);
        return;
    }

    for (const auto &cell : cells)
    {
        const auto found = spatial_index_.find(cell);
        if (found == spatial_index_.end())
            continue;
        EraseSorted(found->second, layer.id);
        if (found->second.empty())
            spatial_index_.erase(found);
    }
}

void EnvironmentService::RebuildSpatialIndex()
{
    spatial_index_.clear();
    global_layers_.clear();
    large_layers_.clear();

    std::vector<EnvironmentLayerId> ids;
    ids.reserve(layers_.size());
    for (const auto &[id, _] : layers_)
        ids.push_back(id);
    std::sort(ids.begin(), ids.end());

    for (const auto id : ids)
        IndexLayer(layers_.at(id));
}

foundation::Result<Revision> EnvironmentService::PrepareRevision() const
{
    const auto next = CheckedNext(revision_);
    if (!next)
        return foundation::Result<Revision>::Failure(
            Error("gameplay.revision_exhausted", "environment revision counter is exhausted"));
    return foundation::Result<Revision>::Success(*next);
}

bool EnvironmentService::CanRecordChanges(std::size_t count) const noexcept
{
    if (count == 0)
        return true;
    if (next_change_sequence_ == 0)
        return false;
    return count - 1 <= std::numeric_limits<std::uint64_t>::max() - next_change_sequence_;
}

void EnvironmentService::Record(EnvironmentChange change) noexcept
{
    change.sequence = next_change_sequence_;
    last_change_sequence_ = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;

    changes_.push_back(std::move(change));
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}

foundation::Result<void> EnvironmentService::ValidateValues(const EnvironmentValues &values) const
{
    if ((values.present & ~kEnvironmentAllValues) != 0)
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.invalid_values", "environment value mask contains unknown fields"));

    const auto normalized = [](EnvironmentFixed value) { return value >= 0 && value <= kEnvironmentOne; };
    if (!normalized(values.humidity) || !normalized(values.precipitation) || !normalized(values.visibility) ||
        !normalized(values.light_exposure) || values.wind_strength < 0 || values.wind_x < -kEnvironmentOne ||
        values.wind_x > kEnvironmentOne || values.wind_y < -kEnvironmentOne || values.wind_y > kEnvironmentOne ||
        values.wind_z < -kEnvironmentOne || values.wind_z > kEnvironmentOne)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.invalid_values", "environment values are outside supported ranges"));
    }

    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::ValidateLayer(const EnvironmentLayer &layer) const
{
    if (!layer.id.IsValid() || !layer.type.IsValid() || !types_.contains(layer.type) ||
        (layer.bounds && !layer.bounds->IsValid()) ||
        (layer.blend == EnvironmentBlendPolicy::CustomRegistered && !blend_handlers_.contains(layer.custom_blend)))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.invalid_layer", "invalid environment layer"));
    }

    if (layer.expires_at && layer.expires_at->ticks <= layer.created_at.ticks)
        return foundation::Result<void>::Failure(Error("gameplay.environment.invalid_time_range",
                                                        "environment layer expiration must be later than creation"));

    if (auto values = ValidateValues(layer.values); !values)
        return values;

    std::unordered_set<EnvironmentHazardTypeId, HazardHash> seen_hazards;
    for (const auto &hazard : layer.hazards)
    {
        if (!hazard.type.IsValid() || !hazard_types_.contains(hazard.type) || hazard.intensity < 0 ||
            !seen_hazards.insert(hazard.type).second)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.invalid_hazard", "environment hazard is invalid or duplicated"));
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::RegisterLayerType(EnvironmentLayerTypeId id, std::string canonical_name)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "environment registry is frozen"));
    if (!id.IsValid() || canonical_name.empty() || types_.contains(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.invalid_type", "invalid or duplicate environment layer type"));
    types_.emplace(id, std::move(canonical_name));
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::RegisterBlendHandler(EnvironmentBlendHandlerId id,
                                                                  std::string canonical_name, BlendHandler handler)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "environment registry is frozen"));
    if (!id.IsValid() || canonical_name.empty() || !handler || blend_handlers_.contains(id))
        return foundation::Result<void>::Failure(Error("gameplay.environment.invalid_blend_handler",
                                                        "invalid or duplicate environment blend handler"));
    blend_handlers_.emplace(id, std::make_pair(std::move(canonical_name), std::move(handler)));
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::RegisterHazardType(EnvironmentHazardTypeId id,
                                                                 std::string canonical_name)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "environment registry is frozen"));
    if (!id.IsValid() || canonical_name.empty() || hazard_types_.contains(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.invalid_hazard_type", "invalid or duplicate environment hazard type"));
    hazard_types_.emplace(id, std::move(canonical_name));
    return foundation::Result<void>::Success();
}

void EnvironmentService::AdvanceGeneratorPast(EnvironmentLayerId id) noexcept
{
    const auto snapshot = ids_.GetSnapshot();
    if (!id.IsValid() || id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;

    auto advanced = snapshot;
    advanced.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    ids_.Restore(advanced);
}

foundation::Result<EnvironmentLayerId> EnvironmentService::AddLayer(EnvironmentLayer layer)
{
    if (!frozen_)
        return foundation::Result<EnvironmentLayerId>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before runtime operations"));

    const bool caller_supplied_id = layer.id.IsValid();
    if (!caller_supplied_id)
        layer.id = EnvironmentLayerId{ids_.Next()};
    if (!layer.id.IsValid())
        return foundation::Result<EnvironmentLayerId>::Failure(
            Error("gameplay.environment.layer_id_exhausted", "environment layer id generator is exhausted"));
    if (layers_.contains(layer.id))
        return foundation::Result<EnvironmentLayerId>::Failure(
            Error("gameplay.environment.invalid_layer", "duplicate environment layer"));
    if (auto valid = ValidateLayer(layer); !valid)
        return foundation::Result<EnvironmentLayerId>::Failure(valid.GetError());
    if (!CanRecordChanges(1))
        return foundation::Result<EnvironmentLayerId>::Failure(
            Error("gameplay.change_sequence_exhausted", "environment change sequence is exhausted"));

    auto revision = PrepareRevision();
    if (!revision)
        return foundation::Result<EnvironmentLayerId>::Failure(revision.GetError());

    revision_ = revision.Value();
    layer.revision = revision_;
    layers_.emplace(layer.id, layer);
    IndexLayer(layer);
    if (caller_supplied_id)
        AdvanceGeneratorPast(layer.id);
    Record({0, EnvironmentChangeKind::Added, layer.id, revision_, layer.context});
    return foundation::Result<EnvironmentLayerId>::Success(layer.id);
}

foundation::Result<void> EnvironmentService::UpdateLayer(EnvironmentLayer layer)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before runtime operations"));

    auto found = layers_.find(layer.id);
    if (found == layers_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.layer_missing", "invalid environment layer update"));
    if (auto valid = ValidateLayer(layer); !valid)
        return valid;
    if (SameLayer(found->second, layer))
        return foundation::Result<void>::Success();
    if (!CanRecordChanges(1))
        return foundation::Result<void>::Failure(
            Error("gameplay.change_sequence_exhausted", "environment change sequence is exhausted"));

    auto revision = PrepareRevision();
    if (!revision)
        return foundation::Result<void>::Failure(revision.GetError());

    const auto old_layer = found->second;
    revision_ = revision.Value();
    layer.revision = revision_;
    UnindexLayer(old_layer);
    found->second = std::move(layer);
    IndexLayer(found->second);
    Record({0, EnvironmentChangeKind::Changed, found->first, revision_, found->second.context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::RemoveLayer(EnvironmentLayerId id, GameplayContext context)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before runtime operations"));
    const auto found = layers_.find(id);
    if (found == layers_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.layer_missing", "environment layer missing"));
    if (!CanRecordChanges(1))
        return foundation::Result<void>::Failure(
            Error("gameplay.change_sequence_exhausted", "environment change sequence is exhausted"));

    auto revision = PrepareRevision();
    if (!revision)
        return foundation::Result<void>::Failure(revision.GetError());

    const auto removed = found->second;
    revision_ = revision.Value();
    UnindexLayer(removed);
    layers_.erase(found);
    Record({0, EnvironmentChangeKind::Removed, id, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::ExpireLayer(EnvironmentLayerId id, GameplayTimePoint now,
                                                         GameplayContext context)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before runtime operations"));
    const auto found = layers_.find(id);
    if (found == layers_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.layer_missing", "environment layer missing"));
    if (!found->second.expires_at || found->second.expires_at->ticks > now.ticks)
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.layer_not_due", "environment layer is not due to expire"));
    if (!CanRecordChanges(1))
        return foundation::Result<void>::Failure(
            Error("gameplay.change_sequence_exhausted", "environment change sequence is exhausted"));

    auto revision = PrepareRevision();
    if (!revision)
        return foundation::Result<void>::Failure(revision.GetError());

    const auto removed = found->second;
    revision_ = revision.Value();
    UnindexLayer(removed);
    layers_.erase(found);
    Record({0, EnvironmentChangeKind::Expired, id, revision_, context});
    if (expired_ != std::numeric_limits<std::uint64_t>::max())
        ++expired_;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EnvironmentService::SweepExpired(GameplayTimePoint now, GameplayContext context)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before runtime operations"));

    std::vector<EnvironmentLayerId> ids;
    for (const auto &[id, layer] : layers_)
        if (layer.expires_at && layer.expires_at->ticks <= now.ticks)
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    if (ids.empty())
        return foundation::Result<void>::Success();
    if (!CanRecordChanges(ids.size()))
        return foundation::Result<void>::Failure(
            Error("gameplay.change_sequence_exhausted", "environment change sequence is exhausted"));

    auto revision = PrepareRevision();
    if (!revision)
        return foundation::Result<void>::Failure(revision.GetError());

    revision_ = revision.Value();
    for (const auto id : ids)
    {
        const auto found = layers_.find(id);
        if (found == layers_.end())
            continue;
        const auto removed = found->second;
        UnindexLayer(removed);
        layers_.erase(found);
        Record({0, EnvironmentChangeKind::Expired, id, revision_, context});
        if (expired_ != std::numeric_limits<std::uint64_t>::max())
            ++expired_;
    }
    return foundation::Result<void>::Success();
}

std::optional<EnvironmentLayer> EnvironmentService::GetLayer(EnvironmentLayerId id) const noexcept
{
    const auto found = layers_.find(id);
    return found == layers_.end() ? std::nullopt : std::optional<EnvironmentLayer>{found->second};
}

std::vector<EnvironmentLayer> EnvironmentService::FindLayers(EnvironmentPosition position) const
{
    std::unordered_set<EnvironmentLayerId, IdHash> ids(global_layers_.begin(), global_layers_.end());
    ids.insert(large_layers_.begin(), large_layers_.end());
    const auto cell = CellFor(position);
    if (const auto found = spatial_index_.find(cell); found != spatial_index_.end())
        ids.insert(found->second.begin(), found->second.end());

    std::vector<EnvironmentLayer> result;
    result.reserve(ids.size());
    for (const auto id : ids)
    {
        const auto &layer = layers_.at(id);
        if (!layer.bounds || layer.bounds->Contains(position))
            result.push_back(layer);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.id < b.id;
    });
    return result;
}

foundation::Result<void> EnvironmentService::Blend(EnvironmentValues &base, const EnvironmentLayer &layer) const
{
    const auto &added = layer.values;
    if (layer.blend == EnvironmentBlendPolicy::CustomRegistered)
    {
        const auto handler = blend_handlers_.find(layer.custom_blend);
        if (handler == blend_handlers_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.blend_handler_missing", "environment blend handler is missing"));

        try
        {
            auto blended = handler->second.second(base, added);
            if (!blended)
            {
                if (blend_failures_ != std::numeric_limits<std::uint64_t>::max())
                    ++blend_failures_;
                return foundation::Result<void>::Failure(blended.GetError());
            }
            if (auto valid = ValidateValues(blended.Value()); !valid)
            {
                if (blend_failures_ != std::numeric_limits<std::uint64_t>::max())
                    ++blend_failures_;
                return foundation::Result<void>::Failure(
                    Error("gameplay.environment.invalid_blend_result", "custom environment blend returned invalid values"));
            }
            base = std::move(blended).Value();
            return foundation::Result<void>::Success();
        }
        catch (const std::exception &)
        {
            if (blend_failures_ != std::numeric_limits<std::uint64_t>::max())
                ++blend_failures_;
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.blend_exception", "custom environment blend handler threw an exception"));
        }
        catch (...)
        {
            if (blend_failures_ != std::numeric_limits<std::uint64_t>::max())
                ++blend_failures_;
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.blend_exception", "custom environment blend handler threw an exception"));
        }
    }

    const auto apply = [policy = layer.blend](EnvironmentFixed current, EnvironmentFixed value) {
        switch (policy)
        {
        case EnvironmentBlendPolicy::Override:
            return value;
        case EnvironmentBlendPolicy::Add:
            return Saturate(static_cast<std::int64_t>(current) + value);
        case EnvironmentBlendPolicy::Multiply:
            return Saturate((static_cast<std::int64_t>(current) * value) / kEnvironmentOne);
        case EnvironmentBlendPolicy::Min:
            return std::min(current, value);
        case EnvironmentBlendPolicy::Max:
            return std::max(current, value);
        case EnvironmentBlendPolicy::CustomRegistered:
            return value;
        }
        return value;
    };
    const auto has = [&](EnvironmentValueField field) {
        return (added.present & static_cast<EnvironmentValueMask>(field)) != 0;
    };

    if (has(EnvironmentValueField::Temperature))
        base.temperature_milli_c = apply(base.temperature_milli_c, added.temperature_milli_c);
    if (has(EnvironmentValueField::Humidity))
        base.humidity = apply(base.humidity, added.humidity);
    if (has(EnvironmentValueField::Precipitation))
        base.precipitation = apply(base.precipitation, added.precipitation);
    if (has(EnvironmentValueField::WindStrength))
        base.wind_strength = apply(base.wind_strength, added.wind_strength);
    if (has(EnvironmentValueField::WindDirection))
    {
        base.wind_x = apply(base.wind_x, added.wind_x);
        base.wind_y = apply(base.wind_y, added.wind_y);
        base.wind_z = apply(base.wind_z, added.wind_z);
    }
    if (has(EnvironmentValueField::Visibility))
        base.visibility = apply(base.visibility, added.visibility);
    if (has(EnvironmentValueField::LightExposure))
        base.light_exposure = apply(base.light_exposure, added.light_exposure);

    return foundation::Result<void>::Success();
}

foundation::Result<EnvironmentSample> EnvironmentService::Sample(EnvironmentPosition position, GameplayTimePoint time,
                                                                 std::span<const GameplayObjectRef> scopes) const
{
    if (samples_ != std::numeric_limits<std::uint64_t>::max())
        ++samples_;

    EnvironmentSample sample;
    sample.position = position;
    sample.time = time;
    sample.revision = revision_;

    const auto layers = FindLayers(position);
    for (const auto &layer : layers)
    {
        if (layer.created_at.ticks > time.ticks || (layer.expires_at && layer.expires_at->ticks <= time.ticks))
            continue;
        if (layer.area_ref.IsValid() && std::find(scopes.begin(), scopes.end(), layer.area_ref) == scopes.end())
            continue;

        if (auto blended = Blend(sample.values, layer); !blended)
            return foundation::Result<EnvironmentSample>::Failure(blended.GetError());
        for (const auto tag : layer.tags.Values())
            sample.tags.Add(tag);
        for (const auto &hazard : layer.hazards)
            sample.hazards.push_back(EnvironmentSampleHazard{layer.id, hazard});
        sample.layers.push_back(layer.id);
    }

    std::sort(sample.hazards.begin(), sample.hazards.end(), [](const auto &a, const auto &b) {
        if (a.hazard.type != b.hazard.type)
            return a.hazard.type < b.hazard.type;
        if (a.source_layer != b.source_layer)
            return a.source_layer < b.source_layer;
        return a.hazard.intensity < b.hazard.intensity;
    });
    return foundation::Result<EnvironmentSample>::Success(std::move(sample));
}

EnvironmentSnapshot EnvironmentService::CaptureSnapshot() const
{
    EnvironmentSnapshot snapshot;
    for (const auto &[_, layer] : layers_)
        if (layer.persistence == EnvironmentPersistence::Persistent)
            snapshot.layers.push_back(layer);
    std::sort(snapshot.layers.begin(), snapshot.layers.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.ids = ids_.GetSnapshot();
    snapshot.revision = revision_;
    return snapshot;
}

foundation::Result<void> EnvironmentService::RestoreSnapshot(EnvironmentSnapshot snapshot)
{
    if (!frozen_)
        return foundation::Result<void>::Failure(
            Error("gameplay.registry_not_frozen", "environment service must be frozen before restore"));

    const auto expected_scope = ids_.GetSnapshot().scope;
    if (!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot.ids) || snapshot.ids.scope != expected_scope)
        return foundation::Result<void>::Failure(
            Error("gameplay.environment.restore_invalid", "invalid environment id generator snapshot"));

    std::unordered_map<EnvironmentLayerId, EnvironmentLayer, IdHash> rebuilt;
    std::uint64_t max_low = 0;
    for (auto &layer : snapshot.layers)
    {
        if (layer.persistence != EnvironmentPersistence::Persistent || layer.revision > snapshot.revision)
            return foundation::Result<void>::Failure(Error(
                "gameplay.environment.restore_invalid", "snapshot contains non-persistent or future-revision layer"));
        if (auto valid = ValidateLayer(layer); !valid)
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.restore_invalid", "invalid layer in snapshot"));
        if (!rebuilt.emplace(layer.id, layer).second)
            return foundation::Result<void>::Failure(
                Error("gameplay.environment.restore_invalid", "duplicate layer in snapshot"));
        if (layer.id.value.High() == snapshot.ids.scope && layer.id.value.Low() > max_low)
            max_low = layer.id.value.Low();
    }

    if (snapshot.ids.next != 0 && snapshot.ids.next <= max_low)
        return foundation::Result<void>::Failure(Error(
            "gameplay.environment.restore_invalid", "environment id generator can reproduce restored ids"));

    layers_ = std::move(rebuilt);
    ids_.Restore(snapshot.ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    last_change_sequence_ = 0;
    RebuildSpatialIndex();
    return foundation::Result<void>::Success();
}

std::vector<EnvironmentChange> EnvironmentService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}

EnvironmentChangeBatch EnvironmentService::ReadChangesSince(std::uint64_t sequence) const
{
    EnvironmentChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && sequence < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }

    const auto found = std::upper_bound(changes_.begin(), changes_.end(), sequence,
                                        [](std::uint64_t value, const EnvironmentChange &change) {
                                            return value < change.sequence;
                                        });
    batch.changes.assign(found, changes_.end());
    return batch;
}

EnvironmentDiagnostics EnvironmentService::GetDiagnostics() const noexcept
{
    EnvironmentDiagnostics diagnostics;
    diagnostics.layers = layers_.size();
    for (const auto &[_, layer] : layers_)
        if (layer.persistence == EnvironmentPersistence::Persistent)
            ++diagnostics.persistent_layers;
    diagnostics.samples = samples_;
    diagnostics.expired = expired_;
    diagnostics.changes = changes_.size();
    diagnostics.blend_failures = blend_failures_;
    return diagnostics;
}
} // namespace epidemic::gameplay::environment
