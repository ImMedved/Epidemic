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
using epidemic::runtime::LazyRuleId;
using epidemic::runtime::LazyRuleKind;
using epidemic::runtime::LazyRuleRecord;
using epidemic::runtime::LazyRuleState;
using epidemic::runtime::ObjectProtectionFlags;
using epidemic::runtime::ObjectProtectionMask;
using epidemic::runtime::PersistenceLocation;
using epidemic::runtime::PersistencePayload;
using epidemic::runtime::PersistenceState;
using epidemic::runtime::PersistentObjectId;
using epidemic::runtime::PersistentObjectKind;
using epidemic::runtime::PersistentObjectRecord;
using epidemic::runtime::RegionId;
using epidemic::runtime::SaveTransactionState;
using epidemic::runtime::TombstoneRecord;
using epidemic::runtime::ZoneOverrideSnapshot;

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

[[nodiscard]] bool TestFailedCommitIsAtomic()
{
    InMemoryPersistenceStore store;
    auto transaction = store.OpenTransaction();
    if (!transaction || !transaction->UpsertObject(MakeRecord(1001)) || !transaction->RemoveObject(PersistentObjectId{9999}))
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
    if (!transaction || !transaction->AddTombstone(tombstone) || !transaction->UpsertZoneOverride(snapshot) || !transaction->Commit())
    {
        return false;
    }

    const auto found_tombstone = store.FindTombstone(PersistentObjectId{1001});
    const auto found_zone = store.FindZoneOverride(snapshot.location);
    return store.IsTombstoned(PersistentObjectId{1001}) && found_tombstone && found_tombstone->reason == Id("cleanup.decay") &&
           found_tombstone->revision == 1u && found_zone && found_zone->record_ids.size() == 1u && found_zone->revision == 1u;
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
    if (!services || !services.Value().store)
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
        {"FailedCommitIsAtomic", TestFailedCommitIsAtomic},
        {"LazyRuleIdQueries", TestLazyRuleIdQueries},
        {"RichTombstonesAndZoneOverrides", TestRichTombstonesAndZoneOverrides},
        {"ValidationRejectsInvalidRecords", TestValidationRejectsInvalidRecords},
        {"FactoryCreatesUsableStore", TestFactoryCreatesUsableStore},
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
