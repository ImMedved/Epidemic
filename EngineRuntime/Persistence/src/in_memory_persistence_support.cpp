#include "in_memory_persistence_support.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> PersistenceFailure(std::string_view code, std::string_view message)
{
    return foundation::Result<void>::Failure(foundation::Error::Create(code, message));
}

[[nodiscard]] bool IsValidLocation(const PersistenceLocation& location) noexcept
{
    return location.region_id.IsValid() || location.chunk_id.IsValid() || location.location_tag.IsValid();
}

[[nodiscard]] bool IsValidGameTime(GameTimePoint time) noexcept
{
    return time.ticks >= 0;
}

[[nodiscard]] constexpr bool IsValidPersistentObjectKind(PersistentObjectKind kind) noexcept
{
    switch (kind)
    {
    case PersistentObjectKind::Unknown:
    case PersistentObjectKind::Object:
    case PersistentObjectKind::ContainerEntry:
    case PersistentObjectKind::SurfaceState:
    case PersistentObjectKind::ZoneOverride:
    case PersistentObjectKind::AbstractFact:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidPersistenceTierValue(PersistenceTier tier) noexcept
{
    switch (tier)
    {
    case PersistenceTier::Disposable:
    case PersistenceTier::TemporaryObserved:
    case PersistenceTier::PlayerTouched:
    case PersistenceTier::Protected:
    case PersistenceTier::QuestCritical:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidPersistenceStateValue(PersistenceState state) noexcept
{
    switch (state)
    {
    case PersistenceState::Clean:
    case PersistenceState::Dirty:
    case PersistenceState::PendingSave:
    case PersistenceState::Saving:
    case PersistenceState::Saved:
    case PersistenceState::LoadPending:
    case PersistenceState::Loaded:
    case PersistenceState::Deleted:
    case PersistenceState::Tombstoned:
    case PersistenceState::Conflict:
    case PersistenceState::Corrupted:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidLazyRuleKindValue(LazyRuleKind kind) noexcept
{
    switch (kind)
    {
    case LazyRuleKind::Decay:
    case LazyRuleKind::Theft:
    case LazyRuleKind::Cleanup:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidLazyRuleStateValue(LazyRuleState state) noexcept
{
    switch (state)
    {
    case LazyRuleState::Pending:
    case LazyRuleState::Evaluated:
    case LazyRuleState::Applied:
    case LazyRuleState::Cancelled:
    case LazyRuleState::Expired:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool HasOnlyKnownProtectionBits(ObjectProtectionMask mask) noexcept
{
    constexpr std::uint32_t kKnown = ToProtectionMask(ObjectProtectionFlags::PreventTheft) |
                                     ToProtectionMask(ObjectProtectionFlags::PreventDecay) |
                                     ToProtectionMask(ObjectProtectionFlags::PreventCleanup) |
                                     ToProtectionMask(ObjectProtectionFlags::PreserveTransform) |
                                     ToProtectionMask(ObjectProtectionFlags::PreserveCondition);
    return (mask.value & ~kKnown) == 0u;
}

[[nodiscard]] foundation::Result<void> ValidateObject(const PersistentObjectRecord& record)
{
    if (!record.persistent_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "persistent object id must be valid");
    }
    if (!record.payload.IsValid())
    {
        return PersistenceFailure("persistence.invalid_payload", "persistent object payload must declare schema and bytes");
    }
    if (!IsValidPersistentObjectKind(record.kind) || !IsValidPersistenceTierValue(record.tier) ||
        !IsValidPersistenceStateValue(record.state))
    {
        return PersistenceFailure("persistence.invalid_enum", "persistent object contains an out-of-domain enum value");
    }
    if (!HasOnlyKnownProtectionBits(record.protection_flags))
    {
        return PersistenceFailure("persistence.invalid_protection_mask", "persistent object protection mask contains unknown bits");
    }
    if (!IsValidGameTime(record.created_game_time) || !IsValidGameTime(record.last_observed_game_time))
    {
        return PersistenceFailure("persistence.invalid_game_time", "persistent object game time must be non-negative");
    }
    if (record.last_observed_game_time < record.created_game_time)
    {
        return PersistenceFailure("persistence.invalid_game_time", "persistent object observation time must not precede creation time");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateLazyRule(const LazyRuleRecord& record)
{
    if (!record.rule_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_id", "lazy rule id must be valid");
    }
    if (!record.target_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "lazy rule target id must be valid");
    }
    if (!IsValidLazyRuleKindValue(record.kind) || !IsValidLazyRuleStateValue(record.state))
    {
        return PersistenceFailure("persistence.invalid_enum", "lazy rule contains an out-of-domain enum value");
    }
    if (!IsValidGameTime(record.created_game_time) || !IsValidGameTime(record.evaluate_after_game_time))
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_time", "lazy rule game time must be non-negative");
    }
    if (record.evaluate_after_game_time < record.created_game_time)
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_time", "lazy rule evaluation time must not precede creation time");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateTombstone(const TombstoneRecord& tombstone)
{
    if (!tombstone.persistent_id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "tombstone object id must be valid");
    }
    if (!IsValidGameTime(tombstone.deleted_game_time))
    {
        return PersistenceFailure("persistence.invalid_game_time", "tombstone deletion time must be non-negative");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] foundation::Result<void> ValidateZoneOverride(const ZoneOverrideSnapshot& snapshot)
{
    if (!IsValidLocation(snapshot.location))
    {
        return PersistenceFailure("persistence.invalid_location", "zone override location must contain at least one valid component");
    }
    return foundation::Result<void>::Success();
}

[[nodiscard]] bool LocationLess(const PersistenceLocation& left, const PersistenceLocation& right) noexcept
{
    if (left.region_id.Raw() != right.region_id.Raw())
    {
        return left.region_id.Raw() < right.region_id.Raw();
    }
    if (left.chunk_id.Raw() != right.chunk_id.Raw())
    {
        return left.chunk_id.Raw() < right.chunk_id.Raw();
    }
    return left.location_tag.Raw() < right.location_tag.Raw();
}

void SortZoneOverrideSnapshot(ZoneOverrideSnapshot& snapshot)
{
    std::sort(snapshot.record_ids.begin(), snapshot.record_ids.end(), [](PersistentObjectId left, PersistentObjectId right) {
        return left.Raw() < right.Raw();
    });
    std::sort(snapshot.tombstones.begin(), snapshot.tombstones.end(), [](const TombstoneRecord& left, const TombstoneRecord& right) {
        return left.persistent_id.Raw() < right.persistent_id.Raw();
    });
    std::sort(snapshot.lazy_rules.begin(), snapshot.lazy_rules.end(), [](const LazyRuleRecord& left, const LazyRuleRecord& right) {
        return left.rule_id.Raw() < right.rule_id.Raw();
    });
}

void SortPersistenceSnapshot(PersistenceSnapshot& snapshot)
{
    std::sort(snapshot.objects.begin(), snapshot.objects.end(), [](const PersistentObjectRecord& left, const PersistentObjectRecord& right) {
        return left.persistent_id.Raw() < right.persistent_id.Raw();
    });
    std::sort(snapshot.tombstones.begin(), snapshot.tombstones.end(), [](const TombstoneRecord& left, const TombstoneRecord& right) {
        return left.persistent_id.Raw() < right.persistent_id.Raw();
    });
    std::sort(snapshot.lazy_rules.begin(), snapshot.lazy_rules.end(), [](const LazyRuleRecord& left, const LazyRuleRecord& right) {
        return left.rule_id.Raw() < right.rule_id.Raw();
    });
    for (ZoneOverrideSnapshot& zone : snapshot.zone_overrides)
    {
        SortZoneOverrideSnapshot(zone);
    }
    std::sort(snapshot.zone_overrides.begin(), snapshot.zone_overrides.end(), [](const ZoneOverrideSnapshot& left, const ZoneOverrideSnapshot& right) {
        return LocationLess(left.location, right.location);
    });
}

} // namespace

size_t PersistenceLocationHash::operator()(const PersistenceLocation& location) const noexcept
{
    size_t seed = std::hash<RegionId>{}(location.region_id);
    seed ^= std::hash<ChunkId>{}(location.chunk_id) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    seed ^= std::hash<std::uint64_t>{}(location.location_tag.Raw()) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    return seed;
}

InMemoryPersistenceStore::InMemoryPersistenceStore(PersistenceSnapshot snapshot)
{
    const auto valid = ValidatePersistenceSnapshot(snapshot);
    if (!valid)
    {
        return;
    }
    revision_ = snapshot.current_revision;
    for (PersistentObjectRecord record : snapshot.objects)
    {
        objects_[record.persistent_id] = std::move(record);
    }
    for (TombstoneRecord tombstone : snapshot.tombstones)
    {
        tombstones_[tombstone.persistent_id] = std::move(tombstone);
    }
    for (LazyRuleRecord rule : snapshot.lazy_rules)
    {
        lazy_rules_[rule.rule_id] = std::move(rule);
    }
    for (ZoneOverrideSnapshot zone : snapshot.zone_overrides)
    {
        zone_overrides_[zone.location] = std::move(zone);
    }
}

InMemoryPersistenceStore::InMemoryPersistenceStore(PersistenceSnapshot snapshot, std::shared_ptr<IPersistenceBackend> backend,
                                                   PersistenceDurability durability)
    : InMemoryPersistenceStore(std::move(snapshot))
{
    backend_ = std::move(backend);
    durability_ = durability;
}

InMemorySaveTransaction::InMemorySaveTransaction(InMemoryPersistenceStore& store, PersistenceRevision base_revision)
    : store_(store), base_revision_(base_revision)
{
}

SaveTransactionState InMemorySaveTransaction::GetState() const
{
    return state_;
}

PersistenceRevision InMemorySaveTransaction::GetBaseRevision() const
{
    return base_revision_;
}

foundation::Result<void> InMemorySaveTransaction::EnsureOpen() const
{
    if (state_ != SaveTransactionState::Open)
    {
        return PersistenceFailure("persistence.transaction_invalid_state", "transaction accepts mutations only while open");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::StageOperation(PersistenceOperation operation)
{
    try
    {
        if (fail_next_operation_allocation_for_testing_)
        {
            fail_next_operation_allocation_for_testing_ = false;
            throw std::bad_alloc{};
        }
        operations_.push_back(std::move(operation));
    }
    catch (const std::bad_alloc&)
    {
        return PersistenceFailure("persistence.allocation_failed", "failed to stage persistence transaction operation");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemorySaveTransaction::UpsertObject(PersistentObjectRecord record)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateObject(record);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(UpsertObjectOperation{std::move(record)});
}

foundation::Result<void> InMemorySaveTransaction::AdminRemoveObject(PersistentObjectId id)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_id", "persistent object id must be valid before removal");
    }
    return StageOperation(AdminRemoveObjectOperation{id});
}

foundation::Result<void> InMemorySaveTransaction::DeleteObject(TombstoneRecord tombstone)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateTombstone(tombstone);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(DeleteObjectOperation{std::move(tombstone)});
}

foundation::Result<void> InMemorySaveTransaction::UpsertLazyRule(LazyRuleRecord record)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateLazyRule(record);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(UpsertLazyRuleOperation{std::move(record)});
}

foundation::Result<void> InMemorySaveTransaction::UpdateLazyRule(LazyRuleRecord record)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateLazyRule(record);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(UpdateLazyRuleOperation{std::move(record)});
}

foundation::Result<void> InMemorySaveTransaction::RemoveLazyRule(LazyRuleId id)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!id.IsValid())
    {
        return PersistenceFailure("persistence.invalid_lazy_rule_id", "lazy rule id must be valid before removal");
    }
    return StageOperation(RemoveLazyRuleOperation{id});
}

foundation::Result<void> InMemorySaveTransaction::AdminAddTombstone(TombstoneRecord tombstone)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateTombstone(tombstone);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(AdminAddTombstoneOperation{std::move(tombstone)});
}

foundation::Result<void> InMemorySaveTransaction::UpsertZoneOverride(ZoneOverrideSnapshot snapshot)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    const auto valid = ValidateZoneOverride(snapshot);
    if (!valid)
    {
        return valid;
    }
    return StageOperation(UpsertZoneOverrideOperation{std::move(snapshot)});
}

foundation::Result<void> InMemorySaveTransaction::RemoveZoneOverride(const PersistenceLocation& location)
{
    const auto open = EnsureOpen();
    if (!open)
    {
        return open;
    }
    if (!IsValidLocation(location))
    {
        return PersistenceFailure("persistence.invalid_location", "zone override removal location must contain at least one valid component");
    }
    return StageOperation(RemoveZoneOverrideOperation{location});
}

foundation::Result<void> InMemorySaveTransaction::Commit()
{
    if (state_ == SaveTransactionState::Committed)
    {
        return foundation::Result<void>::Success();
    }
    if (state_ != SaveTransactionState::Open)
    {
        return PersistenceFailure("persistence.transaction_invalid_state", "transaction can commit only while open");
    }

    state_ = SaveTransactionState::Committing;
    try
    {
        auto candidate = store_.BuildCandidateSnapshot(*this);
        if (!candidate)
        {
            state_ = SaveTransactionState::Failed;
            return foundation::Result<void>::Failure(candidate.GetError());
        }

        auto published = store_.PublishSnapshot(std::move(candidate.Value()));
        if (!published)
        {
            state_ = SaveTransactionState::Failed;
            return published;
        }
    }
    catch (const std::bad_alloc&)
    {
        state_ = SaveTransactionState::Failed;
        return PersistenceFailure("persistence.allocation_failed", "persistence commit allocation failed");
    }
    catch (...)
    {
        state_ = SaveTransactionState::Failed;
        return PersistenceFailure("persistence.backend_exception", "persistence commit dependency threw an exception");
    }

    state_ = SaveTransactionState::Committed;
    return foundation::Result<void>::Success();
}

void InMemorySaveTransaction::Rollback()
{
    if (state_ == SaveTransactionState::Open || state_ == SaveTransactionState::Failed)
    {
        operations_.clear();
        state_ = SaveTransactionState::RolledBack;
    }
}

std::optional<PersistentObjectRecord> InMemoryPersistenceStore::FindObject(PersistentObjectId id) const
{
    const auto iterator = objects_.find(id);
    if (iterator == objects_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<PersistentObjectRecord> InMemoryPersistenceStore::FindByLocation(const PersistenceLocation& location) const
{
    std::vector<PersistentObjectRecord> matches;
    for (const auto& [id, record] : objects_)
    {
        (void)id;
        if (record.location == location)
        {
            matches.push_back(record);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return matches;
}

std::vector<PersistentObjectRecord> InMemoryPersistenceStore::ListObjects() const
{
    std::vector<PersistentObjectRecord> records;
    records.reserve(objects_.size());
    for (const auto& [id, record] : objects_)
    {
        (void)id;
        records.push_back(record);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return records;
}

bool InMemoryPersistenceStore::IsDirty(PersistentObjectId id) const
{
    return dirty_ids_.contains(id);
}

std::vector<PersistentObjectId> InMemoryPersistenceStore::CollectDirty() const
{
    std::vector<PersistentObjectId> ids(dirty_ids_.begin(), dirty_ids_.end());
    std::sort(ids.begin(), ids.end(), [](const auto& left, const auto& right) { return left.Raw() < right.Raw(); });
    return ids;
}

std::optional<TombstoneRecord> InMemoryPersistenceStore::FindTombstone(PersistentObjectId id) const
{
    const auto iterator = tombstones_.find(id);
    if (iterator == tombstones_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

bool InMemoryPersistenceStore::IsTombstoned(PersistentObjectId id) const
{
    return tombstones_.contains(id);
}

std::vector<TombstoneRecord> InMemoryPersistenceStore::ListTombstones() const
{
    std::vector<TombstoneRecord> records;
    records.reserve(tombstones_.size());
    for (const auto& [id, tombstone] : tombstones_)
    {
        (void)id;
        records.push_back(tombstone);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.persistent_id.Raw() < right.persistent_id.Raw(); });
    return records;
}

std::optional<ZoneOverrideSnapshot> InMemoryPersistenceStore::FindZoneOverride(const PersistenceLocation& location) const
{
    const auto iterator = zone_overrides_.find(location);
    if (iterator == zone_overrides_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<ZoneOverrideSnapshot> InMemoryPersistenceStore::ListZoneOverrides() const
{
    std::vector<ZoneOverrideSnapshot> snapshots;
    snapshots.reserve(zone_overrides_.size());
    for (const auto& [location, snapshot] : zone_overrides_)
    {
        (void)location;
        snapshots.push_back(snapshot);
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
        if (left.location.region_id != right.location.region_id)
        {
            return left.location.region_id.Raw() < right.location.region_id.Raw();
        }
        if (left.location.chunk_id != right.location.chunk_id)
        {
            return left.location.chunk_id.Raw() < right.location.chunk_id.Raw();
        }
        return left.location.location_tag.Raw() < right.location.location_tag.Raw();
    });
    return snapshots;
}

std::optional<LazyRuleRecord> InMemoryPersistenceStore::FindLazyRule(LazyRuleId id) const
{
    const auto iterator = lazy_rules_.find(id);
    if (iterator == lazy_rules_.end())
    {
        return std::nullopt;
    }
    return iterator->second;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::FindLazyRules(PersistentObjectId target_id) const
{
    std::vector<LazyRuleRecord> matches;
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        if (rule.target_id == target_id)
        {
            matches.push_back(rule);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) { return left.rule_id.Raw() < right.rule_id.Raw(); });
    return matches;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::QueryDueLazyRules(GameTimePoint now) const
{
    std::vector<LazyRuleRecord> matches;
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        if (rule.state == LazyRuleState::Pending && rule.evaluate_after_game_time <= now)
        {
            matches.push_back(rule);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
        if (left.evaluate_after_game_time == right.evaluate_after_game_time)
        {
            return left.rule_id.Raw() < right.rule_id.Raw();
        }
        return left.evaluate_after_game_time < right.evaluate_after_game_time;
    });
    return matches;
}

std::vector<LazyRuleRecord> InMemoryPersistenceStore::ListLazyRules() const
{
    std::vector<LazyRuleRecord> records;
    records.reserve(lazy_rules_.size());
    for (const auto& [id, rule] : lazy_rules_)
    {
        (void)id;
        records.push_back(rule);
    }
    std::sort(records.begin(), records.end(), [](const auto& left, const auto& right) { return left.rule_id.Raw() < right.rule_id.Raw(); });
    return records;
}

PersistenceRevision InMemoryPersistenceStore::GetRevision() const
{
    return revision_;
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction()
{
    return OpenTransaction(revision_);
}

std::unique_ptr<ISaveTransaction> InMemoryPersistenceStore::OpenTransaction(PersistenceRevision base_revision)
{
    return std::make_unique<InMemorySaveTransaction>(*this, base_revision);
}

PersistenceSnapshot InMemoryPersistenceStore::CreateSnapshot() const
{
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = revision_;
    snapshot.objects = ListObjects();
    snapshot.tombstones = ListTombstones();
    snapshot.lazy_rules = ListLazyRules();
    snapshot.zone_overrides = ListZoneOverrides();
    return snapshot;
}

foundation::Result<void> ValidatePersistenceSnapshot(const PersistenceSnapshot& snapshot)
{
    std::unordered_set<PersistentObjectId> object_ids;
    std::unordered_set<PersistentObjectId> tombstone_ids;
    std::unordered_set<LazyRuleId> lazy_rule_ids;
    std::unordered_set<PersistenceLocation, PersistenceLocationHash> override_locations;

    for (const PersistentObjectRecord& record : snapshot.objects)
    {
        const auto valid = ValidateObject(record);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid persistent object");
        }
        if (record.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "object revision exceeds snapshot revision");
        }
        if (!object_ids.insert(record.persistent_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate object ids");
        }
    }

    for (const TombstoneRecord& tombstone : snapshot.tombstones)
    {
        const auto valid = ValidateTombstone(tombstone);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid tombstone");
        }
        if (tombstone.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "tombstone revision exceeds snapshot revision");
        }
        if (!tombstone_ids.insert(tombstone.persistent_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate tombstones");
        }
        if (object_ids.contains(tombstone.persistent_id))
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an active and tombstoned object");
        }
    }

    for (const LazyRuleRecord& rule : snapshot.lazy_rules)
    {
        const auto valid = ValidateLazyRule(rule);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid lazy rule");
        }
        if (rule.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "lazy rule revision exceeds snapshot revision");
        }
        if (!lazy_rule_ids.insert(rule.rule_id).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate lazy rule ids");
        }
        if (!object_ids.contains(rule.target_id))
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains a dangling lazy rule target");
        }
    }

    for (const ZoneOverrideSnapshot& zone : snapshot.zone_overrides)
    {
        const auto valid = ValidateZoneOverride(zone);
        if (!valid)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains an invalid zone override");
        }
        if (zone.revision > snapshot.current_revision)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "zone override revision exceeds snapshot revision");
        }
        if (!override_locations.insert(zone.location).second)
        {
            return PersistenceFailure("persistence.invalid_snapshot", "snapshot contains duplicate zone overrides");
        }

        std::unordered_set<PersistentObjectId> zone_record_ids;
        std::unordered_set<PersistentObjectId> zone_tombstone_ids;
        std::unordered_set<LazyRuleId> zone_lazy_rule_ids;
        for (const PersistentObjectId id : zone.record_ids)
        {
            if (!id.IsValid())
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains an invalid object id");
            }
            if (!zone_record_ids.insert(id).second)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains duplicate object ids");
            }
        }
        for (const TombstoneRecord& tombstone : zone.tombstones)
        {
            const auto tombstone_valid = ValidateTombstone(tombstone);
            if (!tombstone_valid)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains an invalid tombstone");
            }
            if (tombstone.revision > snapshot.current_revision)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override tombstone revision exceeds snapshot revision");
            }
            if (!zone_tombstone_ids.insert(tombstone.persistent_id).second)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains duplicate tombstones");
            }
            if (zone_record_ids.contains(tombstone.persistent_id))
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains an active and tombstoned object");
            }
        }
        for (const LazyRuleRecord& rule : zone.lazy_rules)
        {
            const auto rule_valid = ValidateLazyRule(rule);
            if (!rule_valid)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains an invalid lazy rule");
            }
            if (rule.revision > snapshot.current_revision)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override lazy rule revision exceeds snapshot revision");
            }
            if (!zone_lazy_rule_ids.insert(rule.rule_id).second)
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains duplicate lazy rule ids");
            }
            if (!zone_record_ids.contains(rule.target_id))
            {
                return PersistenceFailure("persistence.invalid_snapshot", "zone override contains a dangling lazy rule target");
            }
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<PersistenceCandidateState> InMemoryPersistenceStore::BuildCandidateSnapshot(const InMemorySaveTransaction& transaction) const
{
    if (fail_next_candidate_build_allocation_for_testing_)
    {
        fail_next_candidate_build_allocation_for_testing_ = false;
        return foundation::Result<PersistenceCandidateState>::Failure(
            foundation::Error::Create("persistence.allocation_failed", "failed to allocate prepared persistence state"));
    }
    if (transaction.base_revision_ != revision_)
    {
        return foundation::Result<PersistenceCandidateState>::Failure(
            foundation::Error::Create("persistence.conflict", "transaction base revision does not match current store revision"));
    }

    std::unordered_map<PersistentObjectId, PersistentObjectRecord> candidate_objects = objects_;
    std::unordered_map<LazyRuleId, LazyRuleRecord> candidate_lazy_rules = lazy_rules_;
    std::unordered_map<PersistentObjectId, TombstoneRecord> candidate_tombstones = tombstones_;
    std::unordered_map<PersistenceLocation, ZoneOverrideSnapshot, PersistenceLocationHash> candidate_zone_overrides = zone_overrides_;
    std::unordered_set<PersistentObjectId> candidate_dirty = dirty_ids_;
    if (revision_ == std::numeric_limits<PersistenceRevision>::max())
    {
        return foundation::Result<PersistenceCandidateState>::Failure(
            foundation::Error::Create("persistence.revision_overflow", "persistence revision cannot advance beyond UINT64_MAX"));
    }
    const PersistenceRevision candidate_revision = revision_ + 1;

    for (const PersistenceOperation& operation : transaction.operations_)
    {
        const auto valid = std::visit(
            [&](const auto& typed_operation) -> foundation::Result<void> {
                using Operation = std::decay_t<decltype(typed_operation)>;
                if constexpr (std::is_same_v<Operation, UpsertObjectOperation>)
                {
                    const auto record_valid = ValidateObject(typed_operation.record);
                    if (!record_valid)
                    {
                        return record_valid;
                    }
                    PersistentObjectRecord record = typed_operation.record;
                    record.revision = candidate_revision;
                    candidate_objects[record.persistent_id] = std::move(record);
                    candidate_tombstones.erase(typed_operation.record.persistent_id);
                    candidate_dirty.insert(typed_operation.record.persistent_id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, DeleteObjectOperation>)
                {
                    const auto tombstone_valid = ValidateTombstone(typed_operation.tombstone);
                    if (!tombstone_valid)
                    {
                        return tombstone_valid;
                    }
                    if (!candidate_objects.contains(typed_operation.tombstone.persistent_id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for deletion");
                    }
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = candidate_revision;
                    candidate_objects.erase(tombstone.persistent_id);
                    candidate_tombstones[tombstone.persistent_id] = std::move(tombstone);
                    std::erase_if(candidate_lazy_rules, [&](const auto& entry) { return entry.second.target_id == typed_operation.tombstone.persistent_id; });
                    candidate_dirty.insert(typed_operation.tombstone.persistent_id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, AdminRemoveObjectOperation>)
                {
                    if (!candidate_objects.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.record_not_found", "persistent object record was not found for administrative removal");
                    }
                    candidate_objects.erase(typed_operation.id);
                    candidate_dirty.insert(typed_operation.id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, AdminAddTombstoneOperation>)
                {
                    const auto tombstone_valid = ValidateTombstone(typed_operation.tombstone);
                    if (!tombstone_valid)
                    {
                        return tombstone_valid;
                    }
                    TombstoneRecord tombstone = typed_operation.tombstone;
                    tombstone.revision = candidate_revision;
                    candidate_tombstones[tombstone.persistent_id] = std::move(tombstone);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpsertLazyRuleOperation>)
                {
                    const auto rule_valid = ValidateLazyRule(typed_operation.record);
                    if (!rule_valid)
                    {
                        return rule_valid;
                    }
                    LazyRuleRecord rule = typed_operation.record;
                    rule.revision = candidate_revision;
                    candidate_lazy_rules[rule.rule_id] = std::move(rule);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpdateLazyRuleOperation>)
                {
                    const auto rule_valid = ValidateLazyRule(typed_operation.record);
                    if (!rule_valid)
                    {
                        return rule_valid;
                    }
                    if (!candidate_lazy_rules.contains(typed_operation.record.rule_id))
                    {
                        return PersistenceFailure("persistence.lazy_rule_not_found", "lazy rule was not found for update");
                    }
                    LazyRuleRecord rule = typed_operation.record;
                    rule.revision = candidate_revision;
                    candidate_lazy_rules[rule.rule_id] = std::move(rule);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, RemoveLazyRuleOperation>)
                {
                    if (!candidate_lazy_rules.contains(typed_operation.id))
                    {
                        return PersistenceFailure("persistence.lazy_rule_not_found", "lazy rule was not found for removal");
                    }
                    candidate_lazy_rules.erase(typed_operation.id);
                    return foundation::Result<void>::Success();
                }
                else if constexpr (std::is_same_v<Operation, UpsertZoneOverrideOperation>)
                {
                    const auto snapshot_valid = ValidateZoneOverride(typed_operation.snapshot);
                    if (!snapshot_valid)
                    {
                        return snapshot_valid;
                    }
                    ZoneOverrideSnapshot snapshot = typed_operation.snapshot;
                    snapshot.revision = candidate_revision;
                    candidate_zone_overrides[snapshot.location] = std::move(snapshot);
                    return foundation::Result<void>::Success();
                }
                else
                {
                    if (!candidate_zone_overrides.contains(typed_operation.location))
                    {
                        return PersistenceFailure("persistence.override_not_found", "zone override was not found for removal");
                    }
                    candidate_zone_overrides.erase(typed_operation.location);
                    return foundation::Result<void>::Success();
                }
            },
            operation);
        if (!valid)
        {
            return foundation::Result<PersistenceCandidateState>::Failure(valid.GetError());
        }
    }

    for (auto& [location, zone] : candidate_zone_overrides)
    {
        (void)location;
        SortZoneOverrideSnapshot(zone);
    }

    PersistenceSnapshot candidate{};
    candidate.current_revision = candidate_revision;
    candidate.objects.reserve(candidate_objects.size());
    candidate.tombstones.reserve(candidate_tombstones.size());
    candidate.lazy_rules.reserve(candidate_lazy_rules.size());
    candidate.zone_overrides.reserve(candidate_zone_overrides.size());
    for (const auto& [id, record] : candidate_objects)
    {
        (void)id;
        candidate.objects.push_back(record);
    }
    for (const auto& [id, tombstone] : candidate_tombstones)
    {
        (void)id;
        candidate.tombstones.push_back(tombstone);
    }
    for (const auto& [id, rule] : candidate_lazy_rules)
    {
        (void)id;
        candidate.lazy_rules.push_back(rule);
    }
    for (const auto& [location, snapshot] : candidate_zone_overrides)
    {
        (void)location;
        candidate.zone_overrides.push_back(snapshot);
    }
    SortPersistenceSnapshot(candidate);

    const auto valid_snapshot = ValidatePersistenceSnapshot(candidate);
    if (!valid_snapshot)
    {
        return foundation::Result<PersistenceCandidateState>::Failure(valid_snapshot.GetError());
    }
    PersistenceCandidateState candidate_state{};
    candidate_state.snapshot = std::move(candidate);
    candidate_state.objects = std::move(candidate_objects);
    candidate_state.lazy_rules = std::move(candidate_lazy_rules);
    candidate_state.tombstones = std::move(candidate_tombstones);
    candidate_state.zone_overrides = std::move(candidate_zone_overrides);
    candidate_state.dirty_ids = std::move(candidate_dirty);
    return foundation::Result<PersistenceCandidateState>::Success(std::move(candidate_state));
}

foundation::Result<void> InMemoryPersistenceStore::PublishSnapshot(PersistenceCandidateState candidate)
{
    const PersistenceSnapshot& snapshot = candidate.snapshot;
    if (durability_ != PersistenceDurability::MemoryOnly)
    {
        if (!backend_)
        {
            return PersistenceFailure("persistence.backend_missing", "persistence backend is required by durability policy");
        }
        try
        {
            const auto committed = backend_->CommitSnapshot(snapshot, durability_);
            if (!committed)
            {
                return committed;
            }
        }
        catch (...)
        {
            return PersistenceFailure("persistence.backend_exception", "persistence backend threw while committing snapshot");
        }
        candidate.dirty_ids.clear();
    }

    // All potentially allocating work is complete. These swaps publish the complete candidate as one no-allocation commit.
    objects_.swap(candidate.objects);
    tombstones_.swap(candidate.tombstones);
    lazy_rules_.swap(candidate.lazy_rules);
    zone_overrides_.swap(candidate.zone_overrides);
    dirty_ids_.swap(candidate.dirty_ids);
    revision_ = snapshot.current_revision;
    return foundation::Result<void>::Success();
}

foundation::Result<PersistenceSnapshot> InMemoryPersistenceBackend::Load()
{
    return foundation::Result<PersistenceSnapshot>::Success(snapshot_);
}

foundation::Result<void> InMemoryPersistenceBackend::CommitSnapshot(const PersistenceSnapshot& snapshot, PersistenceDurability durability)
{
    (void)durability;
    snapshot_ = snapshot;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::runtime

