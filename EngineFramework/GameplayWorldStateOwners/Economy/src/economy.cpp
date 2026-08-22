#include "Epidemic/GameFramework/Economy/economy.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>
namespace epidemic::gameplay::economy
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
bool CheckedAdd(Fixed a, Fixed b, Fixed &out) noexcept
{
    if ((b > 0 && a > std::numeric_limits<Fixed>::max() - b) ||
        (b < 0 && a < std::numeric_limits<Fixed>::min() - b))
        return false;
    out = a + b;
    return true;
}
bool IsTerminalReservation(FundsReservationState s) noexcept
{
    return s == FundsReservationState::Committed || s == FundsReservationState::Released;
}
bool IsLiveTrade(TradeTransactionState s) noexcept
{
    return s == TradeTransactionState::Prepared || s == TradeTransactionState::Reserved;
}
bool IsTerminalOffer(OfferState s) noexcept
{
    return s == OfferState::Accepted || s == OfferState::Expired || s == OfferState::Cancelled;
}
bool SameObject(GameplayObjectRef a, GameplayObjectRef b) noexcept
{
    return a.domain == b.domain && a.id == b.id;
}
} // namespace
EconomyService::EconomyService() = default;
foundation::Result<void> EconomyService::AddPriceProvider(const IPriceProvider *provider)
{
    if (frozen_ || !provider)
        return foundation::Result<void>::Failure(Error("gameplay.economy.invalid_price_provider", "invalid price provider"));
    price_providers_.push_back(provider);
    return foundation::Result<void>::Success();
}
foundation::Result<CurrencyId> EconomyService::RegisterCurrency(CurrencyDefinition d)
{
    if (frozen_)
        return foundation::Result<CurrencyId>::Failure(Error("gameplay.economy.frozen", "economy definitions frozen"));
    if (d.canonical_name.empty())
        return foundation::Result<CurrencyId>::Failure(Error("gameplay.economy.invalid_currency", "currency canonical name missing"));
    const auto expected = CurrencyId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = expected;
    if (!d.id.IsValid() || d.id != expected || d.smallest_unit <= 0 || currencies_.contains(d.id))
        return foundation::Result<CurrencyId>::Failure(
            Error("gameplay.economy.invalid_currency", "invalid/duplicate currency"));
    d.revision = Revision{1};
    const auto id = d.id;
    currencies_.emplace(id, std::move(d));
    return foundation::Result<CurrencyId>::Success(id);
}
foundation::Result<EconomicAccountId> EconomyService::CreateAccount(EconomicAccount a)
{
    if (!frozen_) return foundation::Result<EconomicAccountId>::Failure(Error("gameplay.registry_not_frozen", "economy definitions must be frozen before runtime mutation"));
    auto amount_valid = ValidateMoneyAmount(a.currency, a.balance, true);
    if (!a.owner.IsValid() || !currencies_.contains(a.currency) || a.balance < 0 || !amount_valid)
        return foundation::Result<EconomicAccountId>::Failure(
            Error("gameplay.economy.invalid_account", "invalid account"));
    if (!a.id.IsValid()) a.id = EconomicAccountId{account_ids_.Next()};
    if (!a.id.IsValid() || accounts_.contains(a.id))
        return foundation::Result<EconomicAccountId>::Failure(
            Error("gameplay.economy.duplicate_account", "duplicate account"));
    Bump();
    a.revision = revision_;
    const auto id = a.id;
    accounts_.emplace(id, a);
    ++diagnostics_.accounts;
    Record({0, EconomyChangeKind::AccountCreated, a.owner, id, {}, a.balance, {}, revision_});
    return foundation::Result<EconomicAccountId>::Success(id);
}
const EconomicAccount *EconomyService::FindAccount(EconomicAccountId id) const noexcept
{
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : &it->second;
}
std::optional<EconomicAccount> EconomyService::FindAccountCopy(EconomicAccountId id) const noexcept
{
    const auto* account = FindAccount(id);
    return account ? std::optional<EconomicAccount>{*account} : std::nullopt;
}
foundation::Result<void> EconomyService::SetAccountState(EconomicAccountId id, AccountState state, GameplayContext c)
{
    auto it = accounts_.find(id);
    if (it == accounts_.end()) return foundation::Result<void>::Failure(Error("gameplay.economy.account_missing", "account missing"));
    const auto old = it->second.state;
    if (old == state) return foundation::Result<void>::Success();
    const bool allowed = (old == AccountState::Active && (state == AccountState::Frozen || state == AccountState::Closed)) ||
                         (old == AccountState::Frozen && (state == AccountState::Active || state == AccountState::Closed));
    if (!allowed) return foundation::Result<void>::Failure(Error("gameplay.economy.account_state", "invalid account state transition"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    EconomyChange change{0, EconomyChangeKind::AccountStateChanged, it->second.owner, id, {}, 0, c, revision_};
    change.currency = it->second.currency;
    change.old_account_state = old;
    change.new_account_state = state;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

Fixed EconomyService::GetBalance(EconomicAccountId id) const noexcept
{
    const auto *a = FindAccount(id);
    return a ? a->balance : 0;
}
Fixed EconomyService::GetAvailableBalance(EconomicAccountId id) const noexcept
{
    Fixed reserved = 0;
    for (const auto &[rid, v] : reservations_)
    {
        (void)rid;
        if (v.account == id && v.state == FundsReservationState::Active)
        {
            Fixed next = 0;
            if (!CheckedAdd(reserved, v.amount, next))
                return 0;
            reserved = next;
        }
    }
    return std::max<Fixed>(0, GetBalance(id) - reserved);
}
foundation::Result<void> EconomyService::Credit(EconomicAccountId id, Fixed amount, GameplayContext c)
{
    auto it = accounts_.find(id);
    if (it == accounts_.end() || it->second.state == AccountState::Closed || !ValidateMoneyAmount(it->second.currency, amount, true))
        return foundation::Result<void>::Failure(Error("gameplay.economy.credit_invalid", "credit invalid"));
    if (amount == 0)
        return foundation::Result<void>::Success();
    Fixed next = 0;
    if (!CheckedAdd(it->second.balance, amount, next))
        return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "balance overflow"));
    Bump();
    it->second.balance = next;
    it->second.revision = revision_;
    Record({0, EconomyChangeKind::BalanceChanged, it->second.owner, id, {}, amount, c, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> EconomyService::Debit(EconomicAccountId id, Fixed amount, GameplayContext c)
{
    auto it = accounts_.find(id);
    if (it == accounts_.end() || it->second.state != AccountState::Active ||
        !ValidateMoneyAmount(it->second.currency, amount, true) || GetAvailableBalance(id) < amount)
        return foundation::Result<void>::Failure(
            Error("gameplay.economy.insufficient_funds", "debit exceeds available funds"));
    if (amount == 0)
        return foundation::Result<void>::Success();
    Bump();
    it->second.balance -= amount;
    it->second.revision = revision_;
    Record({0, EconomyChangeKind::BalanceChanged, it->second.owner, id, {}, -amount, c, revision_});
    return foundation::Result<void>::Success();
}
FundsReservation *EconomyService::MutableReservation(FundsReservationId id) noexcept
{
    auto it = reservations_.find(id);
    return it == reservations_.end() ? nullptr : &it->second;
}
foundation::Result<FundsReservationId> EconomyService::ReserveFunds(EconomicAccountId account, Fixed amount,
                                                                    GameplayObjectRef beneficiary, TypeId reason,
                                                                    GameplayContext c)
{
    const auto *a = FindAccount(account);
    if (!a || a->state != AccountState::Active || !ValidateMoneyAmount(a->currency, amount, false) || GetAvailableBalance(account) < amount)
        return foundation::Result<FundsReservationId>::Failure(
            Error("gameplay.economy.insufficient_funds", "funds unavailable"));
    FundsReservation r;
    r.id = FundsReservationId{reservation_ids_.Next()};
    if (!r.id.IsValid())
        return foundation::Result<FundsReservationId>::Failure(Error("gameplay.economy.id_exhausted", "reservation id exhausted"));
    r.account = account;
    r.amount = amount;
    r.beneficiary = beneficiary;
    r.reason = reason;
    Bump();
    r.revision = revision_;
    const auto id = r.id;
    reservations_.emplace(id, r);
    ++diagnostics_.active_reservations;
    Record({0, EconomyChangeKind::FundsReserved, a->owner, account, {}, amount, c, revision_});
    return foundation::Result<FundsReservationId>::Success(id);
}
foundation::Result<void> EconomyService::ReleaseFunds(FundsReservationId id, GameplayContext c)
{
    auto it = reservations_.find(id);
    if (it == reservations_.end() || it->second.state != FundsReservationState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.economy.reservation_missing", "active funds reservation missing"));
    const auto copy = it->second;
    Bump();
    reservations_.erase(it);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    EconomyChange change{0, EconomyChangeKind::FundsReleased, {}, copy.account, {}, copy.amount, c, revision_};
    change.reservation = copy.id;
    change.source_account = copy.account;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

foundation::Result<void> EconomyService::CommitFunds(FundsReservationId id, EconomicAccountId destination,
                                                     GameplayContext c)
{
    auto *r = MutableReservation(id);
    auto from = r ? accounts_.find(r->account) : accounts_.end();
    auto to = accounts_.find(destination);
    if (!r || r->state != FundsReservationState::Active || from == accounts_.end() || to == accounts_.end() ||
        r->account == destination || from->second.currency != to->second.currency || from->second.state != AccountState::Active ||
        to->second.state == AccountState::Closed || from->second.balance < r->amount ||
        (r->beneficiary.IsValid() && !SameObject(to->second.owner, r->beneficiary)))
        return foundation::Result<void>::Failure(
            Error("gameplay.economy.commit_invalid", "funds reservation cannot be committed"));
    Fixed to_balance = 0;
    if (!CheckedAdd(to->second.balance, r->amount, to_balance))
        return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "balance overflow"));
    Bump();
    from->second.balance -= r->amount;
    to->second.balance = to_balance;
    from->second.revision = to->second.revision = revision_;
    const auto reservation_copy = *r;
    reservations_.erase(id);
    if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
    EconomyChange change{0, EconomyChangeKind::FundsCommitted, to->second.owner, destination, {}, reservation_copy.amount, c, revision_};
    change.reservation = reservation_copy.id;
    change.currency = from->second.currency;
    change.source_account = reservation_copy.account;
    change.destination_account = destination;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}
foundation::Result<MarketId> EconomyService::CreateMarket(MarketState m)
{
    if (!m.id.IsValid())
        m.id = MarketId{market_ids_.Next()};
    if (markets_.contains(m.id))
        return foundation::Result<MarketId>::Failure(Error("gameplay.economy.duplicate_market", "duplicate market"));
    Bump();
    m.revision = revision_;
    const auto id = m.id;
    markets_.emplace(id, m);
    ++diagnostics_.markets;
    return foundation::Result<MarketId>::Success(id);
}
foundation::Result<void> EconomyService::SetMarketIndicator(MarketIndicator i)
{
    if (!markets_.contains(i.market) || !i.commodity.IsValid() || i.supply < 0 || i.demand < 0 || i.price_index_micro < 0)
        return foundation::Result<void>::Failure(Error("gameplay.economy.invalid_indicator", "invalid market indicator"));
    const IndicatorKey key{i.market, i.commodity};
    const auto current = indicators_.find(key);
    if (current != indicators_.end())
    {
        if (i.updated_at < current->second.updated_at)
            return foundation::Result<void>::Failure(Error("gameplay.economy.stale_indicator", "market indicator update is older than current state"));
        if (i.supply == current->second.supply && i.demand == current->second.demand &&
            i.price_index_micro == current->second.price_index_micro && i.updated_at == current->second.updated_at)
            return foundation::Result<void>::Success();
    }
    Bump();
    i.revision = revision_;
    indicators_[key] = i;
    EconomyChange change{0, EconomyChangeKind::MarketChanged, {}, {}, {}, i.price_index_micro, {}, revision_};
    change.market = i.market;
    change.commodity = i.commodity;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

std::optional<MarketIndicator> EconomyService::GetMarketIndicator(MarketId m, EconomicCommodityId c) const
{
    auto it = indicators_.find({m, c});
    return it == indicators_.end() ? std::nullopt : std::optional<MarketIndicator>{it->second};
}
std::optional<PriceQuote> EconomyService::GetPriceQuote(EconomicValueRef subject, CurrencyId currency,
                                                        GameplayObjectRef buyer, GameplayObjectRef seller) const
{
    PriceQuoteRequest request;
    request.subject = subject;
    request.currency = currency;
    request.buyer = buyer;
    request.seller = seller;
    return GetPriceQuote(request);
}
std::optional<PriceQuote> EconomyService::GetPriceQuote(const PriceQuoteRequest &request) const
{
    if (!currencies_.contains(request.currency) || request.quantity <= 0 ||
        (request.market && !markets_.contains(*request.market)))
        return std::nullopt;
    for (const auto *p : price_providers_)
        if (p)
        {
            auto q = p->Quote(request);
            if (q && q->subject.type == request.subject.type && q->subject.object == request.subject.object &&
                q->subject.definition == request.subject.definition && q->currency == request.currency &&
                ValidateMoneyAmount(q->currency, q->amount, false))
                return q;
        }
    return std::nullopt;
}
foundation::Result<OfferId> EconomyService::CreateOffer(EconomicOffer o)
{
    if (!o.seller.IsValid() || !o.type.IsValid() || !o.subject.type.IsValid() || !currencies_.contains(o.currency) ||
        o.quantity <= 0 || !ValidateMoneyAmount(o.currency, o.unit_price, true))
        return foundation::Result<OfferId>::Failure(Error("gameplay.economy.invalid_offer", "invalid economic offer"));
    if (!o.id.IsValid())
        o.id = OfferId{offer_ids_.Next()};
    if (!o.id.IsValid() || offers_.contains(o.id))
        return foundation::Result<OfferId>::Failure(Error("gameplay.economy.duplicate_offer", "duplicate offer"));
    Bump();
    o.revision = revision_;
    const auto id = o.id;
    offers_.emplace(id, o);
    ++diagnostics_.offers;
    Record({0, EconomyChangeKind::OfferCreated, o.seller, {}, {}, o.unit_price, {}, revision_});
    return foundation::Result<OfferId>::Success(id);
}
foundation::Result<void> EconomyService::CancelOffer(OfferId id, GameplayContext c)
{
    auto it = offers_.find(id);
    if (it == offers_.end())
        return foundation::Result<void>::Failure(Error("gameplay.economy.offer_missing", "offer missing"));
    if (it->second.state == OfferState::Cancelled)
        return foundation::Result<void>::Success();
    if (it->second.state != OfferState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.economy.offer_state", "offer is not active"));
    const auto copy = it->second;
    Bump();
    offers_.erase(it);
    EconomyChange change{0, EconomyChangeKind::OfferChanged, copy.seller, {}, {}, 0, c, revision_};
    change.offer = copy.id;
    change.currency = copy.currency;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}
foundation::Result<TradeTransactionId> EconomyService::AcceptOffer(OfferId id, GameplayObjectRef buyer, TradePlan p)
{
    auto offer_it = offers_.find(id);
    if (offer_it == offers_.end())
        return foundation::Result<TradeTransactionId>::Failure(Error("gameplay.economy.offer_missing", "offer missing"));
    const auto &offer = offer_it->second;
    if (offer.state != OfferState::Active || !buyer.IsValid() ||
        (offer.buyer_scope.IsValid() && offer.buyer_scope != buyer))
        return foundation::Result<TradeTransactionId>::Failure(
            Error("gameplay.economy.offer_state", "offer cannot be accepted"));
    if (p.buyer.IsValid() && p.buyer != buyer)
        return foundation::Result<TradeTransactionId>::Failure(
            Error("gameplay.economy.offer_buyer_mismatch", "trade buyer does not match offer buyer"));
    if (p.seller.IsValid() && p.seller != offer.seller)
        return foundation::Result<TradeTransactionId>::Failure(
            Error("gameplay.economy.offer_seller_mismatch", "trade seller does not match offer seller"));
    auto context = p.context;
    auto seller = offer.seller;
    p.buyer = buyer;
    p.seller = seller;
    auto prepared = PrepareTrade(std::move(p));
    if (!prepared)
        return prepared;
    auto accepted_it = offers_.find(id);
    if (accepted_it == offers_.end() || accepted_it->second.state != OfferState::Active)
    {
        auto cancel_result = CancelTrade(prepared.Value());
        (void)cancel_result;
        return foundation::Result<TradeTransactionId>::Failure(
            Error("gameplay.economy.offer_state", "offer changed while accepting"));
    }
    const auto accepted = accepted_it->second;
    Bump();
    offers_.erase(accepted_it);
    if (diagnostics_.offers > 0) --diagnostics_.offers;
    const auto transaction = prepared.Value();
    EconomyChange change{0, EconomyChangeKind::OfferChanged, seller, {}, transaction, 0, context, revision_};
    change.offer = accepted.id;
    change.currency = accepted.currency;
    Record(std::move(change));
    return foundation::Result<TradeTransactionId>::Success(transaction);
}
std::vector<EconomicOffer> EconomyService::FindOffers(GameplayObjectRef seller, OfferState state) const
{
    std::vector<EconomicOffer> out;
    for (const auto &[id, o] : offers_)
    {
        (void)id;
        if (o.state == state && (!seller.IsValid() || o.seller == seller))
            out.push_back(o);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<OfferId> EconomyService::ExpireOffers(GameplayTimePoint now, GameplayContext c)
{
    std::vector<OfferId> out;
    for (const auto &[id, o] : offers_)
        if (o.state == OfferState::Active && o.expires_at.ticks != 0 && o.expires_at <= now) out.push_back(id);
    std::sort(out.begin(), out.end());
    for (const auto id : out)
    {
        const auto it = offers_.find(id);
        if (it == offers_.end()) continue;
        const auto copy = it->second;
        Bump();
        offers_.erase(it);
        EconomyChange change{0, EconomyChangeKind::OfferChanged, copy.seller, {}, {}, 0, c, revision_};
        change.offer = copy.id;
        change.currency = copy.currency;
        Record(std::move(change));
    }
    return out;
}

foundation::Result<void> EconomyService::ValidateMoneyAmount(CurrencyId currency, Fixed amount, bool allow_zero) const
{
    auto it = currencies_.find(currency);
    if (it == currencies_.end() || amount < 0 || (!allow_zero && amount == 0) ||
        (it->second.smallest_unit > 1 && amount % it->second.smallest_unit != 0))
        return foundation::Result<void>::Failure(Error("gameplay.economy.invalid_money", "invalid money amount"));
    return foundation::Result<void>::Success();
}
foundation::Result<void> EconomyService::ValidateTransfer(const MonetaryTransfer &t) const
{
    const auto *a = FindAccount(t.from), *b = FindAccount(t.to);
    if (!a || !b || t.from == t.to || a->currency != t.currency || b->currency != t.currency ||
        a->state != AccountState::Active || b->state == AccountState::Closed ||
        !ValidateMoneyAmount(t.currency, t.amount, false) || GetAvailableBalance(t.from) < t.amount)
        return foundation::Result<void>::Failure(
            Error("gameplay.economy.transfer_invalid", "invalid monetary transfer"));
    return foundation::Result<void>::Success();
}
foundation::Result<TradeTransactionId> EconomyService::PrepareTrade(TradePlan p)
{
    if (!frozen_) return foundation::Result<TradeTransactionId>::Failure(Error("gameplay.registry_not_frozen", "economy definitions must be frozen before runtime mutation"));
    if (!p.buyer.IsValid() || !p.seller.IsValid() || SameObject(p.buyer, p.seller))
        return foundation::Result<TradeTransactionId>::Failure(Error("gameplay.economy.trade_invalid", "trade requires distinct valid buyer and seller"));
    for (const auto &t : p.monetary_transfers)
    {
        auto v = ValidateTransfer(t);
        if (!v) return foundation::Result<TradeTransactionId>::Failure(v.GetError());
    }
    if (!p.id.IsValid()) p.id = TradeTransactionId{transaction_ids_.Next()};
    if (!p.id.IsValid() || transactions_.contains(p.id))
        return foundation::Result<TradeTransactionId>::Failure(Error("gameplay.economy.duplicate_trade", "invalid or duplicate trade transaction id"));
    TradeTransaction tx;
    tx.id = p.id;
    tx.plan = std::move(p);
    tx.state = TradeTransactionState::Prepared;
    Bump();
    tx.revision = revision_;
    const auto id = tx.id;
    const auto context = tx.plan.context;
    transactions_.emplace(id, std::move(tx));
    ++diagnostics_.transactions;
    Record({0, EconomyChangeKind::TradeChanged, {}, {}, id, 0, context, revision_});
    return foundation::Result<TradeTransactionId>::Success(id);
}

foundation::Result<void> EconomyService::ReserveTrade(TradeTransactionId id)
{
    auto it = transactions_.find(id);
    if (it == transactions_.end() || it->second.state != TradeTransactionState::Prepared)
        return foundation::Result<void>::Failure(Error("gameplay.economy.trade_state", "trade not prepared"));
    std::unordered_map<EconomicAccountId, Fixed, IdHash> outgoing;
    std::vector<FundsReservationId> made;
    made.reserve(it->second.plan.monetary_transfers.size());
    for (const auto &t : it->second.plan.monetary_transfers)
    {
        auto v = ValidateTransfer(t);
        if (!v)
            return foundation::Result<void>::Failure(v.GetError());
        Fixed next = 0;
        if (!CheckedAdd(outgoing[t.from], t.amount, next))
            return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "trade amount overflow"));
        outgoing[t.from] = next;
        auto rid = FundsReservationId{reservation_ids_.Next()};
        if (!rid.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.economy.id_exhausted", "reservation id exhausted"));
        made.push_back(rid);
    }
    for (const auto &[account, amount] : outgoing)
        if (GetAvailableBalance(account) < amount)
            return foundation::Result<void>::Failure(Error("gameplay.economy.insufficient_funds", "trade funds unavailable"));
    Bump();
    std::vector<FundsReservationId> committed;
    committed.reserve(made.size());
    for (std::size_t i = 0; i < made.size(); ++i)
    {
        const auto &t = it->second.plan.monetary_transfers[i];
        FundsReservation r;
        r.id = made[i];
        r.account = t.from;
        r.amount = t.amount;
        r.reason = TypeId::FromString("economy.trade");
        r.revision = revision_;
        reservations_.emplace(r.id, r);
        committed.push_back(r.id);
        ++diagnostics_.active_reservations;
        Record({0, EconomyChangeKind::FundsReserved, accounts_.at(t.from).owner, t.from, id, t.amount, it->second.plan.context, revision_});
    }
    it->second.reservations = std::move(committed);
    it->second.state = TradeTransactionState::Reserved;
    it->second.revision = revision_;
    Record({0, EconomyChangeKind::TradeChanged, {}, {}, id, 0, it->second.plan.context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> EconomyService::CommitTrade(TradeTransactionId id)
{
    auto it = transactions_.find(id);
    if (it == transactions_.end()) return foundation::Result<void>::Failure(Error("gameplay.economy.trade_missing", "trade missing"));
    if (it->second.state != TradeTransactionState::Reserved ||
        it->second.reservations.size() != it->second.plan.monetary_transfers.size())
        return foundation::Result<void>::Failure(Error("gameplay.economy.trade_state", "trade not reserved"));

    std::unordered_map<EconomicAccountId, Fixed, IdHash> outgoing, incoming, final_balances;
    for (std::size_t i = 0; i < it->second.reservations.size(); ++i)
    {
        const auto rid = it->second.reservations[i];
        const auto r = reservations_.find(rid);
        const auto& t = it->second.plan.monetary_transfers[i];
        const auto from = accounts_.find(t.from), to = accounts_.find(t.to);
        if (r == reservations_.end() || r->second.state != FundsReservationState::Active ||
            r->second.account != t.from || r->second.amount != t.amount || from == accounts_.end() || to == accounts_.end() ||
            from->second.state != AccountState::Active || to->second.state == AccountState::Closed || t.from == t.to ||
            from->second.currency != t.currency || to->second.currency != t.currency)
            return foundation::Result<void>::Failure(Error("gameplay.economy.trade_invalidated", "reserved trade was invalidated"));
        Fixed value = 0;
        if (!CheckedAdd(outgoing[t.from], t.amount, value))
            return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "trade outgoing amount overflow"));
        outgoing[t.from] = value;
        if (!CheckedAdd(incoming[t.to], t.amount, value))
            return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "trade incoming amount overflow"));
        incoming[t.to] = value;
    }
    for (const auto& [account_id, account] : accounts_)
    {
        const Fixed out = outgoing.contains(account_id) ? outgoing.at(account_id) : 0;
        const Fixed in = incoming.contains(account_id) ? incoming.at(account_id) : 0;
        if (out > account.balance)
            return foundation::Result<void>::Failure(Error("gameplay.economy.trade_invalidated", "reserved trade funds unavailable"));
        Fixed after_out = account.balance - out, final_value = 0;
        if (!CheckedAdd(after_out, in, final_value))
            return foundation::Result<void>::Failure(Error("gameplay.economy.balance_overflow", "trade final balance overflow"));
        final_balances[account_id] = final_value;
    }

    const auto transaction = it->second;
    Bump();
    for (const auto& [account_id, value] : final_balances)
        if (outgoing.contains(account_id) || incoming.contains(account_id))
        {
            auto& account = accounts_.at(account_id);
            account.balance = value;
            account.revision = revision_;
        }
    for (std::size_t i = 0; i < transaction.reservations.size(); ++i)
    {
        const auto rid = transaction.reservations[i];
        const auto transfer = transaction.plan.monetary_transfers[i];
        reservations_.erase(rid);
        if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
        EconomyChange change{0, EconomyChangeKind::FundsCommitted, accounts_.at(transfer.to).owner, transfer.to, id, transfer.amount,
                             transaction.plan.context, revision_};
        change.reservation = rid;
        change.currency = transfer.currency;
        change.source_account = transfer.from;
        change.destination_account = transfer.to;
        Record(std::move(change));
    }
    Record({0, EconomyChangeKind::TradeChanged, {}, {}, id, 0, transaction.plan.context, revision_});
    transactions_.erase(id);
    if (diagnostics_.transactions > 0) --diagnostics_.transactions;
    return foundation::Result<void>::Success();
}

foundation::Result<void> EconomyService::CancelTrade(TradeTransactionId id)
{
    auto it = transactions_.find(id);
    if (it == transactions_.end()) return foundation::Result<void>::Failure(Error("gameplay.economy.trade_missing", "trade missing"));
    if (it->second.state != TradeTransactionState::Prepared && it->second.state != TradeTransactionState::Reserved)
        return foundation::Result<void>::Failure(Error("gameplay.economy.trade_state", "trade cannot be cancelled from current state"));
    const auto tx = it->second;
    for (const auto rid : tx.reservations)
    {
        const auto r = reservations_.find(rid);
        if (r == reservations_.end() || r->second.state != FundsReservationState::Active)
            return foundation::Result<void>::Failure(Error("gameplay.economy.trade_invalidated", "trade reservation missing"));
    }
    Bump();
    for (const auto rid : tx.reservations)
    {
        const auto copy = reservations_.at(rid);
        reservations_.erase(rid);
        if (diagnostics_.active_reservations > 0) --diagnostics_.active_reservations;
        EconomyChange change{0, EconomyChangeKind::FundsReleased, {}, copy.account, id, copy.amount, tx.plan.context, revision_};
        change.reservation = rid;
        change.source_account = copy.account;
        Record(std::move(change));
    }
    Record({0, EconomyChangeKind::TradeChanged, {}, {}, id, 0, tx.plan.context, revision_});
    transactions_.erase(it);
    if (diagnostics_.transactions > 0) --diagnostics_.transactions;
    return foundation::Result<void>::Success();
}

const TradeTransaction *EconomyService::FindTransaction(TradeTransactionId id) const noexcept
{
    auto it = transactions_.find(id);
    return it == transactions_.end() ? nullptr : &it->second;
}
std::optional<TradeTransaction> EconomyService::FindTransactionCopy(TradeTransactionId id) const noexcept
{
    const auto* tx = FindTransaction(id);
    return tx ? std::optional<TradeTransaction>{*tx} : std::nullopt;
}
std::vector<EconomicAccount> EconomyService::FindAccounts(GameplayObjectRef owner, std::optional<CurrencyId> currency) const
{
    std::vector<EconomicAccount> out;
    for (const auto& [id, account] : accounts_)
    {
        (void)id;
        if (SameObject(account.owner, owner) && (!currency.has_value() || account.currency == *currency)) out.push_back(account);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b){ return a.id < b.id; });
    return out;
}
foundation::Result<DebtId> EconomyService::CreateDebt(DebtRecord d)
{
    if (!d.debtor.IsValid() || !d.creditor.IsValid() || SameObject(d.debtor, d.creditor) || !currencies_.contains(d.currency) ||
        !ValidateMoneyAmount(d.currency, d.principal, false))
        return foundation::Result<DebtId>::Failure(Error("gameplay.economy.invalid_debt", "invalid debt"));
    if (!d.id.IsValid())
        d.id = DebtId{debt_ids_.Next()};
    if (!d.id.IsValid() || debts_.contains(d.id))
        return foundation::Result<DebtId>::Failure(Error("gameplay.economy.duplicate_debt", "duplicate debt"));
    Bump();
    d.revision = revision_;
    const auto id = d.id;
    debts_[id] = d;
    ++diagnostics_.debts;
    Record({0, EconomyChangeKind::DebtChanged, d.debtor, {}, {}, d.principal, {}, revision_});
    return foundation::Result<DebtId>::Success(id);
}
foundation::Result<void> EconomyService::ResolveDebt(DebtId id, DebtState state, GameplayContext c)
{
    auto it = debts_.find(id);
    if (it == debts_.end()) return foundation::Result<void>::Failure(Error("gameplay.economy.debt_missing", "debt missing"));
    if (it->second.state == state) return foundation::Result<void>::Success();
    if (it->second.state != DebtState::Active || state == DebtState::Active)
        return foundation::Result<void>::Failure(Error("gameplay.economy.debt_state", "invalid debt state transition"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    EconomyChange change{0, EconomyChangeKind::DebtChanged, it->second.debtor, {}, {}, it->second.principal, c, revision_};
    change.debt = id;
    change.currency = it->second.currency;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

std::vector<DebtRecord> EconomyService::FindDebts(GameplayObjectRef s) const
{
    std::vector<DebtRecord> out;
    for (const auto &[id, d] : debts_)
    {
        (void)id;
        if (d.debtor == s || d.creditor == s)
            out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
foundation::Result<EconomicContractId> EconomyService::CreateContract(EconomicContract c)
{
    std::unordered_set<GameplayObjectRef> unique_parties;
    const bool parties_valid = std::all_of(c.parties.begin(), c.parties.end(), [&](const auto& party){ return party.IsValid() && unique_parties.insert(party).second; });
    if (c.parties.size() < 2 || !parties_valid || !c.type.IsValid() || !currencies_.contains(c.currency) ||
        !ValidateMoneyAmount(c.currency, c.amount, true))
        return foundation::Result<EconomicContractId>::Failure(
            Error("gameplay.economy.invalid_contract", "invalid economic contract"));
    if (!c.id.IsValid())
        c.id = EconomicContractId{contract_ids_.Next()};
    if (!c.id.IsValid() || contracts_.contains(c.id))
        return foundation::Result<EconomicContractId>::Failure(
            Error("gameplay.economy.duplicate_contract", "duplicate economic contract"));
    Bump();
    c.revision = revision_;
    const auto id = c.id;
    contracts_[id] = std::move(c);
    Record({0, EconomyChangeKind::ContractChanged, {}, {}, {}, 0, {}, revision_});
    return foundation::Result<EconomicContractId>::Success(id);
}
foundation::Result<void> EconomyService::SetContractState(EconomicContractId id, ContractState state, GameplayContext c)
{
    auto it = contracts_.find(id);
    if (it == contracts_.end()) return foundation::Result<void>::Failure(Error("gameplay.economy.contract_missing", "contract missing"));
    const auto old = it->second.state;
    if (old == state) return foundation::Result<void>::Success();
    const bool from_draft = old == ContractState::Draft && (state == ContractState::Active || state == ContractState::Cancelled);
    const bool from_active = old == ContractState::Active && (state == ContractState::Fulfilled || state == ContractState::Breached ||
                                                               state == ContractState::Cancelled || state == ContractState::Expired);
    if (!from_draft && !from_active)
        return foundation::Result<void>::Failure(Error("gameplay.economy.contract_state", "invalid contract state transition"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    EconomyChange change{0, EconomyChangeKind::ContractChanged, {}, {}, {}, it->second.amount, c, revision_};
    change.contract = id;
    change.currency = it->second.currency;
    Record(std::move(change));
    return foundation::Result<void>::Success();
}

std::vector<EconomyChange> EconomyService::ChangesSince(std::uint64_t seq) const
{
    std::vector<EconomyChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](auto &c) { return c.sequence > seq; });
    return out;
}
EconomySnapshot EconomyService::CaptureSnapshot() const
{
    EconomySnapshot s;
    for (const auto &[id, v] : accounts_)
    {
        (void)id;
        s.accounts.push_back(v);
    }
    for (const auto &[id, v] : reservations_)
    {
        (void)id;
        if (!IsTerminalReservation(v.state))
            s.reservations.push_back(v);
    }
    for (const auto &[id, v] : markets_)
    {
        (void)id;
        s.markets.push_back(v);
    }
    for (const auto &[id, v] : indicators_)
    {
        (void)id;
        s.indicators.push_back(v);
    }
    for (const auto &[id, v] : offers_)
    {
        (void)id;
        if (!IsTerminalOffer(v.state))
            s.offers.push_back(v);
    }
    for (const auto &[id, v] : transactions_)
    {
        (void)id;
        if (IsLiveTrade(v.state))
            s.transactions.push_back(v);
    }
    for (const auto &[id, v] : debts_)
    {
        (void)id;
        s.debts.push_back(v);
    }
    for (const auto &[id, v] : contracts_)
    {
        (void)id;
        s.contracts.push_back(v);
    }
    auto sorter = [](auto &a, auto &b) { return a.id < b.id; };
    std::sort(s.accounts.begin(), s.accounts.end(), sorter);
    std::sort(s.reservations.begin(), s.reservations.end(), sorter);
    std::sort(s.markets.begin(), s.markets.end(), sorter);
    std::sort(s.offers.begin(), s.offers.end(), sorter);
    std::sort(s.transactions.begin(), s.transactions.end(), sorter);
    std::sort(s.debts.begin(), s.debts.end(), sorter);
    std::sort(s.contracts.begin(), s.contracts.end(), sorter);
    std::sort(s.indicators.begin(), s.indicators.end(),
              [](auto &a, auto &b) { return a.market == b.market ? a.commodity < b.commodity : a.market < b.market; });
    s.account_ids = account_ids_.GetSnapshot();
    s.reservation_ids = reservation_ids_.GetSnapshot();
    s.market_ids = market_ids_.GetSnapshot();
    s.offer_ids = offer_ids_.GetSnapshot();
    s.transaction_ids = transaction_ids_.GetSnapshot();
    s.debt_ids = debt_ids_.GetSnapshot();
    s.contract_ids = contract_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> EconomyService::RestoreSnapshot(EconomySnapshot s)
{
    std::unordered_map<EconomicAccountId, EconomicAccount, IdHash> a;
    for (auto &v : s.accounts)
    {
        if (!v.id.IsValid() || !v.owner.IsValid() || !currencies_.contains(v.currency) || v.balance < 0 ||
            !ValidateMoneyAmount(v.currency, v.balance, true) || v.revision.value > s.revision.value || a.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid account snapshot"));
        a.emplace(v.id, std::move(v));
    }
    std::unordered_map<FundsReservationId, FundsReservation, IdHash> r;
    for (auto &v : s.reservations)
    {
        if (!v.id.IsValid() || !a.contains(v.account) || r.contains(v.id) || v.state != FundsReservationState::Active ||
            v.amount <= 0 || !ValidateMoneyAmount(a.at(v.account).currency, v.amount, false) || v.revision.value > s.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid reservation snapshot"));
        r.emplace(v.id, std::move(v));
    }
    std::unordered_map<MarketId, MarketState, IdHash> markets;
    for (auto &v : s.markets)
    {
        if (!v.id.IsValid() || markets.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid market snapshot"));
        markets.emplace(v.id, std::move(v));
    }
    std::unordered_map<IndicatorKey, MarketIndicator, IndicatorKeyHash> indicators;
    for (auto &v : s.indicators)
    {
        IndicatorKey key{v.market, v.commodity};
        if (!markets.contains(v.market) || !v.commodity.IsValid() || v.supply < 0 || v.demand < 0 || v.price_index_micro < 0 ||
            v.revision.value > s.revision.value || indicators.contains(key))
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid market indicator snapshot"));
        indicators.emplace(key, std::move(v));
    }
    std::unordered_map<OfferId, EconomicOffer, IdHash> offers;
    for (auto &v : s.offers)
    {
        if (!v.id.IsValid() || !v.seller.IsValid() || !v.type.IsValid() || !v.subject.type.IsValid() ||
            !currencies_.contains(v.currency) || v.quantity <= 0 || !ValidateMoneyAmount(v.currency, v.unit_price, true) ||
            offers.contains(v.id) || IsTerminalOffer(v.state) || v.revision.value > s.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid offer snapshot"));
        offers.emplace(v.id, std::move(v));
    }
    std::unordered_map<TradeTransactionId, TradeTransaction, IdHash> transactions;
    for (auto &v : s.transactions)
    {
        if (!v.id.IsValid() || transactions.contains(v.id) || !IsLiveTrade(v.state) ||
            !v.plan.buyer.IsValid() || !v.plan.seller.IsValid() || SameObject(v.plan.buyer, v.plan.seller) ||
            v.revision.value > s.revision.value)
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid trade snapshot"));
        for (const auto& transfer : v.plan.monetary_transfers)
        {
            const auto from = a.find(transfer.from), to = a.find(transfer.to);
            if (from == a.end() || to == a.end() || transfer.from == transfer.to || transfer.amount <= 0 ||
                from->second.currency != transfer.currency || to->second.currency != transfer.currency ||
                !ValidateMoneyAmount(transfer.currency, transfer.amount, false))
                return foundation::Result<void>::Failure(
                    Error("gameplay.economy.restore_invalid", "trade transfer snapshot is invalid"));
        }
        if (v.state == TradeTransactionState::Prepared && !v.reservations.empty())
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "prepared trade cannot own reservations"));
        if (v.state == TradeTransactionState::Reserved)
        {
            if (v.reservations.size() != v.plan.monetary_transfers.size())
                return foundation::Result<void>::Failure(
                    Error("gameplay.economy.restore_invalid", "reserved trade reservation count mismatch"));
            for (std::size_t i = 0; i < v.reservations.size(); ++i)
            {
                const auto rr = r.find(v.reservations[i]);
                const auto& transfer = v.plan.monetary_transfers[i];
                if (rr == r.end() || rr->second.account != transfer.from || rr->second.amount != transfer.amount)
                    return foundation::Result<void>::Failure(
                        Error("gameplay.economy.restore_invalid", "trade reservation does not match transfer"));
            }
        }
        transactions.emplace(v.id, std::move(v));
    }
    std::unordered_map<DebtId, DebtRecord, IdHash> debts;
    for (auto &v : s.debts)
    {
        if (!v.id.IsValid() || !v.debtor.IsValid() || !v.creditor.IsValid() || SameObject(v.debtor, v.creditor) ||
            !currencies_.contains(v.currency) || !ValidateMoneyAmount(v.currency, v.principal, false) ||
            v.revision.value > s.revision.value || debts.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid debt snapshot"));
        debts.emplace(v.id, std::move(v));
    }
    std::unordered_map<EconomicContractId, EconomicContract, IdHash> contracts;
    for (auto &v : s.contracts)
    {
        if (!v.id.IsValid() || !v.type.IsValid() || !currencies_.contains(v.currency) ||
            !ValidateMoneyAmount(v.currency, v.amount, true) || v.revision.value > s.revision.value || contracts.contains(v.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.economy.restore_invalid", "invalid contract snapshot"));
        contracts.emplace(v.id, std::move(v));
    }

    std::unordered_map<EconomicAccountId, Fixed, IdHash> reserved_totals;
    for (const auto& [rid, reservation] : r)
    {
        (void)rid;
        Fixed total = 0;
        if (!CheckedAdd(reserved_totals[reservation.account], reservation.amount, total) || total > a.at(reservation.account).balance)
            return foundation::Result<void>::Failure(Error("gameplay.economy.restore_invalid", "reservations exceed account balance"));
        reserved_totals[reservation.account] = total;
    }
    accounts_ = std::move(a);
    reservations_ = std::move(r);
    markets_ = std::move(markets);
    indicators_ = std::move(indicators);
    offers_ = std::move(offers);
    transactions_ = std::move(transactions);
    debts_ = std::move(debts);
    contracts_ = std::move(contracts);
    account_ids_.Restore(s.account_ids);
    reservation_ids_.Restore(s.reservation_ids);
    market_ids_.Restore(s.market_ids);
    offer_ids_.Restore(s.offer_ids);
    transaction_ids_.Restore(s.transaction_ids);
    debt_ids_.Restore(s.debt_ids);
    contract_ids_.Restore(s.contract_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    diagnostics_.accounts = accounts_.size();
    diagnostics_.markets = markets_.size();
    diagnostics_.offers = offers_.size();
    diagnostics_.transactions = transactions_.size();
    diagnostics_.debts = debts_.size();
    for (const auto &[id, v] : reservations_)
    {
        (void)id;
        if (v.state == FundsReservationState::Active)
            ++diagnostics_.active_reservations;
    }
    return foundation::Result<void>::Success();
}
EconomyDiagnostics EconomyService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
void EconomyService::Record(EconomyChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(std::move(c));
}
} // namespace epidemic::gameplay::economy
