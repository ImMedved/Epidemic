#include "Epidemic/GameFramework/ExtendedGameplayIntegration/extended_gameplay_adapters.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <type_traits>

using namespace epidemic::gameplay;
namespace items = epidemic::gameplay::items;
namespace equipment = epidemic::gameplay::equipment;
namespace dialogue = epidemic::gameplay::dialogue;
namespace economy = epidemic::gameplay::economy;
namespace knowledge = epidemic::gameplay::knowledge;
namespace ownership = epidemic::gameplay::ownership;
namespace processes = epidemic::gameplay::processes;
namespace integration = epidemic::gameplay::integration;

namespace epidemic::gameplay::integration::test_support
{
void FailNextTradePublicationForTest() noexcept;
void FailAfterTradeGoodsReservationsForTest(std::size_t accepted_goods) noexcept;
void ResetTradeCoordinatorFailureSeamsForTest() noexcept;
}

namespace
{
void Check(bool value, const char* message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char* domain, const char* id)
{
    return {GameplayDomainId::FromString(domain), GameplayObjectId::FromString(id)};
}
template<class T> std::vector<std::byte> Bytes(const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    std::vector<std::byte> out(sizeof(T));
    std::memcpy(out.data(), &value, sizeof(T));
    return out;
}
}

int main()
{
    const auto seller = Ref("actor", "seller");
    const auto buyer = Ref("actor", "buyer");
    const auto third_party = Ref("actor", "third");

    items::ItemsInventoryService item_service;
    items::ItemDefinition sword_definition;
    sword_definition.canonical_name = "item.integration.sword";
    sword_definition.stack_policy = items::ItemStackPolicy::NonStackable;
    sword_definition.tags.Add(TagId::FromString("item.weapon"));
    auto sword_definition_id = item_service.RegisterDefinition(sword_definition);
    Check(static_cast<bool>(sword_definition_id), "register sword definition");

    items::ItemDefinition ore_definition;
    ore_definition.canonical_name = "item.integration.ore";
    ore_definition.stack_policy = items::ItemStackPolicy::StackByDefinition;
    auto ore_definition_id = item_service.RegisterDefinition(ore_definition);
    Check(static_cast<bool>(ore_definition_id), "register ore definition");

    items::ItemDefinition ingot_definition;
    ingot_definition.canonical_name = "item.integration.ingot";
    ingot_definition.stack_policy = items::ItemStackPolicy::StackByDefinition;
    auto ingot_definition_id = item_service.RegisterDefinition(ingot_definition);
    Check(static_cast<bool>(ingot_definition_id), "register ingot definition");
    item_service.Freeze();

    items::ContainerRecord seller_container;
    seller_container.owner_object = seller;
    seller_container.max_slots = 32;
    auto seller_container_id = item_service.CreateContainer(seller_container);
    Check(static_cast<bool>(seller_container_id), "create seller container");
    items::ContainerRecord buyer_container;
    buyer_container.owner_object = buyer;
    buyer_container.max_slots = 32;
    auto buyer_container_id = item_service.CreateContainer(buyer_container);
    Check(static_cast<bool>(buyer_container_id), "create buyer container");

    items::ItemInstance sword;
    sword.definition = sword_definition_id.Value();
    sword.location.kind = items::ItemLocationKind::Container;
    sword.location.container = seller_container_id.Value();
    auto sword_id = item_service.CreateItem(sword);
    Check(static_cast<bool>(sword_id), "create sword");

    integration::EquipmentItemsAdapter equipment_items(item_service);
    equipment::EquipmentService equipment_service;
    equipment_service.SetItemProvider(&equipment_items);
    equipment_service.Freeze();
    equipment::EquipmentProfile equipment_profile;
    equipment_profile.subject = seller;
    auto profile_id = equipment_service.CreateProfile(equipment_profile);
    Check(static_cast<bool>(profile_id), "create equipment profile");
    equipment::EquipmentSlotDefinition hand;
    hand.type = equipment::EquipmentSlotTypeId::FromString("hand");
    hand.accepted_tags.Add(TagId::FromString("item.weapon"));
    auto hand_id = equipment_service.AddSlot(profile_id.Value(), hand);
    Check(static_cast<bool>(hand_id), "create equipment slot");
    auto equip_plan = equipment_service.PrepareEquip(seller, equipment::EquipmentItemId{sword_id.Value().value}, {hand_id.Value()});
    Check(static_cast<bool>(equip_plan), "prepare equipment through Items adapter");
    auto binding = equipment_service.CommitEquip(equip_plan.Value());
    Check(static_cast<bool>(binding), "commit equipment through Items adapter");
    Check(item_service.ReservedQuantity(sword_id.Value()) == 1, "equipped item reserved in Items");
    Check(static_cast<bool>(equipment_service.Unequip(binding.Value())), "unequip through Items adapter");
    Check(item_service.ReservedQuantity(sword_id.Value()) == 0, "equipment reservation released");

    items::ItemInstance broken_sword;
    broken_sword.definition = sword_definition_id.Value();
    broken_sword.location.kind = items::ItemLocationKind::Container;
    broken_sword.location.container = seller_container_id.Value();
    auto broken_sword_id = item_service.CreateItem(broken_sword);
    Check(static_cast<bool>(broken_sword_id), "create second sword");
    auto broken_plan = equipment_service.PrepareEquip(seller, equipment::EquipmentItemId{broken_sword_id.Value().value}, {hand_id.Value()});
    Check(static_cast<bool>(broken_plan), "prepare second equipment binding");
    auto broken_binding = equipment_service.CommitEquip(broken_plan.Value());
    Check(static_cast<bool>(broken_binding), "commit second equipment binding");
    const auto backing = item_service.FindReservations(broken_sword_id.Value());
    Check(backing.size() == 1, "second equipment binding has one backing reservation");
    Check(static_cast<bool>(item_service.ReleaseReservation(backing.front().id)), "simulate missing backing reservation");
    Check(!equipment_service.Unequip(broken_binding.Value()), "unequip detects missing backing reservation");
    Check(equipment_service.FindBinding(broken_binding.Value()) != nullptr, "failed release preserves equipment binding");

    items::ItemInstance ore;
    ore.definition = ore_definition_id.Value();
    ore.quantity = 3;
    ore.location.kind = items::ItemLocationKind::Container;
    ore.location.container = seller_container_id.Value();
    auto ore_id = item_service.CreateItem(ore);
    Check(static_cast<bool>(ore_id), "create process input item");

    processes::ProcessesService process_service;
    integration::ItemProcessInputProvider item_input(item_service);
    integration::ItemProcessOutputHandler item_output(item_service);
    process_service.SetInputProvider(&item_input);
    process_service.AddOutputHandler(&item_output);
    processes::ProcessDefinition process_definition;
    process_definition.canonical_name = "process.integration.smelt";
    process_definition.kind = processes::ProcessKindId::FromString("crafting");
    process_definition.timing = processes::ProcessTimingPolicy::Instant;
    process_definition.persistence = processes::ProcessPersistencePolicy::Persistent;
    auto process_definition_id = process_service.RegisterDefinition(process_definition);
    Check(static_cast<bool>(process_definition_id), "register item process definition");
    processes::ProcessRecipe process_recipe;
    process_recipe.canonical_name = "recipe.integration.smelt";
    process_recipe.process = process_definition_id.Value();
    processes::ProcessInputDefinition input;
    input.id = processes::ProcessInputId::FromString("input.ore");
    input.type = processes::ProcessInputTypeId::FromString("item");
    input.amount = 2;
    input.payload = integration::EncodeProcessItemInputPayload({ore_id.Value()});
    process_recipe.inputs.push_back(input);
    processes::ProcessOutputDefinition output;
    output.id = processes::ProcessOutputId::FromString("output.ingot");
    output.type = integration::ItemProcessOutputHandler::OutputType();
    output.amount = 1;
    output.payload = integration::EncodeProcessItemOutputPayload({ingot_definition_id.Value(), seller_container_id.Value()});
    process_recipe.outputs.push_back(output);
    auto recipe_id = process_service.RegisterRecipe(process_recipe);
    Check(static_cast<bool>(recipe_id), "register item process recipe");
    process_service.Freeze();
    processes::StartProcessRequest start_process;
    start_process.recipe = recipe_id.Value();
    start_process.actor = seller;
    start_process.now = GameplayTimePoint{10};
    auto process_id = process_service.StartProcess(start_process);
    Check(static_cast<bool>(process_id), "execute item-backed process");
    Check(item_service.FindItem(ore_id.Value())->quantity == 1, "process consumed item reservation");
    auto ingots = item_service.FindItemsByDefinition(ingot_definition_id.Value());
    Check(ingots.size() == 1, "process produced one item output");
    const auto* process_instance = process_service.FindInstance(process_id.Value());
    Check(process_instance && !process_instance->prepared_outputs.empty() && process_instance->prepared_outputs.front().provider_token.IsPortable(),
          "process output token is portable");
    Check(process_instance && !process_instance->reserved_inputs.empty() && process_instance->reserved_inputs.front().provider_token.IsPortable(),
          "process reservation token is portable");
    Check(static_cast<bool>(item_output.Commit(process_instance->prepared_outputs.front(), *process_instance, {})),
          "replaying committed process output is idempotent");
    Check(item_service.FindItemsByDefinition(ingot_definition_id.Value()).size() == 1,
          "idempotent process output does not duplicate item");

    knowledge::KnowledgeService knowledge_service;
    knowledge::KnowledgeProfile speaker_profile;
    speaker_profile.subject = seller;
    Check(static_cast<bool>(knowledge_service.CreateProfile(speaker_profile)), "create speaker knowledge profile");
    knowledge::KnowledgeProfile listener_profile;
    listener_profile.subject = buyer;
    Check(static_cast<bool>(knowledge_service.CreateProfile(listener_profile)), "create listener knowledge profile");
    knowledge::LearnKnowledgeRequest source_knowledge;
    source_knowledge.learner = seller;
    source_knowledge.type = knowledge::BeliefTypeId::FromString("observed");
    source_knowledge.topic.id = knowledge::KnowledgeTopicId::FromString("integration.shared.secret");
    source_knowledge.topic.primary_subject = third_party;
    source_knowledge.source_kind = knowledge::KnowledgeSourceId::FromString("direct.observation");
    source_knowledge.source_object = third_party;
    source_knowledge.epistemic_state = knowledge::KnowledgeEpistemicState::Known;
    source_knowledge.confidence = knowledge::KnowledgeConfidence::High;
    auto source_record = knowledge_service.Learn(source_knowledge);
    Check(static_cast<bool>(source_record), "speaker learns shareable knowledge");

    integration::DialogueKnowledgeConsequenceHandler knowledge_handler(knowledge_service);
    dialogue::DialogueService dialogue_service;
    dialogue::DialogueConsequenceDefinition share;
    share.id = TypeId::FromString("consequence.share.secret");
    share.type = integration::DialogueKnowledgeConsequenceHandler::ShareType();
    integration::DialogueShareKnowledgePayload share_payload;
    share_payload.record = source_record.Value();
    share.payload = Bytes(share_payload);
    Check(static_cast<bool>(dialogue_service.RegisterConsequence(share)), "register share knowledge consequence");
    dialogue::DialogueConsequenceDefinition assertion;
    assertion.id = TypeId::FromString("consequence.assert.secret");
    assertion.type = integration::DialogueKnowledgeConsequenceHandler::AssertType();
    integration::DialogueAssertKnowledgePayload assertion_payload;
    assertion_payload.belief = knowledge::BeliefTypeId::FromString("reported");
    assertion_payload.topic = knowledge::KnowledgeTopicId::FromString("integration.asserted.secret");
    assertion_payload.confidence = knowledge::KnowledgeConfidence::Medium;
    assertion_payload.subject = seller;
    assertion.payload = Bytes(assertion_payload);
    Check(static_cast<bool>(dialogue_service.RegisterConsequence(assertion)), "register assert knowledge consequence");
    Check(static_cast<bool>(dialogue_service.RegisterConsequenceHandler(share.type, knowledge_handler)), "register share knowledge handler");
    Check(static_cast<bool>(dialogue_service.RegisterConsequenceHandler(assertion.type, knowledge_handler)), "register assert knowledge handler");
    dialogue::ConversationDefinition conversation;
    conversation.canonical_name = "conversation.integration.tell";
    conversation.entry_node = dialogue::DialogueNodeId::FromString("node.tell");
    dialogue::DialogueNodeDefinition tell_node;
    tell_node.id = conversation.entry_node;
    tell_node.consequences.push_back(share.id);
    tell_node.consequences.push_back(assertion.id);
    conversation.nodes.push_back(tell_node);
    auto conversation_id = dialogue_service.RegisterConversation(conversation);
    Check(static_cast<bool>(conversation_id), "register knowledge conversation");
    Check(static_cast<bool>(dialogue_service.FreezeDefinitions()), "freeze dialogue definitions");
    auto session = dialogue_service.StartConversation(conversation_id.Value(), {seller, buyer});
    Check(static_cast<bool>(session), "start knowledge conversation");
    const auto executed = dialogue_service.ExecutePendingConsequences();
    Check(executed.size() == 2, "execute explicit share and assertion consequences");
    const auto shared = knowledge_service.FindKnowledgeByTopic(buyer, source_knowledge.topic.id);
    Check(shared.size() == 1 && shared.front().derived_from == source_record.Value(), "dialogue share preserves knowledge provenance");
    const auto asserted = knowledge_service.FindKnowledgeByTopic(buyer, assertion_payload.topic);
    Check(asserted.size() == 1 && asserted.front().source == seller, "dialogue assertion records speaker as authored source");

    ownership::OwnershipService ownership_service;
    ownership::OwnershipRecord sword_ownership;
    sword_ownership.property = integration::ItemPropertyRef(sword_id.Value());
    sword_ownership.owner = seller;
    sword_ownership.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(sword_ownership)), "assign sword ownership");
    ownership::OwnershipRecord ingot_ownership;
    ingot_ownership.property = integration::ItemPropertyRef(ingots.front().id);
    ingot_ownership.owner = seller;
    ingot_ownership.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(ingot_ownership)), "assign ingot ownership");

    economy::EconomyService economy_service;
    economy::CurrencyDefinition coin;
    coin.canonical_name = "currency.integration.coin";
    auto coin_id = economy_service.RegisterCurrency(coin);
    Check(static_cast<bool>(coin_id), "register currency");
    economy_service.Freeze();
    economy::EconomicAccount buyer_account;
    buyer_account.owner = buyer;
    buyer_account.currency = coin_id.Value();
    buyer_account.balance = 100;
    auto buyer_account_id = economy_service.CreateAccount(buyer_account);
    Check(static_cast<bool>(buyer_account_id), "create buyer account");
    economy::EconomicAccount seller_account;
    seller_account.owner = seller;
    seller_account.currency = coin_id.Value();
    auto seller_account_id = economy_service.CreateAccount(seller_account);
    Check(static_cast<bool>(seller_account_id), "create seller account");

    // G4-EXTINT-001: invalid goods validation must not consume the persisted
    // coordinator execution ID generator or publish an execution record.
    integration::TradeCoordinator validation_trade(economy_service, item_service, ownership_service);
    const auto validation_before = validation_trade.CaptureSnapshot();
    auto MakeValidationPlan = [&](items::ItemInstanceId item, items::ContainerId destination) {
        integration::CoordinatedTradePlan plan;
        plan.money.buyer = buyer;
        plan.money.seller = seller;
        plan.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 1, coin_id.Value()});
        plan.goods.push_back({item, destination, seller, buyer});
        return plan;
    };

    auto malformed = MakeValidationPlan({}, buyer_container_id.Value());
    Check(!validation_trade.Prepare(std::move(malformed)), "G4-EXTINT-001 malformed goods rejected");
    auto validation_after = validation_trade.CaptureSnapshot();
    Check(validation_after.executions.size() == validation_before.executions.size() &&
              validation_after.execution_ids.scope == validation_before.execution_ids.scope &&
              validation_after.execution_ids.next == validation_before.execution_ids.next,
          "G4-EXTINT-001 malformed goods preserve coordinator snapshot and generator");

    items::ItemInstance duplicate_probe;
    duplicate_probe.definition = ingot_definition_id.Value();
    duplicate_probe.location.kind = items::ItemLocationKind::Container;
    duplicate_probe.location.container = seller_container_id.Value();
    auto duplicate_probe_id = item_service.CreateItem(duplicate_probe);
    Check(static_cast<bool>(duplicate_probe_id), "create duplicate trade probe item");
    ownership::OwnershipRecord duplicate_probe_owner;
    duplicate_probe_owner.property = integration::ItemPropertyRef(duplicate_probe_id.Value());
    duplicate_probe_owner.owner = seller;
    duplicate_probe_owner.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(duplicate_probe_owner)), "assign duplicate trade probe ownership");

    auto duplicate_plan = MakeValidationPlan(duplicate_probe_id.Value(), buyer_container_id.Value());
    duplicate_plan.goods.push_back(duplicate_plan.goods.front());
    Check(!validation_trade.Prepare(std::move(duplicate_plan)), "G4-EXTINT-001 duplicate goods rejected");
    validation_after = validation_trade.CaptureSnapshot();
    Check(validation_after.executions.size() == validation_before.executions.size() &&
              validation_after.execution_ids.next == validation_before.execution_ids.next,
          "G4-EXTINT-001 duplicate goods preserve execution generator");

    items::ItemInstance unavailable_probe;
    unavailable_probe.definition = ingot_definition_id.Value();
    unavailable_probe.location.kind = items::ItemLocationKind::Container;
    unavailable_probe.location.container = seller_container_id.Value();
    auto unavailable_probe_id = item_service.CreateItem(unavailable_probe);
    Check(static_cast<bool>(unavailable_probe_id), "create unavailable trade probe item");
    ownership::OwnershipRecord unavailable_probe_owner;
    unavailable_probe_owner.property = integration::ItemPropertyRef(unavailable_probe_id.Value());
    unavailable_probe_owner.owner = seller;
    unavailable_probe_owner.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(unavailable_probe_owner)), "assign unavailable trade probe ownership");
    auto held_probe = item_service.ReserveItem(unavailable_probe_id.Value(), 1, seller, TypeId::FromString("test.hold"));
    Check(static_cast<bool>(held_probe), "reserve unavailable trade probe");
    auto unavailable_plan = MakeValidationPlan(unavailable_probe_id.Value(), buyer_container_id.Value());
    Check(!validation_trade.Prepare(std::move(unavailable_plan)), "G4-EXTINT-001 unavailable goods rejected");
    validation_after = validation_trade.CaptureSnapshot();
    Check(validation_after.executions.size() == validation_before.executions.size() &&
              validation_after.execution_ids.next == validation_before.execution_ids.next,
          "G4-EXTINT-001 unavailable goods preserve execution generator");
    Check(static_cast<bool>(item_service.ReleaseReservation(held_probe.Value())), "release unavailable trade probe hold");

    items::ItemInstance owner_mismatch_probe;
    owner_mismatch_probe.definition = ingot_definition_id.Value();
    owner_mismatch_probe.location.kind = items::ItemLocationKind::Container;
    owner_mismatch_probe.location.container = seller_container_id.Value();
    auto owner_mismatch_probe_id = item_service.CreateItem(owner_mismatch_probe);
    Check(static_cast<bool>(owner_mismatch_probe_id), "create owner mismatch trade probe item");
    ownership::OwnershipRecord owner_mismatch_probe_owner;
    owner_mismatch_probe_owner.property = integration::ItemPropertyRef(owner_mismatch_probe_id.Value());
    owner_mismatch_probe_owner.owner = third_party;
    owner_mismatch_probe_owner.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(owner_mismatch_probe_owner)), "assign mismatched trade probe ownership");
    auto owner_mismatch_plan = MakeValidationPlan(owner_mismatch_probe_id.Value(), buyer_container_id.Value());
    Check(!validation_trade.Prepare(std::move(owner_mismatch_plan)), "G4-EXTINT-001 ownership mismatch rejected");
    validation_after = validation_trade.CaptureSnapshot();
    Check(validation_after.executions.size() == validation_before.executions.size() &&
              validation_after.execution_ids.next == validation_before.execution_ids.next,
          "G4-EXTINT-001 ownership mismatch preserves execution generator");

    // G4-EXTINT-002: durable local publication must happen before the first
    // external reservation. A deterministic module-local seam fails exactly at
    // that boundary and proves that Items/Economy were untouched.
    items::ItemInstance publication_probe;
    publication_probe.definition = ingot_definition_id.Value();
    publication_probe.location.kind = items::ItemLocationKind::Container;
    publication_probe.location.container = seller_container_id.Value();
    auto publication_probe_id = item_service.CreateItem(publication_probe);
    Check(static_cast<bool>(publication_probe_id), "create trade publication probe item");
    ownership::OwnershipRecord publication_probe_owner;
    publication_probe_owner.property = integration::ItemPropertyRef(publication_probe_id.Value());
    publication_probe_owner.owner = seller;
    publication_probe_owner.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(publication_probe_owner)), "assign trade publication probe ownership");

    integration::TradeCoordinator publication_trade(economy_service, item_service, ownership_service);
    const auto publication_before = publication_trade.CaptureSnapshot();
    const auto items_before_publication = item_service.CaptureSnapshot();
    const auto economy_before_publication = economy_service.CaptureSnapshot();
    integration::test_support::FailNextTradePublicationForTest();
    auto publication_plan = MakeValidationPlan(publication_probe_id.Value(), buyer_container_id.Value());
    Check(!publication_trade.Prepare(std::move(publication_plan)), "G4-EXTINT-002 local publication failure is surfaced");
    const auto publication_after = publication_trade.CaptureSnapshot();
    const auto items_after_publication = item_service.CaptureSnapshot();
    const auto economy_after_publication = economy_service.CaptureSnapshot();
    Check(publication_after.executions.size() == publication_before.executions.size() &&
              publication_after.execution_ids.next == publication_before.execution_ids.next,
          "G4-EXTINT-002 failed local publication preserves coordinator state");
    Check(items_after_publication.reservations.size() == items_before_publication.reservations.size() &&
              items_after_publication.reservation_ids.next == items_before_publication.reservation_ids.next &&
              item_service.ReservedQuantity(publication_probe_id.Value()) == 0,
          "G4-EXTINT-002 failed local publication performs zero Items reservation work");
    Check(economy_after_publication.transactions.size() == economy_before_publication.transactions.size() &&
              economy_after_publication.transaction_ids.next == economy_before_publication.transaction_ids.next,
          "G4-EXTINT-002 failed local publication performs zero Economy transaction work");

    // Once one external goods reservation has been accepted, the local execution
    // already exists. A post-acceptance failure therefore remains restorable and
    // repeated Continue calls cannot reserve the remaining leg again.
    items::ItemInstance partial_probe_a;
    partial_probe_a.definition = ingot_definition_id.Value();
    partial_probe_a.location.kind = items::ItemLocationKind::Container;
    partial_probe_a.location.container = seller_container_id.Value();
    auto partial_probe_a_id = item_service.CreateItem(partial_probe_a);
    Check(static_cast<bool>(partial_probe_a_id), "create first partial trade probe");
    items::ItemInstance partial_probe_b = partial_probe_a;
    auto partial_probe_b_id = item_service.CreateItem(partial_probe_b);
    Check(static_cast<bool>(partial_probe_b_id), "create second partial trade probe");
    for (auto item : {partial_probe_a_id.Value(), partial_probe_b_id.Value()})
    {
        ownership::OwnershipRecord record;
        record.property = integration::ItemPropertyRef(item);
        record.owner = seller;
        record.domain = ownership::PropertyDomainId::FromString("personal");
        Check(static_cast<bool>(ownership_service.AssignOwnership(record)), "assign partial trade probe ownership");
    }

    integration::TradeCoordinator partial_trade(economy_service, item_service, ownership_service);
    integration::CoordinatedTradePlan partial_plan;
    partial_plan.money.buyer = buyer;
    partial_plan.money.seller = seller;
    partial_plan.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 1, coin_id.Value()});
    partial_plan.goods.push_back({partial_probe_a_id.Value(), buyer_container_id.Value(), seller, buyer});
    partial_plan.goods.push_back({partial_probe_b_id.Value(), buyer_container_id.Value(), seller, buyer});
    integration::test_support::FailAfterTradeGoodsReservationsForTest(1);
    auto partial_execution = partial_trade.Prepare(std::move(partial_plan));
    Check(static_cast<bool>(partial_execution), "G4-EXTINT-002 accepted external work returns durable reconciliation execution");
    const auto* partial_record = partial_trade.FindExecution(partial_execution.Value());
    Check(partial_record && partial_record->state == integration::CoordinatedTradeState::ReconciliationRequired &&
              partial_record->goods.size() == 2 &&
              partial_record->goods[0].state == integration::TradeGoodsLegState::Reserved &&
              partial_record->goods[1].state == integration::TradeGoodsLegState::NotReserved,
          "G4-EXTINT-002 partial accepted work is durably classified for reconciliation");
    Check(item_service.ReservedQuantity(partial_probe_a_id.Value()) == 1 &&
              item_service.ReservedQuantity(partial_probe_b_id.Value()) == 0,
          "G4-EXTINT-002 only the accepted goods leg is reserved");
    const auto partial_snapshot = partial_trade.CaptureSnapshot();
    integration::TradeCoordinator restored_partial_trade(economy_service, item_service, ownership_service);
    Check(static_cast<bool>(restored_partial_trade.RestoreSnapshot(partial_snapshot)),
          "G4-EXTINT-002 partial reconciliation execution survives snapshot restore");
    Check(!restored_partial_trade.Continue(partial_execution.Value()),
          "G4-EXTINT-002 incomplete reconciliation execution cannot be committed");
    Check(!restored_partial_trade.Continue(partial_execution.Value()),
          "G4-EXTINT-002 repeated retry remains idempotently incomplete");
    Check(item_service.ReservedQuantity(partial_probe_a_id.Value()) == 1 &&
              item_service.ReservedQuantity(partial_probe_b_id.Value()) == 0,
          "G4-EXTINT-002 retry does not double-reserve or reserve later goods");
    Check(static_cast<bool>(restored_partial_trade.Cancel(partial_execution.Value())),
          "G4-EXTINT-002 reconciliation execution can release accepted reservation");
    Check(item_service.ReservedQuantity(partial_probe_a_id.Value()) == 0,
          "G4-EXTINT-002 reconciliation cancellation releases accepted work");
    integration::test_support::ResetTradeCoordinatorFailureSeamsForTest();

    integration::TradeCoordinator trade(economy_service, item_service, ownership_service);
    integration::CoordinatedTradePlan trade_plan;
    trade_plan.money.buyer = buyer;
    trade_plan.money.seller = seller;
    trade_plan.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 25, coin_id.Value()});
    trade_plan.goods.push_back({sword_id.Value(), buyer_container_id.Value(), seller, buyer});
    auto trade_execution = trade.Prepare(std::move(trade_plan));
    Check(static_cast<bool>(trade_execution), "prepare coordinated trade saga");
    Check(item_service.ReservedQuantity(sword_id.Value()) == 1, "trade preparation reserves goods in Items owner");
    const auto trade_snapshot = trade.CaptureSnapshot();
    integration::TradeCoordinator restored_trade(economy_service, item_service, ownership_service);
    Check(static_cast<bool>(restored_trade.RestoreSnapshot(trade_snapshot)), "restore pending coordinated trade saga");
    auto trade_result = restored_trade.Continue(trade_execution.Value());
    Check(static_cast<bool>(trade_result), "continue restored coordinated trade saga");
    Check(economy_service.GetBalance(buyer_account_id.Value()) == 75, "buyer money transferred");
    Check(economy_service.GetBalance(seller_account_id.Value()) == 25, "seller money transferred");
    Check(item_service.FindItem(sword_id.Value())->location.container == buyer_container_id.Value(), "trade moved reserved item");
    const auto* new_owner = ownership_service.GetOwner(integration::ItemPropertyRef(sword_id.Value()));
    Check(new_owner && new_owner->owner == buyer, "trade transferred ownership");
    Check(static_cast<bool>(restored_trade.Continue(trade_execution.Value())), "completed trade retry is idempotent");
    Check(economy_service.GetBalance(buyer_account_id.Value()) == 75, "trade retry does not debit buyer twice");

    integration::CoordinatedTradePlan bad_parties;
    bad_parties.money.buyer = buyer;
    bad_parties.money.seller = seller;
    bad_parties.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 1, coin_id.Value()});
    bad_parties.goods.push_back({ingots.front().id, buyer_container_id.Value(), third_party, buyer});
    Check(!restored_trade.Prepare(std::move(bad_parties)), "trade rejects third-party goods owner without explicit authorization policy");

    integration::CoordinatedTradePlan stale_trade;
    stale_trade.money.buyer = buyer;
    stale_trade.money.seller = seller;
    stale_trade.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 5, coin_id.Value()});
    stale_trade.goods.push_back({ingots.front().id, buyer_container_id.Value(), seller, buyer});
    auto stale_execution = restored_trade.Prepare(std::move(stale_trade));
    Check(static_cast<bool>(stale_execution), "prepare trade used for expected ownership revision test");
    ownership::TransferOwnershipRequest external_transfer;
    external_transfer.property = integration::ItemPropertyRef(ingots.front().id);
    external_transfer.from_owner = seller;
    external_transfer.to_owner = third_party;
    external_transfer.reason = ownership::TransferReason::Gift;
    external_transfer.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.TransferOwnership(external_transfer)),
          "change ownership after trade prepare");
    Check(!restored_trade.Continue(stale_execution.Value()), "stale ownership prevents trade commit");
    Check(item_service.FindItem(ingots.front().id)->location.container == seller_container_id.Value(),
          "stale ownership is detected before item movement");
    const auto* stalled = restored_trade.FindExecution(stale_execution.Value());
    Check(stalled && stalled->state == integration::CoordinatedTradeState::ReconciliationRequired,
          "stale trade remains as durable reconciliation state");
    Check(static_cast<bool>(restored_trade.Cancel(stale_execution.Value())),
          "uncommitted reconciliation trade can release its reservations");
    Check(item_service.ReservedQuantity(ingots.front().id) == 0, "cancelled stale trade releases item reservation");

    return 0;
}
