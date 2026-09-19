#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"
#include "in_memory_persistence_support.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace persistence_test_allocation_fault
{
std::atomic<long long> allocations_before_failure{-1};

[[nodiscard]] bool ShouldFail() noexcept
{
    auto remaining = allocations_before_failure.load(std::memory_order_relaxed);
    while (remaining >= 0)
    {
        if (remaining == 0)
        {
            allocations_before_failure.store(-1, std::memory_order_relaxed);
            return true;
        }
        if (allocations_before_failure.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed))
        {
            return false;
        }
    }
    return false;
}

class FailAfter
{
  public:
    explicit FailAfter(long long successful_allocations_before_failure) noexcept
    {
        allocations_before_failure.store(successful_allocations_before_failure, std::memory_order_relaxed);
    }
    ~FailAfter()
    {
        allocations_before_failure.store(-1, std::memory_order_relaxed);
    }
    FailAfter(const FailAfter&) = delete;
    FailAfter& operator=(const FailAfter&) = delete;
};
} // namespace persistence_test_allocation_fault

#if defined(__GNUC__) || defined(__clang__)
#define EPIDEMIC_PERSISTENCE_TEST_NOINLINE __attribute__((noinline))
#else
#define EPIDEMIC_PERSISTENCE_TEST_NOINLINE
#endif

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void* operator new(std::size_t size)
{
    if (persistence_test_allocation_fault::ShouldFail())
    {
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void operator delete(void* memory) noexcept
{
    std::free(memory);
}

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

EPIDEMIC_PERSISTENCE_TEST_NOINLINE void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

#undef EPIDEMIC_PERSISTENCE_TEST_NOINLINE

namespace epidemic::runtime
{
struct PersistenceRuntimeTestAccess
{
    static void FailNextOperationAllocation(ISaveTransaction& transaction)
    {
        auto* concrete = dynamic_cast<InMemorySaveTransaction*>(&transaction);
        if (concrete != nullptr) concrete->fail_next_operation_allocation_for_testing_ = true;
    }
    static void FailNextCandidateBuildAllocation(InMemoryPersistenceStore& store)
    {
        store.fail_next_candidate_build_allocation_for_testing_ = true;
    }
    static std::size_t OperationCount(const ISaveTransaction& transaction)
    {
        const auto* concrete = dynamic_cast<const InMemorySaveTransaction*>(&transaction);
        return concrete == nullptr ? 0u : concrete->operations_.size();
    }
};
} // namespace epidemic::runtime

namespace
{
using epidemic::foundation::StringId;
using epidemic::runtime::AddProtectionFlag;
using epidemic::runtime::CreatePersistenceServices;
using epidemic::runtime::GameTimePoint;
using epidemic::runtime::HasProtectionFlag;
using epidemic::runtime::InMemoryPersistenceStore;
using epidemic::runtime::InMemoryPersistenceBackend;
using epidemic::runtime::IPersistenceBackend;
using epidemic::runtime::IPersistenceAdministrativeTransaction;
using epidemic::runtime::IPersistenceQuery;
using epidemic::runtime::LazyRuleId;
using epidemic::runtime::LazyRuleKind;
using epidemic::runtime::LazyRuleRecord;
using epidemic::runtime::LazyRuleState;
using epidemic::runtime::ObjectProtectionFlags;
using epidemic::runtime::ObjectProtectionMask;
using epidemic::runtime::PersistenceLocation;
using epidemic::runtime::PersistenceDurability;
using epidemic::runtime::PersistencePayload;
using epidemic::runtime::PersistenceOptions;
using epidemic::runtime::PersistenceSnapshot;
using epidemic::runtime::PersistenceState;
using epidemic::runtime::PersistentObjectId;
using epidemic::runtime::PersistentObjectKind;
using epidemic::runtime::PersistentObjectRecord;
using epidemic::runtime::RegionId;
using epidemic::runtime::SaveTransactionState;
using epidemic::runtime::TombstoneRecord;
using epidemic::runtime::ZoneOverrideSnapshot;

class ControlledBackend final : public IPersistenceBackend
{
  public:
    [[nodiscard]] epidemic::foundation::Result<PersistenceSnapshot> Load() override
    {
        ++load_count;
        if (throw_load) throw std::runtime_error("load");
        return epidemic::foundation::Result<PersistenceSnapshot>::Success(snapshot);
    }

    [[nodiscard]] epidemic::foundation::Result<void> CommitSnapshot(const PersistenceSnapshot& next_snapshot, PersistenceDurability durability) override
    {
        ++save_count;
        last_durability = durability;
        if (throw_commit) throw std::runtime_error("commit");
        if (fail_save)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("persistence.save_failed", "save failed for test"));
        }
        if (durability == PersistenceDurability::SaveAndFlushRequired)
        {
            ++flush_count;
            if (fail_flush)
            {
                return epidemic::foundation::Result<void>::Failure(
                    epidemic::foundation::Error::Create("persistence.flush_failed", "flush failed for test"));
            }
        }
        snapshot = next_snapshot;
        return epidemic::foundation::Result<void>::Success();
    }

    PersistenceSnapshot snapshot{};
    bool fail_save = false;
    bool fail_flush = false;
    bool throw_load = false;
    bool throw_commit = false;
    int load_count = 0;
    int save_count = 0;
    int flush_count = 0;
    PersistenceDurability last_durability = PersistenceDurability::MemoryOnly;
};

template <typename T>
concept HasOpenTransaction = requires(T& value) { value.OpenTransaction(); };

[[nodiscard]] StringId Id(std::string_view value)
{
    return StringId::FromString(value);
}

[[nodiscard]] PersistencePayload MakePayload(std::string_view schema, std::uint32_t version = 1u)
{
    return PersistencePayload{Id(schema), version, {std::byte{0x10}, std::byte{0x20}}};
}

[[nodiscard]] PersistenceLocation MakeLocation(std::uint64_t region, std::string_view tag)
{
    PersistenceLocation location{};
    location.region_id = RegionId{region};
    location.location_tag = Id(tag);
    return location;
}

[[nodiscard]] PersistentObjectRecord MakeRecord(std::uint64_t id, std::string_view schema = "state.object")
{
    PersistentObjectRecord record{};
    record.persistent_id = PersistentObjectId{id};
    record.asset_id = epidemic::runtime::AssetId::FromString("items/potato.itemdef");
    record.kind = PersistentObjectKind::Object;
    record.state = PersistenceState::Dirty;
    record.location = MakeLocation(3, "cell/a");
    record.payload = MakePayload(schema);
    record.created_game_time = GameTimePoint{10};
    record.last_observed_game_time = GameTimePoint{20};
    record.protection_flags = AddProtectionFlag(ObjectProtectionMask{}, ObjectProtectionFlags::PreventDecay);
    return record;
}

[[nodiscard]] LazyRuleRecord MakeLazyRule(std::uint64_t rule_id, std::uint64_t target_id)
{
    LazyRuleRecord record{};
    record.rule_id = LazyRuleId{rule_id};
    record.target_id = PersistentObjectId{target_id};
    record.kind = LazyRuleKind::Decay;
    record.state = LazyRuleState::Pending;
    record.created_game_time = GameTimePoint{100};
    record.evaluate_after_game_time = GameTimePoint{120};
    record.rule_seed = 42;
    return record;
}

[[nodiscard]] bool SameRecord(const PersistentObjectRecord& left, const PersistentObjectRecord& right)
{
    return left.persistent_id == right.persistent_id && left.asset_id == right.asset_id && left.kind == right.kind && left.tier == right.tier &&
           left.state == right.state && left.location == right.location && left.payload.schema_id == right.payload.schema_id &&
           left.payload.schema_version == right.payload.schema_version && left.payload.bytes == right.payload.bytes &&
           left.created_game_time == right.created_game_time && left.last_observed_game_time == right.last_observed_game_time &&
           left.protection_flags.value == right.protection_flags.value && left.condition_hash == right.condition_hash && left.revision == right.revision;
}

[[nodiscard]] bool SameTombstone(const TombstoneRecord& left, const TombstoneRecord& right)
{
    return left.persistent_id == right.persistent_id && left.deleted_game_time == right.deleted_game_time && left.reason == right.reason &&
           left.revision == right.revision;
}

[[nodiscard]] bool SameLazyRule(const LazyRuleRecord& left, const LazyRuleRecord& right)
{
    return left.rule_id == right.rule_id && left.target_id == right.target_id && left.kind == right.kind && left.state == right.state &&
           left.created_game_time == right.created_game_time && left.evaluate_after_game_time == right.evaluate_after_game_time &&
           left.rule_seed == right.rule_seed && left.revision == right.revision;
}

[[nodiscard]] bool SameZoneOverride(const ZoneOverrideSnapshot& left, const ZoneOverrideSnapshot& right)
{
    if (!(left.location == right.location) || left.record_ids != right.record_ids || left.revision != right.revision ||
        left.tombstones.size() != right.tombstones.size() || left.lazy_rules.size() != right.lazy_rules.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.tombstones.size(); ++index)
    {
        if (!SameTombstone(left.tombstones[index], right.tombstones[index])) return false;
    }
    for (std::size_t index = 0; index < left.lazy_rules.size(); ++index)
    {
        if (!SameLazyRule(left.lazy_rules[index], right.lazy_rules[index])) return false;
    }
    return true;
}

[[nodiscard]] bool SameSnapshot(const PersistenceSnapshot& left, const PersistenceSnapshot& right)
{
    if (left.current_revision != right.current_revision || left.objects.size() != right.objects.size() ||
        left.tombstones.size() != right.tombstones.size() || left.lazy_rules.size() != right.lazy_rules.size() ||
        left.zone_overrides.size() != right.zone_overrides.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.objects.size(); ++index)
    {
        if (!SameRecord(left.objects[index], right.objects[index])) return false;
    }
    for (std::size_t index = 0; index < left.tombstones.size(); ++index)
    {
        if (!SameTombstone(left.tombstones[index], right.tombstones[index])) return false;
    }
    for (std::size_t index = 0; index < left.lazy_rules.size(); ++index)
    {
        if (!SameLazyRule(left.lazy_rules[index], right.lazy_rules[index])) return false;
    }
    for (std::size_t index = 0; index < left.zone_overrides.size(); ++index)
    {
        if (!SameZoneOverride(left.zone_overrides[index], right.zone_overrides[index])) return false;
    }
    return true;
}

[[nodiscard]] PersistenceSnapshot MakeBackendSnapshot(std::uint64_t base, epidemic::runtime::PersistenceRevision revision)
{
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = revision;
    auto object = MakeRecord(base + 1u, base % 2u == 0u ? "state.even" : "state.odd");
    object.revision = revision;
    object.condition_hash = base * 11u;
    snapshot.objects.push_back(object);

    TombstoneRecord tombstone{};
    tombstone.persistent_id = PersistentObjectId{base + 2u};
    tombstone.deleted_game_time = GameTimePoint{static_cast<std::int64_t>(base + 20u)};
    tombstone.reason = Id(base % 2u == 0u ? "cleanup.old" : "cleanup.new");
    tombstone.revision = revision;
    snapshot.tombstones.push_back(tombstone);

    auto rule = MakeLazyRule(base + 3u, base + 1u);
    rule.revision = revision;
    rule.rule_seed = base * 17u;
    snapshot.lazy_rules.push_back(rule);

    ZoneOverrideSnapshot zone{};
    zone.location = MakeLocation(base + 4u, base % 2u == 0u ? "zone/old" : "zone/new");
    zone.record_ids.push_back(PersistentObjectId{base + 5u});
    TombstoneRecord zone_tombstone{};
    zone_tombstone.persistent_id = PersistentObjectId{base + 6u};
    zone_tombstone.deleted_game_time = GameTimePoint{static_cast<std::int64_t>(base + 30u)};
    zone_tombstone.reason = Id("zone.cleanup");
    zone_tombstone.revision = revision;
    zone.tombstones.push_back(zone_tombstone);
    auto zone_rule = MakeLazyRule(base + 7u, base + 5u);
    zone_rule.revision = revision;
    zone.lazy_rules.push_back(zone_rule);
    zone.revision = revision;
    snapshot.zone_overrides.push_back(zone);
    return snapshot;
}

[[nodiscard]] bool TestPayloadAndTypedProtectionContracts()
{
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.payload), PersistencePayload>);
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.protection_flags), ObjectProtectionMask>);
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.created_game_time), GameTimePoint>);
    static_assert(!HasOpenTransaction<IPersistenceQuery>);

    const auto payload = MakePayload("state.object");
    const auto invalid_payload = PersistencePayload{};
    auto mask = AddProtectionFlag(ObjectProtectionMask{}, ObjectProtectionFlags::PreventTheft);
    mask = AddProtectionFlag(mask, ObjectProtectionFlags::PreserveCondition);

    return payload.IsValid() && !invalid_payload.IsValid() && HasProtectionFlag(mask, ObjectProtectionFlags::PreventTheft) &&
           HasProtectionFlag(mask, ObjectProtectionFlags::PreserveCondition) && !HasProtectionFlag(mask, ObjectProtectionFlags::PreventCleanup);
}

[[nodiscard]] bool TestTransactionCommitPersistsObjectsAndRevision()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || transaction->GetState() != SaveTransactionState::Open)
    {
        return false;
    }

    const auto upsert = transaction->UpsertObject(MakeRecord(1001));
    const auto commit = transaction->Commit();
    const auto found = store.FindObject(PersistentObjectId{1001});
    const auto dirty = store.CollectDirty();
    return upsert && commit && transaction->GetState() == SaveTransactionState::Committed && found && found->revision == 1u &&
           store.GetRevision() == 1u && dirty.size() == 1u && dirty.front() == PersistentObjectId{1001};
}

[[nodiscard]] bool TestRollbackDiscardsStagedMutations()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(1001)))
    {
        return false;
    }

    transaction->Rollback();
    const auto late_mutation = transaction->UpsertObject(MakeRecord(1002));
    return transaction->GetState() == SaveTransactionState::RolledBack && !store.FindObject(PersistentObjectId{1001}) &&
           store.ListObjects().empty() && store.GetRevision() == 0u && !late_mutation &&
           late_mutation.GetError().HasCode("persistence.transaction_invalid_state");
}

[[nodiscard]] bool TestConcurrentTransactionConflict()
{
    InMemoryPersistenceStore store;
    auto first = store.OpenTransaction();
    auto second = store.OpenTransaction();
    if (!first || !second || first->GetBaseRevision() != 0u || second->GetBaseRevision() != 0u)
    {
        return false;
    }

    const auto first_upsert = first->UpsertObject(MakeRecord(1001));
    const auto first_commit = first->Commit();
    const auto second_upsert = second->UpsertObject(MakeRecord(1002));
    const auto second_commit = second->Commit();

    return first_upsert && first_commit && second_upsert && !second_commit &&
           second_commit.GetError().HasCode("persistence.conflict") &&
           second->GetState() == SaveTransactionState::Failed && store.GetRevision() == 1u &&
           store.FindObject(PersistentObjectId{1001}).has_value() &&
           !store.FindObject(PersistentObjectId{1002}).has_value();
}

[[nodiscard]] bool TestFailedCommitIsAtomic()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    auto* admin = dynamic_cast<IPersistenceAdministrativeTransaction*>(transaction.get());
    if (!transaction || admin == nullptr || !transaction->UpsertObject(MakeRecord(1001)) || !admin->AdminRemoveObject(PersistentObjectId{9999}))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    return !commit && commit.GetError().HasCode("persistence.record_not_found") && transaction->GetState() == SaveTransactionState::Failed &&
           !store.FindObject(PersistentObjectId{1001}) && store.GetRevision() == 0u;
}

[[nodiscard]] bool TestLazyRuleIdQueries()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(1001)) || !transaction->UpsertLazyRule(MakeLazyRule(5001, 1001)) ||
        !transaction->UpsertLazyRule(MakeLazyRule(5002, 1001)) || !transaction->Commit())
    {
        return false;
    }

    const auto by_id = store.FindLazyRule(LazyRuleId{5002});
    const auto by_target = store.FindLazyRules(PersistentObjectId{1001});
    return by_id && by_id->rule_id == LazyRuleId{5002} && by_id->revision == 1u && by_target.size() == 2u &&
           by_target[0].rule_id == LazyRuleId{5001} && by_target[1].rule_id == LazyRuleId{5002};
}

[[nodiscard]] bool TestLazyRuleUpdateRemoveAndDueQuery()
{
    InMemoryPersistenceStore store;
    auto create = store.OpenTransaction();
    auto early = MakeLazyRule(5001, 1001);
    early.evaluate_after_game_time = GameTimePoint{110};
    auto late = MakeLazyRule(5002, 1001);
    late.evaluate_after_game_time = GameTimePoint{200};
    if (!create || !create->UpsertObject(MakeRecord(1001)) || !create->UpsertLazyRule(late) || !create->UpsertLazyRule(early) ||
        !create->Commit())
    {
        return false;
    }

    const auto due_before_update = store.QueryDueLazyRules(GameTimePoint{150});
    late.state = LazyRuleState::Cancelled;
    auto update = store.OpenTransaction();
    if (!update || !update->UpdateLazyRule(late) || !update->RemoveLazyRule(LazyRuleId{5001}) || !update->Commit())
    {
        return false;
    }

    const auto removed = store.FindLazyRule(LazyRuleId{5001});
    const auto updated = store.FindLazyRule(LazyRuleId{5002});
    const auto due_after_update = store.QueryDueLazyRules(GameTimePoint{250});
    return due_before_update.size() == 1u && due_before_update.front().rule_id == LazyRuleId{5001} &&
           !removed && updated && updated->state == LazyRuleState::Cancelled && updated->revision == 2u &&
           due_after_update.empty();
}

[[nodiscard]] bool TestRichTombstonesAndZoneOverrides()
{
    InMemoryPersistenceStore store;
    TombstoneRecord tombstone{};
    tombstone.persistent_id = PersistentObjectId{1001};
    tombstone.deleted_game_time = GameTimePoint{200};
    tombstone.reason = Id("cleanup.decay");

    ZoneOverrideSnapshot snapshot{};
    snapshot.location = MakeLocation(9, "zone/override");
    snapshot.record_ids.push_back(PersistentObjectId{1002});
    snapshot.tombstones.push_back(tombstone);
    snapshot.lazy_rules.push_back(MakeLazyRule(7001, 1002));

    auto transaction = store.OpenTransaction();
    auto* admin = dynamic_cast<IPersistenceAdministrativeTransaction*>(transaction.get());
    if (!transaction || admin == nullptr || !admin->AdminAddTombstone(tombstone) || !transaction->UpsertZoneOverride(snapshot) || !transaction->Commit())
    {
        return false;
    }

    const auto found_tombstone = store.FindTombstone(PersistentObjectId{1001});
    const auto found_zone = store.FindZoneOverride(snapshot.location);
    return store.IsTombstoned(PersistentObjectId{1001}) && found_tombstone && found_tombstone->reason == Id("cleanup.decay") &&
           found_tombstone->revision == 1u && found_zone && found_zone->record_ids.size() == 1u && found_zone->revision == 1u;
}

[[nodiscard]] bool TestBackendSaveFailureLeavesStoreUnchanged()
{
    auto backend = std::make_shared<ControlledBackend>();
    backend->fail_save = true;
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services)
    {
        return false;
    }

    auto transaction = services.Value().store->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(3001)))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    return !commit && commit.GetError().HasCode("persistence.save_failed") &&
           transaction->GetState() == SaveTransactionState::Failed && services.Value().store->GetRevision() == 0u &&
           !services.Value().store->FindObject(PersistentObjectId{3001}) && backend->save_count == 1 && backend->flush_count == 0 &&
           backend->snapshot.current_revision == 0u;
}

[[nodiscard]] bool TestBackendFlushFailureLeavesStoreUnchanged()
{
    auto backend = std::make_shared<ControlledBackend>();
    backend->fail_flush = true;
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveAndFlushRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services)
    {
        return false;
    }

    auto transaction = services.Value().store->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(3002)))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    const auto loaded = backend->Load();
    return !commit && commit.GetError().HasCode("persistence.flush_failed") &&
           transaction->GetState() == SaveTransactionState::Failed && services.Value().store->GetRevision() == 0u &&
           !services.Value().store->FindObject(PersistentObjectId{3002}) && backend->save_count == 1 && backend->flush_count == 1 &&
           loaded && loaded.Value().current_revision == 0u && loaded.Value().objects.empty();
}

[[nodiscard]] bool TestSaveRequiredCommitPublishesAfterBackendSave()
{
    auto backend = std::make_shared<ControlledBackend>();
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services)
    {
        return false;
    }

    auto transaction = services.Value().store->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(3003)) || !transaction->Commit())
    {
        return false;
    }
    return services.Value().store->GetRevision() == 1u && services.Value().store->FindObject(PersistentObjectId{3003}) &&
           backend->save_count == 1 && backend->flush_count == 0 && backend->snapshot.current_revision == 1u &&
           backend->snapshot.objects.size() == 1u && services.Value().store->CollectDirty().empty();
}

[[nodiscard]] bool TestSaveAndFlushRequiredCallsSaveThenFlush()
{
    auto backend = std::make_shared<ControlledBackend>();
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveAndFlushRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services)
    {
        return false;
    }

    auto transaction = services.Value().store->OpenTransaction();
    return transaction && transaction->UpsertObject(MakeRecord(3004)) && transaction->Commit() &&
           backend->save_count == 1 && backend->flush_count == 1 &&
           services.Value().store->FindObject(PersistentObjectId{3004}).has_value();
}

[[nodiscard]] bool TestUpdateMissingLazyRuleRejected()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(1001)) || !transaction->UpdateLazyRule(MakeLazyRule(6001, 1001)))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    return !commit && commit.GetError().HasCode("persistence.lazy_rule_not_found") && store.GetRevision() == 0u &&
           store.ListLazyRules().empty();
}

[[nodiscard]] bool TestDeleteObjectRemovesLazyRulesForTarget()
{
    InMemoryPersistenceStore store;
    auto create = store.OpenTransaction();
    if (!create || !create->UpsertObject(MakeRecord(2002)) || !create->UpsertLazyRule(MakeLazyRule(8001, 2002)) || !create->Commit())
    {
        return false;
    }

    TombstoneRecord tombstone{};
    tombstone.persistent_id = PersistentObjectId{2002};
    tombstone.deleted_game_time = GameTimePoint{300};
    tombstone.reason = Id("delete.lazy_target");

    auto remove = store.OpenTransaction();
    if (!remove || !remove->DeleteObject(tombstone) || !remove->Commit())
    {
        return false;
    }

    return !store.FindObject(PersistentObjectId{2002}) && store.FindTombstone(PersistentObjectId{2002}) &&
           store.FindLazyRules(PersistentObjectId{2002}).empty() && store.ListLazyRules().empty();
}

[[nodiscard]] bool TestSnapshotRejectsDanglingLazyRule()
{
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = 1u;
    auto rule = MakeLazyRule(9101, 9001);
    rule.revision = 1u;
    snapshot.lazy_rules.push_back(rule);

    auto backend = std::make_shared<ControlledBackend>();
    backend->snapshot = snapshot;

    PersistenceOptions options{};
    options.backend = backend;
    const auto services = CreatePersistenceServices(options);
    return !services && services.GetError().HasCode("persistence.invalid_snapshot");
}

[[nodiscard]] bool TestUpsertThenRemoveLazyRuleInOneTransaction()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertLazyRule(MakeLazyRule(9001, 4001)) || !transaction->RemoveLazyRule(LazyRuleId{9001}))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    return commit && store.GetRevision() == 1u && store.ListLazyRules().empty();
}

[[nodiscard]] bool TestInvalidBackendSnapshotRejected()
{
    auto backend = std::make_shared<ControlledBackend>();
    PersistentObjectRecord active = MakeRecord(5001);
    active.revision = 1u;
    TombstoneRecord tombstone{};
    tombstone.persistent_id = PersistentObjectId{5001};
    tombstone.deleted_game_time = GameTimePoint{500};
    tombstone.reason = Id("invalid.duplicate");
    tombstone.revision = 1u;
    backend->snapshot.current_revision = 1u;
    backend->snapshot.objects.push_back(active);
    backend->snapshot.tombstones.push_back(tombstone);

    PersistenceOptions options{};
    options.backend = backend;
    const auto services = CreatePersistenceServices(options);
    return !services && services.GetError().HasCode("persistence.invalid_snapshot");
}

[[nodiscard]] bool TestDeleteObjectIsAtomicTombstoneOperation()
{
    InMemoryPersistenceStore store;
    auto create = store.OpenTransaction();
    if (!create || !create->UpsertObject(MakeRecord(2001)) || !create->Commit())
    {
        return false;
    }

    TombstoneRecord tombstone{};
    tombstone.persistent_id = PersistentObjectId{2001};
    tombstone.deleted_game_time = GameTimePoint{300};
    tombstone.reason = Id("delete.atomic");

    auto remove = store.OpenTransaction();
    const auto deleted = remove->DeleteObject(tombstone);
    const auto committed = remove->Commit();
    const auto found_object = store.FindObject(PersistentObjectId{2001});
    const auto found_tombstone = store.FindTombstone(PersistentObjectId{2001});
    const auto dirty = store.CollectDirty();

    return deleted && committed && !found_object && found_tombstone &&
           found_tombstone->reason == Id("delete.atomic") && found_tombstone->revision == 2u &&
           store.GetRevision() == 2u && dirty.size() == 1u && dirty.front() == PersistentObjectId{2001};
}

[[nodiscard]] bool TestValidationRejectsInvalidRecords()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    PersistentObjectRecord invalid_object = MakeRecord(1001);
    invalid_object.payload = PersistencePayload{};
    LazyRuleRecord invalid_rule = MakeLazyRule(5001, 1001);
    invalid_rule.evaluate_after_game_time = GameTimePoint{1};
    TombstoneRecord invalid_tombstone{};
    invalid_tombstone.persistent_id = PersistentObjectId{1001};
    invalid_tombstone.deleted_game_time = GameTimePoint{-1};

    const auto object_result = transaction->UpsertObject(invalid_object);
    const auto rule_result = transaction->UpsertLazyRule(invalid_rule);
    const auto tombstone_result = transaction->DeleteObject(invalid_tombstone);
    return !object_result && object_result.GetError().HasCode("persistence.invalid_payload") && !rule_result &&
           rule_result.GetError().HasCode("persistence.invalid_lazy_rule_time") && !tombstone_result &&
           tombstone_result.GetError().HasCode("persistence.invalid_game_time");
}

[[nodiscard]] bool TestFactoryCreatesUsableStore()
{
    const auto services = CreatePersistenceServices();
    if (!services || !services.Value().store || !services.Value().query)
    {
        return false;
    }

    auto transaction = services.Value().store->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(77)) || !transaction->Commit())
    {
        return false;
    }
    return services.Value().store->FindObject(PersistentObjectId{77}).has_value();
}

[[nodiscard]] bool TestBackendLoadsFactoryStore()
{
    auto backend = std::make_shared<InMemoryPersistenceBackend>();
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = 4u;
    auto record = MakeRecord(99);
    record.revision = 4u;
    snapshot.objects.push_back(record);
    if (!backend->CommitSnapshot(snapshot, PersistenceDurability::SaveAndFlushRequired))
    {
        return false;
    }

    PersistenceOptions options{};
    options.backend = backend;
    const auto services = CreatePersistenceServices(options);
    if (!services || !services.Value().store || services.Value().store->GetRevision() != 4u)
    {
        return false;
    }

    const auto loaded = services.Value().query->FindObject(PersistentObjectId{99});
    const auto exported = static_cast<InMemoryPersistenceStore*>(services.Value().store.get())->CreateSnapshot();
    return loaded && loaded->revision == 4u && exported.current_revision == 4u && exported.objects.size() == 1u;
}

[[nodiscard]] bool TestRevisionOverflowRejected()
{
    PersistenceSnapshot snapshot{};
    snapshot.current_revision = std::numeric_limits<epidemic::runtime::PersistenceRevision>::max();
    InMemoryPersistenceStore store{snapshot};
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(9001)))
    {
        return false;
    }

    const auto commit = transaction->Commit();
    return !commit && commit.GetError().HasCode("persistence.revision_overflow") &&
           store.GetRevision() == std::numeric_limits<epidemic::runtime::PersistenceRevision>::max();
}

[[nodiscard]] bool TestBackendExceptionsAreContainedAndRollbackRemainsPossible()
{
    auto load_backend = std::make_shared<ControlledBackend>();
    load_backend->throw_load = true;
    PersistenceOptions load_options{};
    load_options.backend = load_backend;
    const auto load = CreatePersistenceServices(load_options);
    if (load || !load.GetError().HasCode("persistence.backend_exception")) return false;

    auto backend = std::make_shared<ControlledBackend>();
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services) return false;
    auto transaction = services.Value().store->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(6101))) return false;
    backend->throw_commit = true;
    const auto commit = transaction->Commit();
    if (commit || !commit.GetError().HasCode("persistence.backend_exception") || transaction->GetState() != SaveTransactionState::Failed ||
        services.Value().store->GetRevision() != 0u || services.Value().store->FindObject(PersistentObjectId{6101}) ||
        backend->snapshot.current_revision != 0u)
    {
        return false;
    }
    transaction->Rollback();
    return transaction->GetState() == SaveTransactionState::RolledBack;
}

[[nodiscard]] bool TestTerminalTransactionCallsPreserveTerminalStates()
{
    InMemoryPersistenceStore store;
    auto committed = store.OpenTransaction();
    if (!committed || !committed->UpsertObject(MakeRecord(6201)) || !committed->Commit()) return false;
    const auto repeated_commit = committed->Commit();
    committed->Rollback();
    if (!repeated_commit || committed->GetState() != SaveTransactionState::Committed) return false;

    auto rolled = store.OpenTransaction();
    rolled->Rollback();
    const auto commit_after_rollback = rolled->Commit();
    rolled->Rollback();
    return !commit_after_rollback && commit_after_rollback.GetError().HasCode("persistence.transaction_invalid_state") &&
           rolled->GetState() == SaveTransactionState::RolledBack;
}

[[nodiscard]] bool TestPersistedEnumAndProtectionDomainsAreValidated()
{
    InMemoryPersistenceStore store;
    const auto run_object_case = [&store](auto mutate) {
        auto tx = store.OpenTransaction();
        auto record = MakeRecord(6301);
        mutate(record);
        const auto result = tx->UpsertObject(record);
        return !result && tx->GetState() == SaveTransactionState::Open && store.GetRevision() == 0u;
    };
    if (!run_object_case([](auto& r) { r.kind = static_cast<PersistentObjectKind>(999); }) ||
        !run_object_case([](auto& r) { r.tier = static_cast<epidemic::runtime::PersistenceTier>(999); }) ||
        !run_object_case([](auto& r) { r.state = static_cast<PersistenceState>(999); }) ||
        !run_object_case([](auto& r) { r.protection_flags.value = 1u << 31; }))
    {
        return false;
    }
    auto tx = store.OpenTransaction();
    auto invalid_kind = MakeLazyRule(6302, 6301);
    invalid_kind.kind = static_cast<LazyRuleKind>(999);
    auto invalid_state = MakeLazyRule(6303, 6301);
    invalid_state.state = static_cast<LazyRuleState>(999);
    const auto a = tx->UpsertLazyRule(invalid_kind);
    const auto b = tx->UpsertLazyRule(invalid_state);
    if (a || b || !a.GetError().HasCode("persistence.invalid_enum") || !b.GetError().HasCode("persistence.invalid_enum")) return false;

    auto backend = std::make_shared<ControlledBackend>();
    auto invalid_loaded = MakeRecord(6304);
    invalid_loaded.revision = 1u;
    invalid_loaded.state = static_cast<PersistenceState>(999);
    backend->snapshot.current_revision = 1u;
    backend->snapshot.objects.push_back(invalid_loaded);
    PersistenceOptions options{};
    options.backend = backend;
    const auto loaded = CreatePersistenceServices(options);
    return !loaded && loaded.GetError().HasCode("persistence.invalid_snapshot");
}

[[nodiscard]] bool TestTransactionAllocationFailureDoesNotStageOperation()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction) return false;
    epidemic::runtime::PersistenceRuntimeTestAccess::FailNextOperationAllocation(*transaction);
    const auto failed = transaction->UpsertObject(MakeRecord(6401));
    if (failed || !failed.GetError().HasCode("persistence.allocation_failed") ||
        epidemic::runtime::PersistenceRuntimeTestAccess::OperationCount(*transaction) != 0u || transaction->GetState() != SaveTransactionState::Open)
    {
        return false;
    }
    return transaction->UpsertObject(MakeRecord(6401)) && transaction->Commit() && store.FindObject(PersistentObjectId{6401}).has_value();
}

[[nodiscard]] bool TestCandidateAllocationFailureLeavesBackendAndLiveStateOld()
{
    auto backend = std::make_shared<ControlledBackend>();
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = PersistenceDurability::SaveRequired;
    const auto services = CreatePersistenceServices(options);
    if (!services) return false;
    auto* concrete = dynamic_cast<InMemoryPersistenceStore*>(services.Value().store.get());
    if (concrete == nullptr) return false;
    auto transaction = concrete->OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(6501))) return false;
    epidemic::runtime::PersistenceRuntimeTestAccess::FailNextCandidateBuildAllocation(*concrete);
    const auto failed = transaction->Commit();
    return !failed && failed.GetError().HasCode("persistence.allocation_failed") && transaction->GetState() == SaveTransactionState::Failed &&
           concrete->GetRevision() == 0u && !concrete->FindObject(PersistentObjectId{6501}) && backend->save_count == 0 && backend->snapshot.current_revision == 0u;
}

[[nodiscard]] bool TestEmptyTransactionCommitAndDuplicateSnapshotIds()
{
    InMemoryPersistenceStore store;
    auto empty = store.OpenTransaction();
    if (!empty || !empty->Commit() || store.GetRevision() != 1u || empty->GetState() != SaveTransactionState::Committed) return false;

    auto backend = std::make_shared<ControlledBackend>();
    auto first = MakeRecord(6601);
    first.revision = 1u;
    auto duplicate = first;
    duplicate.asset_id = epidemic::runtime::AssetId::FromString("items/other.itemdef");
    backend->snapshot.current_revision = 1u;
    backend->snapshot.objects = {first, duplicate};
    PersistenceOptions options{};
    options.backend = backend;
    const auto services = CreatePersistenceServices(options);
    return !services && services.GetError().HasCode("persistence.invalid_snapshot");
}

[[nodiscard]] bool TestSnapshotOrderIsDeterministic()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    ZoneOverrideSnapshot zone_b{};
    zone_b.location = MakeLocation(2, "zone/b");
    zone_b.record_ids = {PersistentObjectId{30}, PersistentObjectId{10}};
    zone_b.tombstones.push_back(TombstoneRecord{PersistentObjectId{40}, GameTimePoint{40}, Id("test")});
    zone_b.tombstones.push_back(TombstoneRecord{PersistentObjectId{20}, GameTimePoint{40}, Id("test")});
    zone_b.lazy_rules.push_back(MakeLazyRule(9002, 10));
    zone_b.lazy_rules.push_back(MakeLazyRule(9001, 30));

    ZoneOverrideSnapshot zone_a{};
    zone_a.location = MakeLocation(1, "zone/a");

    if (!transaction || !transaction->UpsertObject(MakeRecord(30)) || !transaction->UpsertObject(MakeRecord(10)) ||
        !transaction->UpsertLazyRule(MakeLazyRule(9002, 10)) || !transaction->UpsertLazyRule(MakeLazyRule(9001, 30)) ||
        !transaction->UpsertZoneOverride(zone_b) || !transaction->UpsertZoneOverride(zone_a) || !transaction->Commit())
    {
        return false;
    }

    const PersistenceSnapshot snapshot = store.CreateSnapshot();
    return snapshot.objects.size() == 2u && snapshot.objects[0].persistent_id == PersistentObjectId{10} &&
           snapshot.objects[1].persistent_id == PersistentObjectId{30} &&
           snapshot.lazy_rules.size() == 2u && snapshot.lazy_rules[0].rule_id == LazyRuleId{9001} &&
           snapshot.lazy_rules[1].rule_id == LazyRuleId{9002} &&
           snapshot.zone_overrides.size() == 2u && snapshot.zone_overrides[0].location.region_id == RegionId{1} &&
           snapshot.zone_overrides[1].location.region_id == RegionId{2} &&
           snapshot.zone_overrides[1].record_ids[0] == PersistentObjectId{10} &&
           snapshot.zone_overrides[1].record_ids[1] == PersistentObjectId{30} &&
           snapshot.zone_overrides[1].tombstones[0].persistent_id == PersistentObjectId{20} &&
           snapshot.zone_overrides[1].lazy_rules[0].rule_id == LazyRuleId{9001};
}
[[nodiscard]] bool TestQueryAndSnapshotResultsAreDetached()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    ZoneOverrideSnapshot zone{};
    zone.location = MakeLocation(11, "zone/detached");
    zone.record_ids.push_back(PersistentObjectId{7101});
    zone.lazy_rules.push_back(MakeLazyRule(7102, 7101));
    if (!transaction || !transaction->UpsertObject(MakeRecord(7101)) || !transaction->UpsertLazyRule(MakeLazyRule(7103, 7101)) ||
        !transaction->UpsertZoneOverride(zone) || !transaction->Commit())
    {
        return false;
    }

    auto object = store.FindObject(PersistentObjectId{7101});
    auto rule = store.FindLazyRule(LazyRuleId{7103});
    auto found_zone = store.FindZoneOverride(zone.location);
    PersistenceSnapshot snapshot = store.CreateSnapshot();
    if (!object || !rule || !found_zone || snapshot.objects.empty() || snapshot.lazy_rules.empty() || snapshot.zone_overrides.empty()) return false;

    object->payload.bytes[0] = std::byte{0x7F};
    rule->rule_seed = 9999u;
    found_zone->record_ids.clear();
    snapshot.objects[0].payload.bytes.clear();
    snapshot.lazy_rules[0].state = LazyRuleState::Expired;
    snapshot.zone_overrides[0].lazy_rules.clear();

    const auto live_object = store.FindObject(PersistentObjectId{7101});
    const auto live_rule = store.FindLazyRule(LazyRuleId{7103});
    const auto live_zone = store.FindZoneOverride(zone.location);
    return live_object && live_object->payload.bytes.size() == 2u && live_object->payload.bytes[0] == std::byte{0x10} &&
           live_rule && live_rule->rule_seed == 42u && live_zone && live_zone->record_ids.size() == 1u && live_zone->lazy_rules.size() == 1u;
}

[[nodiscard]] bool TestZoneOverrideCandidateValidationIsAtomic()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    ZoneOverrideSnapshot zone{};
    zone.location = MakeLocation(12, "zone/invalid");
    zone.record_ids = {PersistentObjectId{7201}, PersistentObjectId{7201}};
    zone.lazy_rules.push_back(MakeLazyRule(7202, 7201));
    if (!transaction || !transaction->UpsertZoneOverride(zone)) return false;
    const auto commit = transaction->Commit();
    return !commit && commit.GetError().HasCode("persistence.invalid_snapshot") && transaction->GetState() == SaveTransactionState::Failed &&
           store.GetRevision() == 0u && store.ListZoneOverrides().empty();
}

[[nodiscard]] bool TestRichSnapshotRoundTripsThroughInMemoryBackend()
{
    InMemoryPersistenceBackend backend;
    const PersistenceSnapshot expected = MakeBackendSnapshot(7300u, 9u);
    if (!backend.CommitSnapshot(expected, PersistenceDurability::SaveRequired)) return false;
    const auto loaded = backend.Load();
    return loaded && SameSnapshot(loaded.Value(), expected);
}

[[nodiscard]] bool TestDurabilityPoliciesUseSingleAtomicCommitContract()
{
    auto memory_backend = std::make_shared<ControlledBackend>();
    PersistenceOptions memory_options{};
    memory_options.backend = memory_backend;
    memory_options.durability = PersistenceDurability::MemoryOnly;
    const auto memory_services = CreatePersistenceServices(memory_options);
    if (!memory_services) return false;
    auto memory_tx = memory_services.Value().store->OpenTransaction();
    if (!memory_tx || !memory_tx->UpsertObject(MakeRecord(7401)) || !memory_tx->Commit()) return false;
    if (memory_backend->save_count != 0 || memory_services.Value().store->CollectDirty().size() != 1u) return false;

    auto save_backend = std::make_shared<ControlledBackend>();
    PersistenceOptions save_options{};
    save_options.backend = save_backend;
    save_options.durability = PersistenceDurability::SaveRequired;
    const auto save_services = CreatePersistenceServices(save_options);
    if (!save_services) return false;
    auto save_tx = save_services.Value().store->OpenTransaction();
    if (!save_tx || !save_tx->UpsertObject(MakeRecord(7402)) || !save_tx->Commit()) return false;
    if (save_backend->save_count != 1 || save_backend->last_durability != PersistenceDurability::SaveRequired ||
        !save_services.Value().store->CollectDirty().empty())
    {
        return false;
    }

    auto flush_backend = std::make_shared<ControlledBackend>();
    PersistenceOptions flush_options{};
    flush_options.backend = flush_backend;
    flush_options.durability = PersistenceDurability::SaveAndFlushRequired;
    const auto flush_services = CreatePersistenceServices(flush_options);
    if (!flush_services) return false;
    auto flush_tx = flush_services.Value().store->OpenTransaction();
    return flush_tx && flush_tx->UpsertObject(MakeRecord(7403)) && flush_tx->Commit() && flush_backend->save_count == 1 &&
           flush_backend->flush_count == 1 && flush_backend->last_durability == PersistenceDurability::SaveAndFlushRequired &&
           flush_services.Value().store->CollectDirty().empty();
}

[[nodiscard]] bool TestInvalidDurabilityRejectedBeforeBackendUse()
{
    auto backend = std::make_shared<ControlledBackend>();
    PersistenceOptions options{};
    options.backend = backend;
    options.durability = static_cast<PersistenceDurability>(999);
    const auto services = CreatePersistenceServices(options);
    return !services && services.GetError().HasCode("persistence.invalid_durability") && backend->load_count == 0 && backend->save_count == 0;
}

[[nodiscard]] bool TestInMemoryBackendStrongCommitUnderAllocationFaults()
{
    const PersistenceSnapshot previous = MakeBackendSnapshot(7000u, 7u);
    const PersistenceSnapshot next = MakeBackendSnapshot(8000u, 8u);
    bool saw_failure = false;
    bool saw_success = false;

    for (long long fail_after = 0; fail_after < 128; ++fail_after)
    {
        InMemoryPersistenceBackend backend;
        if (!backend.CommitSnapshot(previous, PersistenceDurability::MemoryOnly)) return false;

        bool commit_failed = false;
        {
            persistence_test_allocation_fault::FailAfter fault(fail_after);
            try
            {
                const auto committed = backend.CommitSnapshot(next, PersistenceDurability::SaveAndFlushRequired);
                commit_failed = !committed;
                saw_success = static_cast<bool>(committed);
            }
            catch (const std::bad_alloc&)
            {
                commit_failed = true;
            }
        }

        const auto loaded = backend.Load();
        if (!loaded) return false;
        if (commit_failed)
        {
            saw_failure = true;
            if (!SameSnapshot(loaded.Value(), previous)) return false;
        }
        else
        {
            if (!SameSnapshot(loaded.Value(), next)) return false;
            break;
        }
    }
    return saw_failure && saw_success;
}

} // namespace

int main()
{
    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"PayloadAndTypedProtectionContracts", TestPayloadAndTypedProtectionContracts},
        {"TransactionCommitPersistsObjectsAndRevision", TestTransactionCommitPersistsObjectsAndRevision},
        {"RollbackDiscardsStagedMutations", TestRollbackDiscardsStagedMutations},
        {"ConcurrentTransactionConflict", TestConcurrentTransactionConflict},
        {"FailedCommitIsAtomic", TestFailedCommitIsAtomic},
        {"LazyRuleIdQueries", TestLazyRuleIdQueries},
        {"LazyRuleUpdateRemoveAndDueQuery", TestLazyRuleUpdateRemoveAndDueQuery},
        {"RichTombstonesAndZoneOverrides", TestRichTombstonesAndZoneOverrides},
        {"BackendSaveFailureLeavesStoreUnchanged", TestBackendSaveFailureLeavesStoreUnchanged},
        {"BackendFlushFailureLeavesStoreUnchanged", TestBackendFlushFailureLeavesStoreUnchanged},
        {"SaveRequiredCommitPublishesAfterBackendSave", TestSaveRequiredCommitPublishesAfterBackendSave},
        {"SaveAndFlushRequiredCallsSaveThenFlush", TestSaveAndFlushRequiredCallsSaveThenFlush},
        {"UpdateMissingLazyRuleRejected", TestUpdateMissingLazyRuleRejected},
        {"DeleteObjectRemovesLazyRulesForTarget", TestDeleteObjectRemovesLazyRulesForTarget},
        {"SnapshotRejectsDanglingLazyRule", TestSnapshotRejectsDanglingLazyRule},
        {"UpsertThenRemoveLazyRuleInOneTransaction", TestUpsertThenRemoveLazyRuleInOneTransaction},
        {"InvalidBackendSnapshotRejected", TestInvalidBackendSnapshotRejected},
        {"DeleteObjectIsAtomicTombstoneOperation", TestDeleteObjectIsAtomicTombstoneOperation},
        {"ValidationRejectsInvalidRecords", TestValidationRejectsInvalidRecords},
        {"FactoryCreatesUsableStore", TestFactoryCreatesUsableStore},
        {"BackendLoadsFactoryStore", TestBackendLoadsFactoryStore},
        {"RevisionOverflowRejected", TestRevisionOverflowRejected},
        {"BackendExceptionsContained", TestBackendExceptionsAreContainedAndRollbackRemainsPossible},
        {"TerminalTransactionCalls", TestTerminalTransactionCallsPreserveTerminalStates},
        {"PersistedEnumDomains", TestPersistedEnumAndProtectionDomainsAreValidated},
        {"TransactionAllocationFailure", TestTransactionAllocationFailureDoesNotStageOperation},
        {"CandidateAllocationFailure", TestCandidateAllocationFailureLeavesBackendAndLiveStateOld},
        {"EmptyCommitAndDuplicateSnapshotIds", TestEmptyTransactionCommitAndDuplicateSnapshotIds},
        {"SnapshotOrderIsDeterministic", TestSnapshotOrderIsDeterministic},
        {"QueryAndSnapshotResultsAreDetached", TestQueryAndSnapshotResultsAreDetached},
        {"ZoneOverrideCandidateValidationIsAtomic", TestZoneOverrideCandidateValidationIsAtomic},
        {"RichSnapshotRoundTripsThroughInMemoryBackend", TestRichSnapshotRoundTripsThroughInMemoryBackend},
        {"DurabilityPoliciesUseSingleAtomicCommitContract", TestDurabilityPoliciesUseSingleAtomicCommitContract},
        {"InvalidDurabilityRejectedBeforeBackendUse", TestInvalidDurabilityRejectedBeforeBackendUse},
        {"InMemoryBackendStrongCommitUnderAllocationFaults", TestInMemoryBackendStrongCommitUnderAllocationFaults},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Persistence test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}
