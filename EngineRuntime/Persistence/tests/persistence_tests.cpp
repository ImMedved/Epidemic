#include "Epidemic/Runtime/Persistence/persistence_services.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"
#include "in_memory_persistence_support.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>

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
        return epidemic::foundation::Result<PersistenceSnapshot>::Success(snapshot);
    }

    [[nodiscard]] epidemic::foundation::Result<void> Save(const PersistenceSnapshot& next_snapshot) override
    {
        ++save_count;
        if (fail_save)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("persistence.save_failed", "save failed for test"));
        }
        snapshot = next_snapshot;
        return epidemic::foundation::Result<void>::Success();
    }

    [[nodiscard]] epidemic::foundation::Result<void> Flush() override
    {
        ++flush_count;
        if (fail_flush)
        {
            return epidemic::foundation::Result<void>::Failure(
                epidemic::foundation::Error::Create("persistence.flush_failed", "flush failed for test"));
        }
        return epidemic::foundation::Result<void>::Success();
    }

    PersistenceSnapshot snapshot{};
    bool fail_save = false;
    bool fail_flush = false;
    int load_count = 0;
    int save_count = 0;
    int flush_count = 0;
};

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

[[nodiscard]] bool TestPayloadAndTypedProtectionContracts()
{
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.payload), PersistencePayload>);
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.protection_flags), ObjectProtectionMask>);
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.created_game_time), GameTimePoint>);

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
    if (!transaction || !transaction->UpsertObject(MakeRecord(1001)) || !transaction->AdminRemoveObject(PersistentObjectId{9999}))
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
    if (!transaction || !transaction->UpsertLazyRule(MakeLazyRule(5001, 1001)) || !transaction->UpsertLazyRule(MakeLazyRule(5002, 1001)) ||
        !transaction->Commit())
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
    if (!create || !create->UpsertLazyRule(late) || !create->UpsertLazyRule(early) || !create->Commit())
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
    snapshot.record_ids.push_back(PersistentObjectId{1001});
    snapshot.tombstones.push_back(tombstone);
    snapshot.lazy_rules.push_back(MakeLazyRule(7001, 1001));

    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->AdminAddTombstone(tombstone) || !transaction->UpsertZoneOverride(snapshot) || !transaction->Commit())
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
           !services.Value().store->FindObject(PersistentObjectId{3001}) && backend->save_count == 1 && backend->flush_count == 0;
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
    return !commit && commit.GetError().HasCode("persistence.flush_failed") &&
           transaction->GetState() == SaveTransactionState::Failed && services.Value().store->GetRevision() == 0u &&
           !services.Value().store->FindObject(PersistentObjectId{3002}) && backend->save_count == 1 && backend->flush_count == 1;
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
           backend->snapshot.objects.size() == 1u;
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

    const auto object_result = transaction->UpsertObject(invalid_object);
    const auto rule_result = transaction->UpsertLazyRule(invalid_rule);
    return !object_result && object_result.GetError().HasCode("persistence.invalid_payload") && !rule_result &&
           rule_result.GetError().HasCode("persistence.invalid_lazy_rule_time");
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
    if (!backend->Save(snapshot) || !backend->Flush())
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
        {"UpsertThenRemoveLazyRuleInOneTransaction", TestUpsertThenRemoveLazyRuleInOneTransaction},
        {"InvalidBackendSnapshotRejected", TestInvalidBackendSnapshotRejected},
        {"DeleteObjectIsAtomicTombstoneOperation", TestDeleteObjectIsAtomicTombstoneOperation},
        {"ValidationRejectsInvalidRecords", TestValidationRejectsInvalidRecords},
        {"FactoryCreatesUsableStore", TestFactoryCreatesUsableStore},
        {"BackendLoadsFactoryStore", TestBackendLoadsFactoryStore},
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
