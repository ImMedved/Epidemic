#include "Epidemic/GameFramework/Economy/economy.h"
#include <cstdlib>
#include <iostream>
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
    std::optional<PriceQuote> Quote(EconomicValueRef s, CurrencyId c, GameplayObjectRef,
                                    GameplayObjectRef) const override
    {
        return PriceQuote{s, c, 123, Revision{7}};
    }
};
} // namespace
int main()
{
    EconomyService s;
    CurrencyDefinition coin;
    coin.canonical_name = "currency.coin";
    auto cid = s.RegisterCurrency(coin);
    Check(static_cast<bool>(cid), "currency");
    Price price;
    Check(static_cast<bool>(s.AddPriceProvider(&price)), "price provider registered");
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
    Check(s.GetPriceQuote(value, cid.Value())->amount == 123, "price provider");
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
    auto scoped_offer_id = s.CreateOffer(scoped_offer);
    Check(static_cast<bool>(scoped_offer_id), "scoped offer");
    TradePlan offer_plan;
    offer_plan.monetary_transfers.push_back({bid.Value(), sid.Value(), 25, cid.Value()});
    Check(!static_cast<bool>(s.AcceptOffer(scoped_offer_id.Value(), Ref("actor", "stranger"), offer_plan)),
          "buyer scope enforced");
    auto offer_tx = s.AcceptOffer(scoped_offer_id.Value(), buyer.owner, offer_plan);
    Check(static_cast<bool>(offer_tx), "accept offer");
    Check(s.FindOffers(seller.owner).empty(), "accepted offer removed from active query");
    Check(s.FindTransaction(offer_tx.Value()) != nullptr, "accepted offer creates trade transaction");
    auto snap = s.CaptureSnapshot();
    Check(snap.offers.empty(), "terminal offers omitted from snapshot");
    Check(snap.reservations.empty(), "terminal reservations omitted from snapshot");
    Check(snap.transactions.size() == 1 && snap.transactions.front().id == offer_tx.Value() &&
          snap.transactions.front().state == TradeTransactionState::Prepared, "only live trades kept in snapshot");
    auto duplicate_debt_snapshot = snap;
    duplicate_debt_snapshot.debts.push_back(duplicate_debt_snapshot.debts.front());
    EconomyService invalid_restore;
    Check(static_cast<bool>(invalid_restore.RegisterCurrency(coin)), "invalid restore currency def");
    invalid_restore.Freeze();
    Check(!static_cast<bool>(invalid_restore.RestoreSnapshot(std::move(duplicate_debt_snapshot))), "duplicate debt snapshot rejected");
    EconomyService restored;
    Check(static_cast<bool>(restored.RegisterCurrency(coin)), "restore currency def");
    restored.Freeze();
    Check(static_cast<bool>(restored.RestoreSnapshot(std::move(snap))), "restore economy");
    Check(restored.GetBalance(bid.Value()) == 700, "balance restored");
    Check(restored.FindDebts(buyer.owner).size() == 1, "debt restored");
    return 0;
}
