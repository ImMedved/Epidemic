#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::economy
{
using Fixed = std::int64_t;
#define ECO_TYPE(name)                                                                                                 \
    struct name                                                                                                        \
    {                                                                                                                  \
        TypeId value{};                                                                                                \
        static constexpr name FromString(std::string_view s) noexcept                                                  \
        {                                                                                                              \
            return {TypeId::FromString(s)};                                                                            \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
#define ECO_OBJ(name)                                                                                                  \
    struct name                                                                                                        \
    {                                                                                                                  \
        GameplayObjectId value{};                                                                                      \
        static constexpr name FromRaw(std::uint64_t h, std::uint64_t l) noexcept                                       \
        {                                                                                                              \
            return {GameplayObjectId::FromRaw(h, l)};                                                                  \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
ECO_TYPE(CurrencyId);
ECO_TYPE(EconomicCommodityId);
ECO_TYPE(EconomicValueTypeId);
ECO_TYPE(OfferTypeId);
ECO_TYPE(ContractTypeId);
ECO_TYPE(TaxRuleId);
ECO_TYPE(PriceProviderId);
ECO_OBJ(EconomicAccountId);
ECO_OBJ(FundsReservationId);
ECO_OBJ(MarketId);
ECO_OBJ(OfferId);
ECO_OBJ(TradeTransactionId);
ECO_OBJ(DebtId);
ECO_OBJ(EconomicContractId);
#undef ECO_TYPE
#undef ECO_OBJ
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class AccountState
{
    Active,
    Frozen,
    Closed
};
enum class FundsReservationState
{
    Active,
    Committed,
    Released
};
enum class OfferState
{
    Active,
    Accepted,
    Expired,
    Cancelled
};
enum class TradeTransactionState
{
    Prepared,
    Reserved,
    Committing,
    Committed,
    Compensating,
    Cancelled,
    Failed
};
enum class DebtState
{
    Active,
    Paid,
    Forgiven,
    Defaulted,
    Expired
};
enum class ContractState
{
    Draft,
    Active,
    Fulfilled,
    Breached,
    Cancelled,
    Expired
};
enum class EconomyChangeKind
{
    AccountCreated,
    AccountStateChanged,
    BalanceChanged,
    FundsReserved,
    FundsReleased,
    FundsCommitted,
    MarketCreated,
    MarketChanged,
    OfferCreated,
    OfferChanged,
    TradeChanged,
    DebtChanged,
    ContractChanged,
    DebtCompacted,
    ContractCompacted
};
struct CurrencyDefinition
{
    CurrencyId id{};
    std::string canonical_name;
    Fixed smallest_unit = 1;
    GameplayTagSet tags;
    Revision revision{};
};
struct EconomicAccount
{
    EconomicAccountId id{};
    GameplayObjectRef owner{};
    CurrencyId currency{};
    Fixed balance = 0;
    AccountState state = AccountState::Active;
    Revision revision{};
};
struct FundsReservation
{
    FundsReservationId id{};
    EconomicAccountId account{};
    Fixed amount = 0;
    GameplayObjectRef beneficiary{};
    TypeId reason{};
    FundsReservationState state = FundsReservationState::Active;
    Revision revision{};
};
struct MarketState
{
    MarketId id{};
    GameplayObjectRef area{};
    GameplayTagSet tags;
    Revision revision{};
};
struct MarketIndicator
{
    MarketId market{};
    EconomicCommodityId commodity{};
    Fixed supply = 0;
    Fixed demand = 0;
    Fixed price_index_micro = 1'000'000;
    GameplayTimePoint updated_at{};
    Revision revision{};
};
struct EconomicValueRef
{
    EconomicValueTypeId type{};
    GameplayObjectRef object{};
    TypeId definition{};
};
struct PriceQuote
{
    EconomicValueRef subject{};
    CurrencyId currency{};
    std::optional<MarketId> market{};
    GameplayObjectRef buyer{};
    GameplayObjectRef seller{};
    Fixed quantity = 1;
    Fixed unit_amount = 0;
    Revision dependencies_revision{};
};
struct PriceQuoteRequest
{
    EconomicValueRef subject{};
    CurrencyId currency{};
    std::optional<MarketId> market{};
    GameplayObjectRef buyer{};
    GameplayObjectRef seller{};
    Fixed quantity = 1;
    GameplayContext context{};
};
struct EconomicOffer
{
    OfferId id{};
    GameplayObjectRef seller{};
    GameplayObjectRef buyer_scope{};
    OfferTypeId type{};
    EconomicValueRef subject{};
    Fixed quantity = 0;
    CurrencyId currency{};
    Fixed unit_price = 0;
    GameplayTimePoint expires_at{};
    OfferState state = OfferState::Active;
    Revision revision{};
};
struct MonetaryTransfer
{
    EconomicAccountId from{};
    EconomicAccountId to{};
    Fixed amount = 0;
    CurrencyId currency{};
};
struct TradePlan
{
    TradeTransactionId id{};
    GameplayObjectRef buyer{};
    GameplayObjectRef seller{};
    std::vector<MonetaryTransfer> monetary_transfers;
    Revision dependencies_revision{};
    GameplayContext context{};
};
struct TradeTransaction
{
    TradeTransactionId id{};
    TradePlan plan;
    std::vector<FundsReservationId> reservations;
    OfferId source_offer{};
    Revision source_offer_revision{};
    Fixed accepted_quantity = 0;
    EconomicValueRef accepted_subject{};
    TradeTransactionState state = TradeTransactionState::Prepared;
    Revision revision{};
};
struct OfferAcceptanceRequest
{
    OfferId offer{};
    Revision expected_offer_revision{};
    GameplayObjectRef buyer{};
    Fixed accepted_quantity = 0;
    EconomicAccountId buyer_funding_account{};
    EconomicAccountId seller_destination_account{};
    GameplayContext context{};
};
struct DebtRecord
{
    DebtId id{};
    GameplayObjectRef debtor{};
    GameplayObjectRef creditor{};
    CurrencyId currency{};
    Fixed principal = 0;
    GameplayTimePoint due_at{};
    DebtState state = DebtState::Active;
    Revision revision{};
};
struct ContractTermsSchema
{
    ContractTypeId type{};
    TypeId schema{};
    std::size_t max_payload_bytes = 0;
};
struct EconomicContract
{
    EconomicContractId id{};
    std::vector<GameplayObjectRef> parties;
    ContractTypeId type{};
    CurrencyId currency{};
    Fixed amount = 0;
    GameplayTimePoint due_at{};
    TypeId terms_schema{};
    std::vector<std::byte> terms_payload;
    ContractState state = ContractState::Draft;
    Revision revision{};
};
struct EconomyChange
{
    std::uint64_t sequence = 0;
    EconomyChangeKind kind = EconomyChangeKind::BalanceChanged;
    GameplayObjectRef subject{};
    EconomicAccountId account{};
    TradeTransactionId transaction{};
    Fixed amount = 0;
    GameplayContext context{};
    Revision revision{};
    FundsReservationId reservation{};
    MarketId market{};
    OfferId offer{};
    DebtId debt{};
    EconomicContractId contract{};
    CurrencyId currency{};
    EconomicAccountId source_account{};
    EconomicAccountId destination_account{};
    EconomicCommodityId commodity{};
    AccountState old_account_state = AccountState::Active;
    AccountState new_account_state = AccountState::Active;
};
struct EconomySnapshot
{
    std::vector<EconomicAccount> accounts;
    std::vector<FundsReservation> reservations;
    std::vector<MarketState> markets;
    std::vector<MarketIndicator> indicators;
    std::vector<EconomicOffer> offers;
    std::vector<TradeTransaction> transactions;
    std::vector<DebtRecord> debts;
    std::vector<EconomicContract> contracts;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot account_ids{}, reservation_ids{}, market_ids{}, offer_ids{},
        transaction_ids{}, debt_ids{}, contract_ids{};
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
struct EconomyDiagnostics
{
    std::uint64_t accounts = 0;
    std::uint64_t active_reservations = 0;
    std::uint64_t markets = 0;
    std::uint64_t offers = 0;
    std::uint64_t transactions = 0;
    std::uint64_t debts = 0;
};
struct EconomyChangeBatch
{
    std::vector<EconomyChange> changes;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    bool snapshot_required = false;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};

class IPriceProvider
{
  public:
    virtual ~IPriceProvider() = default;
    [[nodiscard]] virtual std::optional<PriceQuote> Quote(const PriceQuoteRequest &request) const = 0;
};
class EconomyService
{
  public:
    EconomyService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.economy");
    }
    [[nodiscard]] foundation::Result<CurrencyId> RegisterCurrency(CurrencyDefinition definition);
    void Freeze() noexcept;
    [[nodiscard]] foundation::Result<void> AddPriceProvider(PriceProviderId id, std::int32_t priority, const IPriceProvider *provider);
    [[nodiscard]] foundation::Result<void> RegisterContractTermsSchema(ContractTermsSchema schema);
    [[nodiscard]] foundation::Result<EconomicAccountId> CreateAccount(EconomicAccount account);
    [[nodiscard]] const EconomicAccount *FindAccount(EconomicAccountId id) const noexcept;
    [[nodiscard]] std::optional<EconomicAccount> FindAccountCopy(EconomicAccountId id) const noexcept;
    [[nodiscard]] foundation::Result<void> SetAccountState(EconomicAccountId id, AccountState state,
                                                           GameplayContext context = {});
    [[nodiscard]] Fixed GetBalance(EconomicAccountId id) const noexcept;
    [[nodiscard]] Fixed GetAvailableBalance(EconomicAccountId id) const noexcept;
    [[nodiscard]] foundation::Result<void> Credit(EconomicAccountId id, Fixed amount, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Debit(EconomicAccountId id, Fixed amount, GameplayContext context = {});
    [[nodiscard]] foundation::Result<FundsReservationId> ReserveFunds(EconomicAccountId account, Fixed amount,
                                                                      GameplayObjectRef beneficiary = {},
                                                                      TypeId reason = {}, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> ReleaseFunds(FundsReservationId reservation, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CommitFunds(FundsReservationId reservation, EconomicAccountId destination,
                                                       GameplayContext context = {});
    [[nodiscard]] foundation::Result<MarketId> CreateMarket(MarketState market);
    [[nodiscard]] foundation::Result<void> SetMarketIndicator(MarketIndicator indicator);
    [[nodiscard]] std::optional<MarketIndicator> GetMarketIndicator(MarketId market,
                                                                    EconomicCommodityId commodity) const;
    [[nodiscard]] std::optional<PriceQuote> GetPriceQuote(EconomicValueRef subject, CurrencyId currency,
                                                          GameplayObjectRef buyer = {},
                                                          GameplayObjectRef seller = {}) const;
    [[nodiscard]] std::optional<PriceQuote> GetPriceQuote(const PriceQuoteRequest &request) const;
    [[nodiscard]] foundation::Result<OfferId> CreateOffer(EconomicOffer offer);
    [[nodiscard]] foundation::Result<void> CancelOffer(OfferId offer, GameplayContext context = {});
    [[nodiscard]] foundation::Result<TradeTransactionId> AcceptOffer(OfferAcceptanceRequest request);
    [[nodiscard]] std::vector<EconomicOffer> FindOffers(GameplayObjectRef seller = {},
                                                        OfferState state = OfferState::Active) const;
    [[nodiscard]] foundation::Result<std::vector<OfferId>> ExpireOffers(GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] foundation::Result<TradeTransactionId> PrepareTrade(TradePlan plan);
    [[nodiscard]] foundation::Result<void> ReserveTrade(TradeTransactionId transaction);
    [[nodiscard]] foundation::Result<void> CommitTrade(TradeTransactionId transaction);
    [[nodiscard]] foundation::Result<void> CancelTrade(TradeTransactionId transaction);
    [[nodiscard]] const TradeTransaction *FindTransaction(TradeTransactionId id) const noexcept;
    [[nodiscard]] std::optional<TradeTransaction> FindTransactionCopy(TradeTransactionId id) const noexcept;
    [[nodiscard]] std::vector<EconomicAccount> FindAccounts(GameplayObjectRef owner, std::optional<CurrencyId> currency = std::nullopt) const;
    [[nodiscard]] foundation::Result<DebtId> CreateDebt(DebtRecord debt);
    [[nodiscard]] foundation::Result<void> ResolveDebt(DebtId debt, DebtState state, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompactDebt(DebtId debt, GameplayContext context = {});
    [[nodiscard]] std::vector<DebtRecord> FindDebts(GameplayObjectRef subject) const;
    [[nodiscard]] foundation::Result<EconomicContractId> CreateContract(EconomicContract contract);
    [[nodiscard]] foundation::Result<void> SetContractState(EconomicContractId contract, ContractState state,
                                                            GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> CompactContract(EconomicContractId contract, GameplayContext context = {});
    private:
        [[nodiscard]] EconomyChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] EconomyChangeBatch ReadChangesSince(ChangeCursor cursor) const
    {
        auto batch = ReadChangesSinceSequence(cursor.sequence);
        batch.oldest_available_cursor = {journal_epoch_, batch.oldest_available_sequence};
        batch.latest_cursor = {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                                    : next_change_sequence_ - 1};
        if ((!cursor.IsValid() && cursor.sequence != 0) || (cursor.IsValid() && cursor.epoch != journal_epoch_))
        {
            batch.changes.clear();
            batch.snapshot_required = true;
        }
        return batch;
    }
    [[nodiscard]] ChangeCursor LatestChangeCursor() const noexcept
    {
        return {journal_epoch_, next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                          : next_change_sequence_ - 1};
    }
    private:
        [[nodiscard]] std::vector<EconomyChange> ChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] EconomySnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(EconomySnapshot snapshot);
    [[nodiscard]] EconomyDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept
    {
        return revision_;
    }

  private:
    struct IndicatorKey
    {
        MarketId market{};
        EconomicCommodityId commodity{};
        [[nodiscard]] bool operator==(const IndicatorKey &) const noexcept = default;
    };
    struct IndicatorKeyHash
    {
        [[nodiscard]] std::size_t operator()(const IndicatorKey &k) const noexcept
        {
            auto a = std::hash<GameplayObjectId>{}(k.market.value), b = std::hash<TypeId>{}(k.commodity.value);
            return a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6u) + (a >> 2u));
        }
    };
    [[nodiscard]] std::optional<Revision> NextRevision() const noexcept;
    void Record(EconomyChange c) noexcept;
    [[nodiscard]] foundation::Result<void> ValidateTransfer(const MonetaryTransfer &t) const;
    [[nodiscard]] foundation::Result<void> ValidateMoneyAmount(CurrencyId currency, Fixed amount,
                                                               bool allow_zero) const;
    [[nodiscard]] FundsReservation *MutableReservation(FundsReservationId id) noexcept;
    bool frozen_ = false;
    Revision revision_{};
    std::unordered_map<CurrencyId, CurrencyDefinition, IdHash> currencies_;
    std::unordered_map<EconomicAccountId, EconomicAccount, IdHash> accounts_;
    std::unordered_map<FundsReservationId, FundsReservation, IdHash> reservations_;
    std::unordered_map<MarketId, MarketState, IdHash> markets_;
    std::unordered_map<IndicatorKey, MarketIndicator, IndicatorKeyHash> indicators_;
    std::unordered_map<OfferId, EconomicOffer, IdHash> offers_;
    std::unordered_map<TradeTransactionId, TradeTransaction, IdHash> transactions_;
    std::unordered_map<DebtId, DebtRecord, IdHash> debts_;
    std::unordered_map<EconomicContractId, EconomicContract, IdHash> contracts_;
    std::unordered_map<ContractTypeId, ContractTermsSchema, IdHash> contract_terms_schemas_;
    struct PriceProviderRegistration
    {
        PriceProviderId id{};
        std::int32_t priority = 0;
        const IPriceProvider *provider = nullptr;
    };
    std::vector<PriceProviderRegistration> price_providers_;
    MonotonicIdGenerator<GameplayObjectId> account_ids_{0x3700}, reservation_ids_{0x3701}, market_ids_{0x3702},
        offer_ids_{0x3703}, transaction_ids_{0x3704}, debt_ids_{0x3705}, contract_ids_{0x3706};
    std::deque<EconomyChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    EconomyDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::economy
