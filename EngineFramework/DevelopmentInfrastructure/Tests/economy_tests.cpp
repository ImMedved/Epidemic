#include "Epidemic/GameFramework/Economy/economy.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::economy;
namespace
{
void Check(bool v, const char *m)
{
    if (!v)
    {
        std::cerr << m << '\n';
        std::exit(1);
    }
}
GameplayObjectRef Ref(const char *d, const char *i)
{
    return {GameplayDomainId::FromString(d), GameplayObjectId::FromString(i)};
}
class Price final : public IPriceProvider
{
  public:
    std::optional<PriceQuote> Quote(const PriceQuoteRequest &request) const override
    {
        PriceQuote quote;
        quote.subject = request.subject;
        quote.currency = request.currency;
        quote.market = request.market;
        quote.buyer = request.buyer;
        quote.seller = request.seller;
        quote.quantity = request.quantity;
        quote.unit_amount = 123;
        quote.dependencies_revision = Revision{7};
        return quote;
    }
};
class BadPrice final : public IPriceProvider
{
  public:
    std::optional<PriceQuote> Quote(const PriceQuoteRequest &request) const override
    {
        PriceQuote quote;
        quote.subject = request.subject;
        quote.currency = request.currency;
        quote.market = request.market;
        quote.buyer = request.buyer;
        quote.seller = request.seller;
        quote.quantity = request.quantity + 1; // invalid fingerprint, must be rejected
        quote.unit_amount = 1;
        return quote;
    }
};} // namespace
int main()
{
    EconomyService s;
    CurrencyDefinition coin;
    coin.canonical_name = "currency.coin";
    auto cid = s.RegisterCurrency(coin);
    Check(static_cast<bool>(cid), "currency");
    Price price;
    BadPrice bad_price;
    Check(static_cast<bool>(s.AddPriceProvider(PriceProviderId::FromString("provider.bad"), 100, &bad_price)), "bad provider registered");
    Check(static_cast<bool>(s.AddPriceProvider(PriceProviderId::FromString("provider.good"), 10, &price)), "price provider registered");
    Check(!static_cast<bool>(s.AddPriceProvider(PriceProviderId::FromString("provider.good"), 0, &price)), "duplicate provider id rejected");
    ContractTermsSchema terms_schema;
    terms_schema.type = ContractTypeId::FromString("contract.payload");
    terms_schema.schema = TypeId::FromString("contract.payload.v1");
    terms_schema.max_payload_bytes = 8;
    Check(static_cast<bool>(s.RegisterContractTermsSchema(terms_schema)), "contract terms schema");
    s.Freeze();
    EconomicAccount buyer;
    buyer.owner = Ref("actor", "buyer");
    buyer.currency = cid.Value();
    buyer.balance = 1000;
    auto bid = s.CreateAccount(buyer);
    Check(static_cast<bool>(bid), "buyer account");
    EconomicAccount seller;
    seller.owner = Ref("actor", "seller");
    seller.currency = cid.Value();
    seller.balance = 50;
    auto sid = s.CreateAccount(seller);
    Check(static_cast<bool>(sid), "seller account");
    auto reserve = s.ReserveFunds(bid.Value(), 200, seller.owner);
    Check(static_cast<bool>(reserve), "reserve funds");
    Check(s.GetAvailableBalance(bid.Value()) == 800, "available balance");
    Check(static_cast<bool>(s.CommitFunds(reserve.Value(), sid.Value())), "commit funds");
    Check(s.GetBalance(bid.Value()) == 800 && s.GetBalance(sid.Value()) == 250, "funds moved");
    TradePlan plan;
    plan.buyer = buyer.owner;
    plan.seller = seller.owner;
    plan.monetary_transfers.push_back({bid.Value(), sid.Value(), 100, cid.Value()});
    auto tx = s.PrepareTrade(plan);
    Check(static_cast<bool>(tx), "prepare trade");

    TradePlan duplicate_plan = plan;
    duplicate_plan.id = TradeTransactionId::FromRaw(0xE001, 0x77);
    auto explicit_tx = s.PrepareTrade(duplicate_plan);
    Check(static_cast<bool>(explicit_tx), "prepare explicit trade id");
    const auto duplicate_revision = s.CurrentRevision();
    Check(!static_cast<bool>(s.PrepareTrade(duplicate_plan)), "duplicate trade id rejected");
    Check(s.CurrentRevision() == duplicate_revision, "duplicate trade id does not mutate economy");
    Check(static_cast<bool>(s.CancelTrade(explicit_tx.Value())), "cancel explicit-id trade");

    Check(static_cast<bool>(s.ReserveTrade(tx.Value())), "reserve trade");

    // B29 regression: account state is a volatile monetary precondition and is
    // checked again after reservation, before final balance mutation.
    Check(static_cast<bool>(s.SetAccountState(bid.Value(), AccountState::Frozen)), "freeze buyer after reserve");
    Check(!static_cast<bool>(s.CommitTrade(tx.Value())), "frozen source invalidates reserved trade");
    Check(s.GetBalance(bid.Value()) == 800 && s.GetBalance(sid.Value()) == 250, "failed commit leaves balances unchanged");
    Check(static_cast<bool>(s.SetAccountState(bid.Value(), AccountState::Active)), "unfreeze buyer");
    Check(static_cast<bool>(s.CommitTrade(tx.Value())), "commit trade");
    Check(s.GetBalance(bid.Value()) == 700 && s.GetBalance(sid.Value()) == 350, "trade funds moved");

    EconomicAccount closable;
    closable.owner = Ref("actor", "closable");
    closable.currency = cid.Value();
    auto closable_id = s.CreateAccount(closable);
    Check(static_cast<bool>(closable_id), "create closable account");
    Check(static_cast<bool>(s.SetAccountState(closable_id.Value(), AccountState::Closed)), "close account");
    Check(!static_cast<bool>(s.SetAccountState(closable_id.Value(), AccountState::Active)), "closed account cannot reopen");
    MarketState market;
    market.area = Ref("area", "city");
    auto mid = s.CreateMarket(market);
    Check(static_cast<bool>(mid), "market");
    auto commodity = EconomicCommodityId::FromString("commodity.food");
    Check(
        static_cast<bool>(s.SetMarketIndicator({mid.Value(), commodity, 10, 20, 1'200'000, GameplayTimePoint{10}, {}})),
        "indicator");
    Check(s.GetMarketIndicator(mid.Value(), commodity)->price_index_micro == 1'200'000, "indicator stored");
    EconomicValueRef value;
    value.type = EconomicValueTypeId::FromString("item");
    value.definition = TypeId::FromString("item.apple");
    Check(s.GetPriceQuote(value, cid.Value())->unit_amount == 123, "invalid higher-priority quote skipped");
    DebtRecord debt;
    debt.debtor = buyer.owner;
    debt.creditor = seller.owner;
    debt.currency = cid.Value();
    debt.principal = 500;
    auto debtid = s.CreateDebt(debt);
    Check(static_cast<bool>(debtid), "debt");
    debt.id = debtid.Value();
    Check(!static_cast<bool>(s.CreateDebt(debt)), "duplicate debt rejected");

    EconomicContract contract;
    contract.parties = {buyer.owner, seller.owner};
    contract.type = ContractTypeId::FromString("contract.sale");
    contract.currency = cid.Value();
    contract.amount = 250;
    auto contract_id = s.CreateContract(contract);
    Check(static_cast<bool>(contract_id), "contract");
    contract.id = contract_id.Value();
    Check(!static_cast<bool>(s.CreateContract(contract)), "duplicate contract rejected");

    EconomicContract payload_contract;
    payload_contract.parties = {buyer.owner, seller.owner};
    payload_contract.type = terms_schema.type;
    payload_contract.currency = cid.Value();
    payload_contract.amount = 10;
    payload_contract.terms_schema = terms_schema.schema;
    payload_contract.terms_payload = {std::byte{1}, std::byte{2}};
    auto payload_contract_id = s.CreateContract(payload_contract);
    Check(static_cast<bool>(payload_contract_id), "typed contract terms accepted");
    payload_contract.id = {};
    payload_contract.terms_schema = TypeId::FromString("contract.payload.wrong");
    Check(!static_cast<bool>(s.CreateContract(payload_contract)), "unregistered contract terms rejected");

    EconomicOffer cancelled_offer;
    cancelled_offer.seller = seller.owner;
    cancelled_offer.type = OfferTypeId::FromString("offer.sell");
    cancelled_offer.subject = value;
    cancelled_offer.quantity = 1;
    cancelled_offer.currency = cid.Value();
    cancelled_offer.unit_price = 10;
    auto cancelled_offer_id = s.CreateOffer(cancelled_offer);
    Check(static_cast<bool>(cancelled_offer_id), "offer for cancel");
    Check(static_cast<bool>(s.CancelOffer(cancelled_offer_id.Value())), "cancel offer");
    Check(s.FindOffers(seller.owner).empty(), "cancelled offer removed from active query");

    EconomicOffer scoped_offer = cancelled_offer;
    scoped_offer.id = {};
    scoped_offer.buyer_scope = buyer.owner;
    scoped_offer.unit_price = 10;
    auto scoped_offer_id = s.CreateOffer(scoped_offer);
    Check(static_cast<bool>(scoped_offer_id), "scoped offer");
    const auto active_offer = s.FindOffers(seller.owner).front();
    OfferAcceptanceRequest stranger_accept;
    stranger_accept.offer = scoped_offer_id.Value();
    stranger_accept.expected_offer_revision = active_offer.revision;
    stranger_accept.buyer = Ref("actor", "stranger");
    stranger_accept.accepted_quantity = 1;
    stranger_accept.buyer_funding_account = bid.Value();
    stranger_accept.seller_destination_account = sid.Value();
    Check(!static_cast<bool>(s.AcceptOffer(stranger_accept)), "buyer scope enforced");

    OfferAcceptanceRequest accept = stranger_accept;
    accept.buyer = buyer.owner;
    auto offer_tx = s.AcceptOffer(accept);
    Check(static_cast<bool>(offer_tx), "accept offer");
    Check(s.FindOffers(seller.owner).empty(), "accepted offer removed from active query");
    const auto *accepted_tx = s.FindTransaction(offer_tx.Value());
    Check(accepted_tx != nullptr, "accepted offer creates trade transaction");
    Check(accepted_tx->source_offer == scoped_offer_id.Value() && accepted_tx->accepted_quantity == 1, "trade records offer provenance");
    Check(accepted_tx->plan.monetary_transfers.size() == 1 && accepted_tx->plan.monetary_transfers.front().amount == 10,
          "offer settlement amount is canonical and cannot be caller supplied");
    auto snap = s.CaptureSnapshot();
    Check(snap.offers.empty(), "terminal offers omitted from snapshot");
    Check(snap.reservations.empty(), "terminal reservations omitted from snapshot");
    Check(snap.transactions.size() == 1 && snap.transactions.front().id == offer_tx.Value() &&
          snap.transactions.front().state == TradeTransactionState::Prepared, "only live trades kept in snapshot");
    auto duplicate_debt_snapshot = snap;
    duplicate_debt_snapshot.debts.push_back(duplicate_debt_snapshot.debts.front());
    EconomyService invalid_restore;
    Check(static_cast<bool>(invalid_restore.RegisterCurrency(coin)), "invalid restore currency def");
    Check(static_cast<bool>(invalid_restore.RegisterContractTermsSchema(terms_schema)), "invalid restore contract schema");
    invalid_restore.Freeze();
    Check(!static_cast<bool>(invalid_restore.RestoreSnapshot(std::move(duplicate_debt_snapshot))), "duplicate debt snapshot rejected");
    auto invalid_generator_snapshot = snap;
    invalid_generator_snapshot.account_ids.scope = 0xDEAD;
    EconomyService invalid_generator_restore;
    Check(static_cast<bool>(invalid_generator_restore.RegisterCurrency(coin)), "generator restore currency def");
    invalid_generator_restore.Freeze();
    Check(!static_cast<bool>(invalid_generator_restore.RestoreSnapshot(std::move(invalid_generator_snapshot))), "foreign generator scope rejected");

    EconomyService restored;
    Check(static_cast<bool>(restored.RegisterCurrency(coin)), "restore currency def");
    Check(static_cast<bool>(restored.RegisterContractTermsSchema(terms_schema)), "restore contract schema");
    restored.Freeze();
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore economy");
    Check(restored.GetBalance(bid.Value()) == 700, "balance restored");
    Check(restored.FindDebts(buyer.owner).size() == 1, "debt restored");

    DebtRecord terminal_debt;
    terminal_debt.debtor = buyer.owner;
    terminal_debt.creditor = seller.owner;
    terminal_debt.currency = cid.Value();
    terminal_debt.principal = 5;
    auto terminal_debt_id = restored.CreateDebt(terminal_debt);
    Check(static_cast<bool>(terminal_debt_id), "terminal debt created");
    Check(!static_cast<bool>(restored.CompactDebt(terminal_debt_id.Value())), "active debt cannot compact");
    Check(static_cast<bool>(restored.ResolveDebt(terminal_debt_id.Value(), DebtState::Paid)), "debt resolved");
    Check(static_cast<bool>(restored.CompactDebt(terminal_debt_id.Value())), "terminal debt compacted");

    EconomyService journal;
    Check(static_cast<bool>(journal.RegisterCurrency(coin)), "journal currency");
    journal.Freeze();
    EconomicAccount journal_account;
    journal_account.owner = Ref("actor", "journal");
    journal_account.currency = cid.Value();
    auto journal_id = journal.CreateAccount(journal_account);
    Check(static_cast<bool>(journal_id), "journal account");
    for (int i = 0; i < 4200; ++i) Check(static_cast<bool>(journal.Credit(journal_id.Value(), 1)), "journal credit");
    Check(journal.ReadChangesSince(ChangeCursor{}).snapshot_required, "bounded journal requires snapshot for stale reader");

    auto overflow_snapshot = restored.CaptureSnapshot();
    overflow_snapshot.revision = Revision{std::numeric_limits<std::uint64_t>::max()};
    EconomyService exhausted;
    Check(static_cast<bool>(exhausted.RegisterCurrency(coin)), "exhausted restore currency def");
    Check(static_cast<bool>(exhausted.RegisterContractTermsSchema(terms_schema)), "exhausted restore contract schema");
    exhausted.Freeze();
    Check(static_cast<bool>(exhausted.RestoreSnapshot(std::move(overflow_snapshot))), "max revision snapshot restores read-only state");
    const auto markets_before = exhausted.CaptureSnapshot().markets.size();
    MarketState overflow_market;
    overflow_market.area = Ref("area", "overflow");
    const auto overflow_create = exhausted.CreateMarket(overflow_market);
    Check(!overflow_create && overflow_create.GetError().HasCode("gameplay.revision_exhausted"),
          "mutation rejects exhausted revision with controlled error");
    Check(exhausted.CurrentRevision().value == std::numeric_limits<std::uint64_t>::max(),
          "failed exhausted mutation does not wrap revision");
    Check(exhausted.CaptureSnapshot().markets.size() == markets_before,
          "failed exhausted mutation leaves authoritative state unchanged");

    // ECO-09/ECO-10: runtime records require the frozen registry and canonical initial lifecycle states.
    EconomyService prefreeze;
    auto prefreeze_currency = prefreeze.RegisterCurrency(coin);
    Check(static_cast<bool>(prefreeze_currency), "prefreeze currency");
    EconomicOffer prefreeze_offer;
    prefreeze_offer.seller = Ref("actor", "prefreeze-seller");
    prefreeze_offer.type = OfferTypeId::FromString("offer.prefreeze");
    prefreeze_offer.subject = value;
    prefreeze_offer.quantity = 1;
    prefreeze_offer.currency = prefreeze_currency.Value();
    prefreeze_offer.unit_price = 1;
    DebtRecord prefreeze_debt;
    prefreeze_debt.debtor = Ref("actor", "prefreeze-debtor");
    prefreeze_debt.creditor = Ref("actor", "prefreeze-creditor");
    prefreeze_debt.currency = prefreeze_currency.Value();
    prefreeze_debt.principal = 1;
    EconomicContract prefreeze_contract;
    prefreeze_contract.parties = {prefreeze_debt.debtor, prefreeze_debt.creditor};
    prefreeze_contract.type = ContractTypeId::FromString("contract.prefreeze");
    prefreeze_contract.currency = prefreeze_currency.Value();
    prefreeze_contract.amount = 1;
    Check(!static_cast<bool>(prefreeze.CreateOffer(prefreeze_offer)), "offer creation before freeze rejected");
    Check(!static_cast<bool>(prefreeze.CreateDebt(prefreeze_debt)), "debt creation before freeze rejected");
    Check(!static_cast<bool>(prefreeze.CreateContract(prefreeze_contract)), "contract creation before freeze rejected");
    Check(prefreeze.CaptureSnapshot().offers.empty() && prefreeze.CaptureSnapshot().debts.empty() &&
              prefreeze.CaptureSnapshot().contracts.empty(),
          "prefreeze failures do not publish runtime state");

    EconomyService invalid_states;
    auto invalid_states_currency = invalid_states.RegisterCurrency(coin);
    Check(static_cast<bool>(invalid_states_currency), "invalid states currency");
    invalid_states.Freeze();
    EconomicAccount invalid_account;
    invalid_account.owner = Ref("actor", "invalid-account");
    invalid_account.currency = invalid_states_currency.Value();
    invalid_account.state = static_cast<AccountState>(99);
    Check(!static_cast<bool>(invalid_states.CreateAccount(invalid_account)), "invalid account state rejected");
    auto invalid_offer = prefreeze_offer;
    invalid_offer.currency = invalid_states_currency.Value();
    invalid_offer.state = static_cast<OfferState>(99);
    Check(!static_cast<bool>(invalid_states.CreateOffer(invalid_offer)), "invalid offer state rejected");
    auto invalid_debt = prefreeze_debt;
    invalid_debt.currency = invalid_states_currency.Value();
    invalid_debt.state = static_cast<DebtState>(99);
    Check(!static_cast<bool>(invalid_states.CreateDebt(invalid_debt)), "invalid debt state rejected");
    auto invalid_contract = prefreeze_contract;
    invalid_contract.currency = invalid_states_currency.Value();
    invalid_contract.state = static_cast<ContractState>(99);
    Check(!static_cast<bool>(invalid_states.CreateContract(invalid_contract)), "invalid contract state rejected");
    Check(invalid_states.CaptureSnapshot().accounts.empty() && invalid_states.CaptureSnapshot().offers.empty() &&
              invalid_states.CaptureSnapshot().debts.empty() && invalid_states.CaptureSnapshot().contracts.empty(),
          "invalid lifecycle values leave state unchanged");

    // ECO-03/ECO-02: aggregate availability is validated before reservation IDs/state are published.
    EconomyService aggregate;
    auto aggregate_currency = aggregate.RegisterCurrency(coin);
    Check(static_cast<bool>(aggregate_currency), "aggregate currency");
    aggregate.Freeze();
    EconomicAccount aggregate_source;
    aggregate_source.owner = Ref("actor", "aggregate-source");
    aggregate_source.currency = aggregate_currency.Value();
    aggregate_source.balance = 100;
    EconomicAccount aggregate_destination;
    aggregate_destination.owner = Ref("actor", "aggregate-destination");
    aggregate_destination.currency = aggregate_currency.Value();
    auto aggregate_source_id = aggregate.CreateAccount(aggregate_source);
    auto aggregate_destination_id = aggregate.CreateAccount(aggregate_destination);
    Check(static_cast<bool>(aggregate_source_id) && static_cast<bool>(aggregate_destination_id), "aggregate accounts");
    TradePlan aggregate_plan;
    aggregate_plan.buyer = aggregate_source.owner;
    aggregate_plan.seller = aggregate_destination.owner;
    aggregate_plan.monetary_transfers = {
        {aggregate_source_id.Value(), aggregate_destination_id.Value(), 60, aggregate_currency.Value()},
        {aggregate_source_id.Value(), aggregate_destination_id.Value(), 60, aggregate_currency.Value()}};
    auto aggregate_tx = aggregate.PrepareTrade(aggregate_plan);
    Check(static_cast<bool>(aggregate_tx), "aggregate trade prepared");
    const auto aggregate_before = aggregate.CaptureSnapshot();
    auto aggregate_reserve = aggregate.ReserveTrade(aggregate_tx.Value());
    const auto aggregate_after = aggregate.CaptureSnapshot();
    Check(!aggregate_reserve, "aggregate over-reservation rejected");
    Check(aggregate_before.reservation_ids.next == aggregate_after.reservation_ids.next &&
              aggregate_before.reservations.size() == aggregate_after.reservations.size(),
          "failed aggregate reservation consumes no IDs or reservations");
    const auto *aggregate_tx_after = aggregate.FindTransaction(aggregate_tx.Value());
    Check(aggregate_tx_after != nullptr && aggregate_tx_after->state == TradeTransactionState::Prepared &&
              aggregate_tx_after->reservations.empty(),
          "failed aggregate reservation leaves transaction prepared");

    // ECO-08: offer expiration is blocked atomically at revision exhaustion.
    EconomyService expiration_seed;
    auto expiration_currency = expiration_seed.RegisterCurrency(coin);
    Check(static_cast<bool>(expiration_currency), "expiration currency");
    expiration_seed.Freeze();
    EconomicOffer expiring_offer;
    expiring_offer.seller = Ref("actor", "expiration-seller");
    expiring_offer.type = OfferTypeId::FromString("offer.expiring");
    expiring_offer.subject = value;
    expiring_offer.quantity = 1;
    expiring_offer.currency = expiration_currency.Value();
    expiring_offer.unit_price = 1;
    expiring_offer.expires_at = GameplayTimePoint{10};
    auto expiring_offer_id = expiration_seed.CreateOffer(expiring_offer);
    Check(static_cast<bool>(expiring_offer_id), "expiring offer created");
    auto expiration_snapshot = expiration_seed.CaptureSnapshot();
    expiration_snapshot.revision = Revision{std::numeric_limits<std::uint64_t>::max()};
    for (auto& offer_record : expiration_snapshot.offers)
        offer_record.revision = expiration_snapshot.revision;
    EconomyService expiration_exhausted;
    auto expiration_exhausted_currency = expiration_exhausted.RegisterCurrency(coin);
    Check(static_cast<bool>(expiration_exhausted_currency), "expiration exhausted currency");
    expiration_exhausted.Freeze();
    Check(static_cast<bool>(expiration_exhausted.RestoreSnapshot(expiration_snapshot)), "expiration exhausted restore");
    auto expiration_result = expiration_exhausted.ExpireOffers(GameplayTimePoint{11});
    Check(!expiration_result && expiration_result.GetError().HasCode("gameplay.revision_exhausted"),
          "expiration rejects exhausted revision");
    Check(expiration_exhausted.FindOffers(expiring_offer.seller).size() == 1,
          "failed expiration leaves active offer intact");

    // ECO-11: malformed lifecycle state in restore is rejected without replacing live state.
    const auto live_balance_before_bad_restore = restored.GetBalance(bid.Value());
    auto bad_lifecycle_snapshot = restored.CaptureSnapshot();
    Check(!bad_lifecycle_snapshot.accounts.empty(), "bad lifecycle seed has account");
    bad_lifecycle_snapshot.accounts.front().state = static_cast<AccountState>(99);
    Check(!static_cast<bool>(restored.RestoreSnapshot(std::move(bad_lifecycle_snapshot))),
          "invalid lifecycle snapshot rejected");
    Check(restored.GetBalance(bid.Value()) == live_balance_before_bad_restore,
          "failed lifecycle restore preserves live economy");

    return 0;
}
