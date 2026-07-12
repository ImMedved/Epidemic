#include "Epidemic/Runtime/Persistence/persistent_record.h"
#include "Epidemic/Runtime/Persistence/lazy_rule_record.h"
#include "Epidemic/Runtime/Persistence/persistence_store.h"
#include "Epidemic/Runtime/Persistence/save_transaction.h"
#include "in_memory_persistence_support.h"

// File note:
// Focused module-level tests for the surrounding runtime component. Each helper builds
// a narrow fixture, and each Test* function verifies one public contract or regression.
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace
{
using epidemic::runtime::AddProtectionFlag;
using epidemic::runtime::HasProtectionFlag;
using epidemic::runtime::IDirtyTracker;
using epidemic::runtime::IPersistenceStore;
using epidemic::runtime::IPersistentObjectStore;
using epidemic::runtime::ISaveTransaction;
using epidemic::runtime::ITombstoneStore;
using epidemic::runtime::IZoneOverrideStore;
using epidemic::runtime::InMemoryDirtyTracker;
using epidemic::runtime::InMemoryPersistenceStore;
using epidemic::runtime::InMemoryPersistentObjectStore;
using epidemic::runtime::InMemorySaveTransaction;
using epidemic::runtime::InMemoryTombstoneStore;
using epidemic::runtime::InMemoryZoneOverrideStore;
using epidemic::runtime::LazyRuleKind;
using epidemic::runtime::LazyRuleRecord;
using epidemic::runtime::LazyRuleState;
using epidemic::runtime::ObjectProtectionFlags;
using epidemic::runtime::PersistenceLocation;
using epidemic::runtime::PersistenceState;
using epidemic::runtime::PersistentObjectId;
using epidemic::runtime::PersistentObjectKind;
using epidemic::runtime::PersistentObjectRecord;
using epidemic::runtime::SaveTransactionState;
using epidemic::runtime::ZoneOverrideSnapshot;

// Helper used by the tests to evaluate contains id.
bool ContainsId(const std::vector<PersistentObjectId>& values, PersistentObjectId id)
{
    return std::find(values.begin(), values.end(), id) != values.end();
}

// Builds a reusable fixture object for record.
PersistentObjectRecord MakeRecord(std::uint64_t id_value, const char* asset_path, const PersistenceLocation& location)
{
    PersistentObjectRecord record{};
    record.persistent_id = epidemic::runtime::PersistentObjectId{id_value};
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    record.asset_id = epidemic::runtime::AssetId::FromString(asset_path);
    record.kind = PersistentObjectKind::Object;
    record.location = location;
    return record;
}

// Builds a reusable fixture object for lazy rule.
LazyRuleRecord MakeLazyRule(std::uint64_t id_value, LazyRuleKind kind)
{
    LazyRuleRecord record{};
    record.target_id = PersistentObjectId{id_value};
    record.kind = kind;
    record.state = LazyRuleState::Pending;
    record.created_game_time = 10;
    record.evaluate_after_game_time = 20;
    record.rule_seed = 99;
    return record;
}

// Verifies default persistent record is neutral.
bool TestDefaultPersistentRecordIsNeutral()
{
    const PersistentObjectRecord record{};
    return !record.persistent_id.IsValid() && !record.asset_id.IsValid() && record.kind == PersistentObjectKind::Unknown &&
           record.state == PersistenceState::Clean && record.protection_flags == 0 && record.condition_hash == 0;
}

// Verifies persistence location stores region chunk and tag.
bool TestPersistenceLocationStoresRegionChunkAndTag()
{
    PersistenceLocation location{};
    location.region_id = epidemic::runtime::RegionId{3};
    location.chunk_id = epidemic::runtime::ChunkId{9};
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    location.location_tag = epidemic::foundation::StringId::FromString("surface/potato");

    return location.region_id.IsValid() && location.chunk_id.IsValid() && location.location_tag.IsValid();
}

// Verifies protection flag helpers work.
bool TestProtectionFlagHelpersWork()
{
    std::uint32_t flags = 0;
    // Function note: Handles add protection flag.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    flags = AddProtectionFlag(flags, ObjectProtectionFlags::PreventTheft);
    // Function note: Handles add protection flag.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    flags = AddProtectionFlag(flags, ObjectProtectionFlags::PreserveCondition);

    return HasProtectionFlag(flags, ObjectProtectionFlags::PreventTheft) &&
           HasProtectionFlag(flags, ObjectProtectionFlags::PreserveCondition) &&
           !HasProtectionFlag(flags, ObjectProtectionFlags::PreventCleanup);
}

// Verifies dirty tracker tracks ids.
bool TestDirtyTrackerTracksIds()
{
    InMemoryDirtyTracker tracker;
    IDirtyTracker& view = tracker;
    const PersistentObjectId first{11};
    const PersistentObjectId second{22};

    view.MarkDirty(first);
    view.MarkDirty(second);
    view.MarkClean(first);

    const auto dirty = view.CollectDirty();
    return !view.IsDirty(first) && view.IsDirty(second) && dirty.size() == 1 && dirty.front() == second;
}

// Verifies tombstone store remembers deleted ids.
bool TestTombstoneStoreRemembersDeletedIds()
{
    InMemoryTombstoneStore store;
    ITombstoneStore& view = store;
    const PersistentObjectId id{77};

    const auto result = view.AddTombstone(id);
    return result && view.IsTombstoned(id) && !view.IsTombstoned(PersistentObjectId{88});
}

// Verifies zone override store stores snapshots.
bool TestZoneOverrideStoreStoresSnapshots()
{
    InMemoryZoneOverrideStore store;
    IZoneOverrideStore& view = store;

    ZoneOverrideSnapshot snapshot{};
    snapshot.location.region_id = epidemic::runtime::RegionId{2};
    snapshot.location.chunk_id = epidemic::runtime::ChunkId{5};
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    snapshot.location.location_tag = epidemic::foundation::StringId::FromString("cell/a");
    snapshot.record_ids.push_back(PersistentObjectId{10});
    snapshot.tombstones.push_back(PersistentObjectId{20});
    snapshot.lazy_rules.push_back(MakeLazyRule(10, LazyRuleKind::Decay));

    const auto upsert = view.Upsert(snapshot);
    const auto found = view.Find(snapshot.location);
    return upsert && found.has_value() && ContainsId(found->record_ids, PersistentObjectId{10}) &&
           // Function note: Checks id.
           // Inputs/outputs: see the signature; the method consumes caller-provided values and
           // returns either a value, status flag or Result according to the surrounding API.
           // Relations: this member is part of the local runtime workflow and pairs with
           // neighboring query/update helpers defined in the same class or file.
           ContainsId(found->tombstones, PersistentObjectId{20}) && found->lazy_rules.size() == 1;
}

// Verifies lazy rule record stores decay metadata.
bool TestLazyRuleRecordStoresDecayMetadata()
{
    // Function note: Builds lazy rule.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const LazyRuleRecord record = MakeLazyRule(101, LazyRuleKind::Decay);
    return record.target_id.IsValid() && record.kind == LazyRuleKind::Decay && record.state == LazyRuleState::Pending &&
           record.evaluate_after_game_time > record.created_game_time;
}

// Verifies persistent object store upsert and find by location.
bool TestPersistentObjectStoreUpsertAndFindByLocation()
{
    InMemoryPersistentObjectStore store;
    IPersistentObjectStore& view = store;

    PersistenceLocation location{};
    location.region_id = epidemic::runtime::RegionId{4};
    location.chunk_id = epidemic::runtime::ChunkId{8};
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    location.location_tag = epidemic::foundation::StringId::FromString("world/root");

    // Function note: Builds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    PersistentObjectRecord first = MakeRecord(1001, "items/potato.itemdef", location);
    first.tier = epidemic::runtime::PersistenceTier::PlayerTouched;
    // Function note: Handles add protection flag.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    first.protection_flags = AddProtectionFlag(0, ObjectProtectionFlags::PreventTheft);
    // Function note: Handles add protection flag.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    first.protection_flags = AddProtectionFlag(first.protection_flags, ObjectProtectionFlags::PreventDecay);
    // Function note: Builds record.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const PersistentObjectRecord second = MakeRecord(1002, "items/onion.itemdef", location);

    if (!view.Upsert(first) || !view.Upsert(second))
    {
        return false;
    }

    const auto found = view.Find(first.persistent_id);
    const auto by_location = view.FindByLocation(location);
    return found.has_value() && found->asset_id == first.asset_id && found->tier == epidemic::runtime::PersistenceTier::PlayerTouched &&
           HasProtectionFlag(found->protection_flags, ObjectProtectionFlags::PreventTheft) &&
           // Function note: Checks protection flag.
           // Inputs/outputs: see the signature; the method consumes caller-provided values and
           // returns either a value, status flag or Result according to the surrounding API.
           // Relations: this member is part of the local runtime workflow and pairs with
           // neighboring query/update helpers defined in the same class or file.
           HasProtectionFlag(found->protection_flags, ObjectProtectionFlags::PreventDecay) && by_location.size() == 2;
}

// Verifies in memory persistence store stores lazy rules.
bool TestInMemoryPersistenceStoreStoresLazyRules()
{
    InMemoryPersistenceStore store;
    IPersistenceStore& view = store;

    if (!view.UpsertLazyRule(MakeLazyRule(501, LazyRuleKind::Decay)) || !view.UpsertLazyRule(MakeLazyRule(501, LazyRuleKind::Cleanup)))
    {
        return false;
    }

    const auto rules = view.FindLazyRules(PersistentObjectId{501});
    return rules.size() == 2;
}

// Verifies save transaction state transitions.
bool TestSaveTransactionStateTransitions()
{
    InMemorySaveTransaction transaction;
    if (transaction.GetState() != SaveTransactionState::Open)
    {
        return false;
    }

    const auto commit_result = transaction.Commit();
    if (!commit_result || transaction.GetState() != SaveTransactionState::Committed)
    {
        return false;
    }

    InMemorySaveTransaction rollback_transaction;
    rollback_transaction.Rollback();
    return rollback_transaction.GetState() == SaveTransactionState::RolledBack;
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.created_game_time), std::uint64_t>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_same_v<decltype(PersistentObjectRecord{}.protection_flags), std::uint32_t>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<IDirtyTracker>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<ITombstoneStore>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<IZoneOverrideStore>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<IPersistentObjectStore>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<IPersistenceStore>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::has_virtual_destructor_v<ISaveTransaction>);

    if (!TestDefaultPersistentRecordIsNeutral())
    {
        return 1;
    }

    if (!TestPersistenceLocationStoresRegionChunkAndTag())
    {
        return 2;
    }

    if (!TestProtectionFlagHelpersWork())
    {
        return 3;
    }

    if (!TestDirtyTrackerTracksIds())
    {
        return 4;
    }

    if (!TestTombstoneStoreRemembersDeletedIds())
    {
        return 5;
    }

    if (!TestZoneOverrideStoreStoresSnapshots())
    {
        return 6;
    }

    if (!TestLazyRuleRecordStoresDecayMetadata())
    {
        return 7;
    }

    if (!TestPersistentObjectStoreUpsertAndFindByLocation())
    {
        return 8;
    }

    if (!TestInMemoryPersistenceStoreStoresLazyRules())
    {
        return 9;
    }

    if (!TestSaveTransactionStateTransitions())
    {
        return 10;
    }

    return 0;
}
