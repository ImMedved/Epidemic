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

template<class T>
std::vector<std::byte> Bytes(const T& value)
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
    process_definition.timing = processes::ProcessTimingPolicy::Instant;
    auto process_definition_id = process_service.RegisterDefinition(process_definition);
    Check(static_cast<bool>(process_definition_id), "register item process definition");

    processes::ProcessRecipe process_recipe;
    process_recipe.canonical_name = "recipe.integration.smelt";
    process_recipe.process = process_definition_id.Value();
    processes::ProcessInputDefinition input;
    input.id = processes::ProcessInputId::FromString("input.ore");
    input.type = processes::ProcessInputTypeId::FromString("item");
    input.amount = 2;
    input.payload = processes::RegisteredPayload::FromTrivial(
        integration::ItemProcessInputProvider::InputPayloadType(), integration::ProcessItemInputPayload{ore_id.Value()});
    process_recipe.inputs.push_back(input);
    processes::ProcessOutputDefinition output;
    output.id = processes::ProcessOutputId::FromString("output.ingot");
    output.type = integration::ItemProcessOutputHandler::OutputType();
    output.amount = 1;
    output.payload = processes::RegisteredPayload::FromTrivial(
        integration::ItemProcessOutputHandler::OutputPayloadType(), integration::ProcessItemOutputPayload{ingot_definition_id.Value(), seller_container_id.Value()});
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
    Check(item_service.FindItemsByDefinition(ingot_definition_id.Value()).size() == 1, "process produced item output");

    knowledge::KnowledgeService knowledge_service;
    knowledge::KnowledgeProfile speaker_profile;
    speaker_profile.subject = seller;
    Check(static_cast<bool>(knowledge_service.CreateProfile(speaker_profile)), "create speaker knowledge profile");
    knowledge::KnowledgeProfile listener_profile;
    listener_profile.subject = buyer;
    Check(static_cast<bool>(knowledge_service.CreateProfile(listener_profile)), "create listener knowledge profile");
    integration::DialogueKnowledgeConsequenceHandler knowledge_handler(knowledge_service);
    dialogue::DialogueService dialogue_service;
    dialogue::DialogueConsequenceDefinition tell;
    tell.id = TypeId::FromString("consequence.tell.secret");
    tell.type = integration::DialogueKnowledgeConsequenceHandler::Type();
    integration::DialogueKnowledgePayload tell_payload;
    tell_payload.belief = knowledge::BeliefTypeId::FromString("reported");
    tell_payload.topic = knowledge::KnowledgeTopicId::FromString("integration.secret");
    tell_payload.confidence = knowledge::KnowledgeConfidence::High;
    tell_payload.subject = seller;
    tell.payload = Bytes(tell_payload);
    Check(static_cast<bool>(dialogue_service.RegisterConsequence(tell)), "register knowledge dialogue consequence");
    Check(static_cast<bool>(dialogue_service.RegisterConsequenceHandler(tell.type, knowledge_handler)), "register knowledge dialogue handler");
    dialogue::ConversationDefinition conversation;
    conversation.canonical_name = "conversation.integration.tell";
    conversation.entry_node = dialogue::DialogueNodeId::FromString("node.tell");
    dialogue::DialogueNodeDefinition tell_node;
    tell_node.id = conversation.entry_node;
    tell_node.consequences.push_back(tell.id);
    conversation.nodes.push_back(tell_node);
    auto conversation_id = dialogue_service.RegisterConversation(conversation);
    Check(static_cast<bool>(conversation_id), "register knowledge conversation");
    Check(static_cast<bool>(dialogue_service.FreezeDefinitions()), "freeze dialogue definitions");
    auto session = dialogue_service.StartConversation(conversation_id.Value(), {seller, buyer});
    Check(static_cast<bool>(session), "start knowledge conversation");
    const auto executed = dialogue_service.ExecutePendingConsequences();
    Check(executed.size() == 1, "execute knowledge transfer consequence");
    Check(knowledge_service.FindKnowledgeByTopic(buyer, tell_payload.topic).size() == 1, "dialogue transferred knowledge");

    ownership::OwnershipService ownership_service;
    ownership::OwnershipRecord sword_ownership;
    sword_ownership.property = integration::ItemPropertyRef(sword_id.Value());
    sword_ownership.owner = seller;
    sword_ownership.domain = ownership::PropertyDomainId::FromString("personal");
    Check(static_cast<bool>(ownership_service.AssignOwnership(sword_ownership)), "assign sword ownership");

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

    integration::TradeCoordinator trade(economy_service, item_service, ownership_service);
    integration::CoordinatedTradePlan trade_plan;
    trade_plan.money.buyer = buyer;
    trade_plan.money.seller = seller;
    trade_plan.money.monetary_transfers.push_back({buyer_account_id.Value(), seller_account_id.Value(), 25, coin_id.Value()});
    trade_plan.goods.push_back({sword_id.Value(), buyer_container_id.Value(), seller, buyer});
    auto trade_result = trade.Execute(std::move(trade_plan));
    Check(static_cast<bool>(trade_result), "coordinated trade");
    Check(economy_service.GetBalance(buyer_account_id.Value()) == 75, "buyer money transferred");
    Check(economy_service.GetBalance(seller_account_id.Value()) == 25, "seller money transferred");
    Check(item_service.FindItem(sword_id.Value())->location.container == buyer_container_id.Value(), "trade moved item");
    const auto* new_owner = ownership_service.GetOwner(integration::ItemPropertyRef(sword_id.Value()));
    Check(new_owner && new_owner->owner == buyer, "trade transferred ownership");

    return 0;
}
