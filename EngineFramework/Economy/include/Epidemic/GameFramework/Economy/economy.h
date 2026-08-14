#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::economy
{
using Fixed=std::int64_t;
#define ECO_TYPE(name) struct name{TypeId value{};static constexpr name FromString(std::string_view s)noexcept{return {TypeId::FromString(s)};}[[nodiscard]]constexpr bool IsValid()const noexcept{return value.IsValid();}[[nodiscard]]constexpr bool operator==(const name&)const noexcept=default;[[nodiscard]]constexpr auto operator<=>(const name&)const noexcept=default;}
#define ECO_OBJ(name) struct name{GameplayObjectId value{};static constexpr name FromRaw(std::uint64_t h,std::uint64_t l)noexcept{return {GameplayObjectId::FromRaw(h,l)};}[[nodiscard]]constexpr bool IsValid()const noexcept{return value.IsValid();}[[nodiscard]]constexpr bool operator==(const name&)const noexcept=default;[[nodiscard]]constexpr auto operator<=>(const name&)const noexcept=default;}
ECO_TYPE(CurrencyId);ECO_TYPE(EconomicCommodityId);ECO_TYPE(EconomicValueTypeId);ECO_TYPE(OfferTypeId);ECO_TYPE(ContractTypeId);ECO_TYPE(TaxRuleId);
ECO_OBJ(EconomicAccountId);ECO_OBJ(FundsReservationId);ECO_OBJ(MarketId);ECO_OBJ(OfferId);ECO_OBJ(TradeTransactionId);ECO_OBJ(DebtId);ECO_OBJ(EconomicContractId);
#undef ECO_TYPE
#undef ECO_OBJ
struct IdHash{template<class T>[[nodiscard]]std::size_t operator()(const T&id)const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};

enum class AccountState{Active,Frozen,Closed};enum class FundsReservationState{Active,Committed,Released};enum class OfferState{Active,Accepted,Expired,Cancelled};enum class TradeTransactionState{Prepared,Reserved,Committing,Committed,Compensating,Cancelled,Failed};enum class DebtState{Active,Paid,Forgiven,Defaulted,Expired};enum class ContractState{Draft,Active,Fulfilled,Breached,Cancelled,Expired};enum class EconomyChangeKind{AccountCreated,BalanceChanged,FundsReserved,FundsReleased,FundsCommitted,MarketChanged,OfferCreated,OfferChanged,TradeChanged,DebtChanged,ContractChanged};
struct CurrencyDefinition{CurrencyId id{};std::string canonical_name;Fixed smallest_unit=1;GameplayTagSet tags;Revision revision{};};
struct EconomicAccount{EconomicAccountId id{};GameplayObjectRef owner{};CurrencyId currency{};Fixed balance=0;AccountState state=AccountState::Active;Revision revision{};};
struct FundsReservation{FundsReservationId id{};EconomicAccountId account{};Fixed amount=0;GameplayObjectRef beneficiary{};TypeId reason{};FundsReservationState state=FundsReservationState::Active;Revision revision{};};
struct MarketState{MarketId id{};GameplayObjectRef area{};GameplayTagSet tags;Revision revision{};};
struct MarketIndicator{MarketId market{};EconomicCommodityId commodity{};Fixed supply=0;Fixed demand=0;Fixed price_index_micro=1'000'000;GameplayTimePoint updated_at{};Revision revision{};};
struct EconomicValueRef{EconomicValueTypeId type{};GameplayObjectRef object{};TypeId definition{};};
struct PriceQuote{EconomicValueRef subject{};CurrencyId currency{};Fixed amount=0;Revision dependencies_revision{};};
struct EconomicOffer{OfferId id{};GameplayObjectRef seller{};GameplayObjectRef buyer_scope{};OfferTypeId type{};EconomicValueRef subject{};Fixed quantity=0;CurrencyId currency{};Fixed unit_price=0;GameplayTimePoint expires_at{};OfferState state=OfferState::Active;Revision revision{};};
struct MonetaryTransfer{EconomicAccountId from{};EconomicAccountId to{};Fixed amount=0;CurrencyId currency{};};
struct TradePlan{TradeTransactionId id{};GameplayObjectRef buyer{};GameplayObjectRef seller{};std::vector<MonetaryTransfer> monetary_transfers;Revision dependencies_revision{};GameplayContext context{};};
struct TradeTransaction{TradeTransactionId id{};TradePlan plan;std::vector<FundsReservationId> reservations;TradeTransactionState state=TradeTransactionState::Prepared;Revision revision{};};
struct DebtRecord{DebtId id{};GameplayObjectRef debtor{};GameplayObjectRef creditor{};CurrencyId currency{};Fixed principal=0;GameplayTimePoint due_at{};DebtState state=DebtState::Active;Revision revision{};};
struct EconomicContract{EconomicContractId id{};std::vector<GameplayObjectRef> parties;ContractTypeId type{};CurrencyId currency{};Fixed amount=0;GameplayTimePoint due_at{};ContractState state=ContractState::Draft;Revision revision{};};
struct EconomyChange{std::uint64_t sequence=0;EconomyChangeKind kind=EconomyChangeKind::BalanceChanged;GameplayObjectRef subject{};EconomicAccountId account{};TradeTransactionId transaction{};Fixed amount=0;GameplayContext context{};Revision revision{};};
struct EconomySnapshot{std::vector<EconomicAccount> accounts;std::vector<FundsReservation> reservations;std::vector<MarketState> markets;std::vector<MarketIndicator> indicators;std::vector<EconomicOffer> offers;std::vector<TradeTransaction> transactions;std::vector<DebtRecord> debts;std::vector<EconomicContract> contracts;MonotonicIdGenerator<GameplayObjectId>::Snapshot account_ids{},reservation_ids{},market_ids{},offer_ids{},transaction_ids{},debt_ids{},contract_ids{};Revision revision{};};
struct EconomyDiagnostics{std::uint64_t accounts=0;std::uint64_t active_reservations=0;std::uint64_t markets=0;std::uint64_t offers=0;std::uint64_t transactions=0;std::uint64_t debts=0;};

class IPriceProvider{public:virtual~IPriceProvider()=default;[[nodiscard]]virtual std::optional<PriceQuote> Quote(EconomicValueRef subject,CurrencyId currency,GameplayObjectRef buyer,GameplayObjectRef seller)const=0;};
class EconomyService
{
public:
 EconomyService();[[nodiscard]]static constexpr GameplayDomainId Domain()noexcept{return GameplayDomainId::FromString("framework.economy");}
 [[nodiscard]]foundation::Result<CurrencyId> RegisterCurrency(CurrencyDefinition definition);void Freeze()noexcept{frozen_=true;}void AddPriceProvider(const IPriceProvider*provider){if(provider)price_providers_.push_back(provider);}
 [[nodiscard]]foundation::Result<EconomicAccountId> CreateAccount(EconomicAccount account);[[nodiscard]]const EconomicAccount* FindAccount(EconomicAccountId id)const noexcept;[[nodiscard]]Fixed GetBalance(EconomicAccountId id)const noexcept;[[nodiscard]]Fixed GetAvailableBalance(EconomicAccountId id)const noexcept;[[nodiscard]]foundation::Result<void> Credit(EconomicAccountId id,Fixed amount,GameplayContext context={});[[nodiscard]]foundation::Result<void> Debit(EconomicAccountId id,Fixed amount,GameplayContext context={});
 [[nodiscard]]foundation::Result<FundsReservationId> ReserveFunds(EconomicAccountId account,Fixed amount,GameplayObjectRef beneficiary={},TypeId reason={},GameplayContext context={});[[nodiscard]]foundation::Result<void> ReleaseFunds(FundsReservationId reservation,GameplayContext context={});[[nodiscard]]foundation::Result<void> CommitFunds(FundsReservationId reservation,EconomicAccountId destination,GameplayContext context={});
 [[nodiscard]]foundation::Result<MarketId> CreateMarket(MarketState market);[[nodiscard]]foundation::Result<void> SetMarketIndicator(MarketIndicator indicator);[[nodiscard]]std::optional<MarketIndicator> GetMarketIndicator(MarketId market,EconomicCommodityId commodity)const;
 [[nodiscard]]std::optional<PriceQuote> GetPriceQuote(EconomicValueRef subject,CurrencyId currency,GameplayObjectRef buyer={},GameplayObjectRef seller={})const;
 [[nodiscard]]foundation::Result<OfferId> CreateOffer(EconomicOffer offer);[[nodiscard]]std::vector<EconomicOffer> FindOffers(GameplayObjectRef seller={},OfferState state=OfferState::Active)const;[[nodiscard]]std::vector<OfferId> ExpireOffers(GameplayTimePoint now,GameplayContext context={});
 [[nodiscard]]foundation::Result<TradeTransactionId> PrepareTrade(TradePlan plan);[[nodiscard]]foundation::Result<void> ReserveTrade(TradeTransactionId transaction);[[nodiscard]]foundation::Result<void> CommitTrade(TradeTransactionId transaction);[[nodiscard]]foundation::Result<void> CancelTrade(TradeTransactionId transaction);[[nodiscard]]const TradeTransaction* FindTransaction(TradeTransactionId id)const noexcept;
 [[nodiscard]]foundation::Result<DebtId> CreateDebt(DebtRecord debt);[[nodiscard]]foundation::Result<void> ResolveDebt(DebtId debt,DebtState state,GameplayContext context={});[[nodiscard]]std::vector<DebtRecord> FindDebts(GameplayObjectRef subject)const;
 [[nodiscard]]foundation::Result<EconomicContractId> CreateContract(EconomicContract contract);[[nodiscard]]foundation::Result<void> SetContractState(EconomicContractId contract,ContractState state,GameplayContext context={});
 [[nodiscard]]std::vector<EconomyChange> ChangesSince(std::uint64_t sequence)const;[[nodiscard]]EconomySnapshot CaptureSnapshot()const;[[nodiscard]]foundation::Result<void> RestoreSnapshot(EconomySnapshot snapshot);[[nodiscard]]EconomyDiagnostics GetDiagnostics()const noexcept;[[nodiscard]]Revision CurrentRevision()const noexcept{return revision_;}
private:
 struct IndicatorKey{MarketId market{};EconomicCommodityId commodity{};[[nodiscard]]bool operator==(const IndicatorKey&)const noexcept=default;};struct IndicatorKeyHash{[[nodiscard]]std::size_t operator()(const IndicatorKey&k)const noexcept{auto a=std::hash<GameplayObjectId>{}(k.market.value),b=std::hash<TypeId>{}(k.commodity.value);return a^(b+0x9E3779B97F4A7C15ull+(a<<6u)+(a>>2u));}};
 void Bump()noexcept{++revision_.value;}void Record(EconomyChange c);[[nodiscard]]foundation::Result<void> ValidateTransfer(const MonetaryTransfer&t)const;[[nodiscard]]FundsReservation* MutableReservation(FundsReservationId id)noexcept;
 bool frozen_=false;Revision revision_{};std::unordered_map<CurrencyId,CurrencyDefinition,IdHash> currencies_;std::unordered_map<EconomicAccountId,EconomicAccount,IdHash> accounts_;std::unordered_map<FundsReservationId,FundsReservation,IdHash> reservations_;std::unordered_map<MarketId,MarketState,IdHash> markets_;std::unordered_map<IndicatorKey,MarketIndicator,IndicatorKeyHash> indicators_;std::unordered_map<OfferId,EconomicOffer,IdHash> offers_;std::unordered_map<TradeTransactionId,TradeTransaction,IdHash> transactions_;std::unordered_map<DebtId,DebtRecord,IdHash> debts_;std::unordered_map<EconomicContractId,EconomicContract,IdHash> contracts_;std::vector<const IPriceProvider*>price_providers_;MonotonicIdGenerator<GameplayObjectId>account_ids_{0x3700},reservation_ids_{0x3701},market_ids_{0x3702},offer_ids_{0x3703},transaction_ids_{0x3704},debt_ids_{0x3705},contract_ids_{0x3706};std::vector<EconomyChange>changes_;std::uint64_t next_change_sequence_=1;EconomyDiagnostics diagnostics_{};
};
}
