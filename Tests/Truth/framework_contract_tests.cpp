#include "truth_test.h"

#include "Epidemic/GameFramework/Economy/economy.h"
#include "Epidemic/GameFramework/Entities/entities.h"
#include "Epidemic/GameFramework/ItemsInventory/items_inventory.h"
#include "Epidemic/GameFramework/Progression/progression.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"
#include "Epidemic/GameFramework/Time/gameplay_time.h"

#include <algorithm>
#include <cstddef>
#include <vector>

using namespace epidemic::gameplay;

namespace
{
GameplayObjectRef Object(std::string_view domain, std::string_view id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}

void RandomStreamsReplayExactly()
{
    random::RandomSequence first{{0xC0FFEEu}, random::RandomStream::FromString("truth.random.loot")};
    for (int index = 0; index < 37; ++index)
    {
        TRUTH_REQUIRE(first.TryNextU64().has_value());
    }
    const auto checkpoint = first.CaptureSnapshot();
    std::vector<std::uint64_t> expected;
    for (int index = 0; index < 128; ++index)
    {
        expected.push_back(*first.TryNextU64());
    }

    const auto replay = random::RandomSequence::TryFromSnapshot(checkpoint);
    TRUTH_REQUIRE(replay.has_value());
    auto restored = *replay;
    for (const auto value : expected)
    {
        TRUTH_REQUIRE(restored.TryNextU64() == value);
    }
    TRUTH_REQUIRE(!restored.TryUniform(0).has_value());
    TRUTH_REQUIRE(!random::RandomSequence::TryFromSnapshot({{1}, {}, 0}).has_value());
}

void GameplayTimeIsIndependentFromLogicalTicks()
{
    time::GameplayTimeService service;
    const auto domain = GameplayDomainId::FromString("truth.time");
    const auto clock = service.RegisterClock("truth.clock.world");
    const auto action = service.RegisterAction("truth.action.deadline", domain);
    TRUTH_REQUIRE(clock && action);
    service.Freeze();

    const GameplayObjectRef owner{domain, GameplayObjectId::FromString("deadline.owner")};
    const auto schedule = service.Schedule(clock.Value(), GameplayTimePoint{10}, owner, action.Value());
    TRUTH_REQUIRE(schedule);
    TRUTH_REQUIRE(service.SetPaused(clock.Value(), true));

    GameplayContext unrelated_logic;
    unrelated_logic.tick = GameplayTickId{10'000};
    TRUTH_REQUIRE(unrelated_logic.tick.value == 10'000);
    TRUTH_REQUIRE(service.AdvanceClock(clock.Value(), GameplayDuration{500}));
    TRUTH_REQUIRE(service.GetClock(clock.Value())->now == GameplayTimePoint{0});
    const auto paused_due = service.CollectDue(clock.Value());
    TRUTH_REQUIRE(paused_due && paused_due.Value().empty());

    TRUTH_REQUIRE(service.SetPaused(clock.Value(), false));
    TRUTH_REQUIRE(service.SetTimeScale(clock.Value(), 500));
    TRUTH_REQUIRE(service.AdvanceClock(clock.Value(), GameplayDuration{20}));
    const auto due = service.CollectDue(clock.Value());
    TRUTH_REQUIRE(due && due.Value().size() == 1);
    TRUTH_REQUIRE(due.Value().front().scheduled_for == GameplayTimePoint{10});
}

void EntityImportAndRestoreAreCollisionSafeAndAtomic()
{
    using namespace entities;
    EntityService service;
    EntityArchetypeDefinition actor;
    actor.canonical_name = "truth.entity.actor";
    const auto archetype = service.RegisterArchetype(actor);
    TRUTH_REQUIRE(archetype);
    service.Freeze();

    const auto owned_scope = GameplayObjectId::FromString("framework.entities.instances").High();
    CreateEntityRequest imported;
    imported.archetype = archetype.Value();
    imported.requested_id = EntityId::FromRaw(owned_scope, 50'000);
    const auto imported_result = service.Create(imported);
    TRUTH_REQUIRE(imported_result);
    TRUTH_REQUIRE(!service.Create(imported));

    CreateEntityRequest generated;
    generated.archetype = archetype.Value();
    const auto generated_result = service.Create(generated);
    TRUTH_REQUIRE(generated_result);
    TRUTH_REQUIRE(generated_result.Value().id.Low() > 50'000);

    auto corrupt = service.CaptureSnapshot();
    corrupt.records.push_back(corrupt.records.front());
    const auto before_revision = service.CurrentRevision();
    TRUTH_REQUIRE(!service.RestoreSnapshot(std::move(corrupt)));
    TRUTH_REQUIRE(service.CurrentRevision() == before_revision);
    TRUTH_REQUIRE(service.Exists(imported_result.Value().id));
    TRUTH_REQUIRE(service.Exists(generated_result.Value().id));
}

void ProgressReservationsNeverLeakTentativeState()
{
    using namespace progression;
    ProgressionService service;
    ProgressionTrackDefinition track_definition;
    track_definition.canonical_name = "truth.progression.experience";
    track_definition.rank_thresholds_micro = {100, 300};
    const auto track = service.RegisterTrack(track_definition);
    TRUTH_REQUIRE(track && service.Freeze());

    const auto actor = Object("truth.actor", "progression");
    TRUTH_REQUIRE(service.EnsureProfile(actor));
    TRUTH_REQUIRE(service.GrantProgress(actor, track.Value(), 80));
    const auto reservation = service.ReserveProgressGrant(actor, track.Value(), 40);
    TRUTH_REQUIRE(reservation);
    const auto during_reservation = service.GetTrack(actor, track.Value());
    TRUTH_REQUIRE(during_reservation && during_reservation.Value().progress_micro == 80);

    const auto snapshot = service.CaptureSnapshot();
    const auto profile = std::find_if(snapshot.profiles.begin(), snapshot.profiles.end(),
                                      [&](const auto& current) { return current.subject == actor; });
    TRUTH_REQUIRE(profile != snapshot.profiles.end());
    const auto state = std::find_if(profile->tracks.begin(), profile->tracks.end(),
                                    [&](const auto& current) { return current.id == track.Value(); });
    TRUTH_REQUIRE(state != profile->tracks.end());
    TRUTH_REQUIRE(state->progress_micro == 80);
    TRUTH_REQUIRE(!service.GrantProgress(actor, track.Value(), 1));

    service.CommitProgressGrant(reservation.Value());
    service.CommitProgressGrant(reservation.Value());
    const auto committed = service.GetTrack(actor, track.Value());
    TRUTH_REQUIRE(committed && committed.Value().progress_micro == 120);
}

void WorldItemIdentitySurvivesPartialTransfer()
{
    using namespace items;
    ItemsInventoryService service;
    ItemDefinition definition;
    definition.canonical_name = "truth.item.resource";
    definition.stack_policy = ItemStackPolicy::StackByDefinition;
    const auto item_definition = service.RegisterDefinition(definition);
    TRUTH_REQUIRE(item_definition);
    service.Freeze();

    ItemInstance stack;
    stack.definition = item_definition.Value();
    stack.quantity = 5;
    stack.location.kind = ItemLocationKind::World;
    stack.location.world_object = Object("truth.world", "resource.source");
    const auto source = service.CreateItem(stack);
    TRUTH_REQUIRE(source);
    TRUTH_REQUIRE(!service.SplitStack(source.Value(), 2));

    ItemLocation destination;
    destination.kind = ItemLocationKind::World;
    destination.world_object = Object("truth.world", "resource.split");
    const auto transfer = service.PrepareTransfer(source.Value(), destination, 2);
    TRUTH_REQUIRE(transfer && service.CommitTransfer(transfer.Value()));
    TRUTH_REQUIRE(service.FindItem(source.Value())->quantity == 3);
    const auto world_items = service.FindItemsByDefinition(item_definition.Value());
    TRUTH_REQUIRE(std::count_if(world_items.begin(), world_items.end(), [&](const ItemInstance& item) {
        return item.location.kind == ItemLocationKind::World &&
               (item.location.world_object == stack.location.world_object ||
                item.location.world_object == destination.world_object);
    }) == 2);

    ItemInstance duplicate = stack;
    duplicate.quantity = 1;
    TRUTH_REQUIRE(!service.CreateItem(duplicate));

    auto corrupt = service.CaptureSnapshot();
    auto duplicate_record = *std::find_if(corrupt.items.begin(), corrupt.items.end(), [](const ItemInstance& item) {
        return item.location.kind == ItemLocationKind::World;
    });
    duplicate_record.id = ItemInstanceId::FromRaw(0xBEEF, 1);
    corrupt.items.push_back(duplicate_record);
    const auto before_revision = service.CurrentRevision();
    TRUTH_REQUIRE(!service.RestoreSnapshot(std::move(corrupt)));
    TRUTH_REQUIRE(service.CurrentRevision() == before_revision);
    TRUTH_REQUIRE(service.FindItem(source.Value()) != nullptr);
}

void AcceptedOfferDefinesItsOwnSettlement()
{
    using namespace economy;
    EconomyService service;
    CurrencyDefinition currency;
    currency.canonical_name = "truth.currency.credit";
    const auto currency_id = service.RegisterCurrency(currency);
    TRUTH_REQUIRE(currency_id);
    service.Freeze();

    const auto buyer = Object("truth.actor", "buyer");
    const auto seller = Object("truth.actor", "seller");
    EconomicAccount buyer_account;
    buyer_account.owner = buyer;
    buyer_account.currency = currency_id.Value();
    buyer_account.balance = 1'000;
    EconomicAccount seller_account;
    seller_account.owner = seller;
    seller_account.currency = currency_id.Value();
    const auto buyer_id = service.CreateAccount(buyer_account);
    const auto seller_id = service.CreateAccount(seller_account);
    TRUTH_REQUIRE(buyer_id && seller_id);

    EconomicOffer offer;
    offer.seller = seller;
    offer.buyer_scope = buyer;
    offer.type = OfferTypeId::FromString("truth.offer.sale");
    offer.subject.type = EconomicValueTypeId::FromString("truth.value.item");
    offer.subject.definition = TypeId::FromString("truth.item.food");
    offer.quantity = 4;
    offer.currency = currency_id.Value();
    offer.unit_price = 25;
    const auto offer_id = service.CreateOffer(offer);
    TRUTH_REQUIRE(offer_id);
    const auto stored_offer = service.FindOffers(seller).front();

    OfferAcceptanceRequest acceptance;
    acceptance.offer = offer_id.Value();
    acceptance.expected_offer_revision = Revision{stored_offer.revision.value + 1};
    acceptance.buyer = buyer;
    acceptance.accepted_quantity = 2;
    acceptance.buyer_funding_account = buyer_id.Value();
    acceptance.seller_destination_account = seller_id.Value();
    TRUTH_REQUIRE(!service.AcceptOffer(acceptance));

    acceptance.expected_offer_revision = stored_offer.revision;
    const auto transaction = service.AcceptOffer(acceptance);
    TRUTH_REQUIRE(transaction);
    const auto* accepted = service.FindTransaction(transaction.Value());
    TRUTH_REQUIRE(accepted != nullptr);
    TRUTH_REQUIRE(accepted->source_offer == offer_id.Value());
    TRUTH_REQUIRE(accepted->accepted_quantity == 2);
    TRUTH_REQUIRE(accepted->plan.monetary_transfers.size() == 1);
    TRUTH_REQUIRE(accepted->plan.monetary_transfers.front().amount == 50);
}
} // namespace

int main()
{
    return epidemic::truth_tests::Run("framework-contracts", {
        {"random streams replay exactly", &RandomStreamsReplayExactly},
        {"gameplay time is independent from logical ticks", &GameplayTimeIsIndependentFromLogicalTicks},
        {"entity import and restore are collision-safe and atomic", &EntityImportAndRestoreAreCollisionSafeAndAtomic},
        {"progress reservations never leak tentative state", &ProgressReservationsNeverLeakTentativeState},
        {"world item identity survives partial transfer", &WorldItemIdentitySurvivesPartialTransfer},
        {"accepted offer defines its own settlement", &AcceptedOfferDefinesItsOwnSettlement},
    });
}