# Economy Goal 4 B03 local audit

Status: block-local `LOCAL_READY` projection for Goal 4. This is not a whole-Framework `FROZEN` claim.

## Responsibility and ownership

Owns currencies, accounts, fund reservations, offers, trades, debts, contracts, economy revisions/generators and the economy journal.

The service remains the single authoritative owner of the state listed below. Derived indexes and journals are not independent semantic owners.

## Authoritative state and indexes

Admission inventory: 105 public callables, 108 mutation obligations, 1 lifecycle candidates, 27 stale-identity candidates and 0 external-boundary candidates.

Primary records, secondary indexes, ID generators, revision/change sequence and bounded journal state are reviewed together. Mutations that span several containers must complete all fallible staging before the no-fail authoritative commit point. Query ordering and index-derived views are required to agree with primary state.

## Public contracts and failure atomicity

Every admission callable below is classified and reviewed. Mutators have success, no-op, invalid/precondition and failure decisions in the B03 coverage projection. Revision, ID-generator and journal publication is part of the mutation contract. Allocation/publication failure must not expose a partially advanced generator, revision, primary record, derived index or journal entry.

B03 allocation evidence uses source-private module seams rather than the shared process-global allocator override. These seams are test-only implementation details and do not expand the public API.

## Lifecycle and identity

Lifecycle candidates and stale identity candidates from the admission inventory were reviewed. Invalid, removed, duplicate and stale IDs are required to be rejected or treated as the documented no-op without publishing false state. Terminal records are not silently resurrected by retry.

## Persistence

Snapshot/restore is reviewed as a candidate-state operation: validate first, build off-state, preserve all live state on failure, then publish the candidate. Successful restore preserves the persistent ID-generator/revision/journal boundary represented by the snapshot contract.

## External boundaries

Price/provider failures are explicit service inputs. Admission emitted no separate external-boundary candidate rows.

External callback/provider failure is converted to the module error/result contract. Retry must not duplicate an already committed semantic side effect, and failed compensation that requires reconciliation stays represented by owned state.

## Threading contract

No additional internal synchronization guarantee is introduced by B03. These owners are treated as externally serialized/owner-thread services for local correctness evidence. Later Goal 6 performs the engine-wide concurrency qualification.

## Regression evidence

Registered module target: `EpidemicGameFrameworkEconomyTests` from `EngineFramework/DevelopmentInfrastructure/Tests/economy_tests.cpp`.


## Goal 4 functional checklist

- [x] Currency/account registration.
- [x] Funds reservation lifecycle.
- [x] Monetary transfer.
- [x] Offer lifecycle.
- [x] Trade transaction lifecycle.
- [x] Debt and contract lifecycle.
- [x] Price provider failure.
- [x] No negative/overflow balance unless explicitly allowed.
- [x] Reservation prevents double spending.
- [x] Transfer failure leaves both accounts unchanged.
- [x] Snapshot/restore accounts, reservations, offers, trades, debts, contracts, generators, journal.

## Exact public API anchors

Each row is the block-local contract anchor for one admission callable. The matching test anchor is stored in `_goal4_handoff/B03/public_api_anchors.json`.

- `05c2a45d7960f33d` | `QUERY` | `epidemic::gameplay::economy::OfferTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `074dcf473e213202` | `QUERY` | `epidemic::gameplay::economy::EconomicValueTypeId` | `[[nodiscard]] constexpr bool operator==(const EconomicValueTypeId&)const noexcept=default;`
- `0910d694e264cd7d` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<EconomicContractId> CreateContract(EconomicContract contract);`
- `0b51567e8b7e72d6` | `FACTORY` | `epidemic::gameplay::economy::OfferId` | `static constexpr OfferId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `0b7b332a1fead277` | `QUERY` | `epidemic::gameplay::economy::PriceProviderId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `0ccefd349f5befe7` | `CONSTRUCTOR` | `epidemic::gameplay::economy::EconomyService` | `EconomyService();`
- `0e68dc885384040a` | `QUERY` | `epidemic::gameplay::economy::EconomicAccountId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `10db49e0e8f3aba8` | `QUERY` | `epidemic::gameplay::economy::TradeTransactionId` | `[[nodiscard]] constexpr auto operator<=>(const TradeTransactionId&)const noexcept=default;`
- `11ea732d14d62253` | `QUERY` | `epidemic::gameplay::economy::CurrencyId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `1a1bd92bfe88341b` | `FACTORY` | `epidemic::gameplay::economy::CurrencyId` | `static constexpr CurrencyId FromString(std::string_view s)noexcept`
- `1ab66bfde205d271` | `QUERY` | `epidemic::gameplay::economy::TradeTransactionId` | `[[nodiscard]] constexpr bool operator==(const TradeTransactionId&)const noexcept=default;`
- `1cf5b62c7590da4d` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::optional<EconomicAccount> FindAccountCopy(EconomicAccountId id)const noexcept;`
- `1de32a7ef014a4cd` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CommitTrade(TradeTransactionId transaction);`
- `1e418d1f93efbf7e` | `QUERY` | `epidemic::gameplay::economy::EconomicCommodityId` | `[[nodiscard]] constexpr auto operator<=>(const EconomicCommodityId&)const noexcept=default;`
- `218de27a07fb17f5` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::optional<PriceQuote> GetPriceQuote(EconomicValueRef subject,CurrencyId currency,GameplayObjectRef buyer={},GameplayObjectRef seller={})const;`
- `231e19c6a5c74791` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::optional<MarketIndicator> GetMarketIndicator(MarketId market,EconomicCommodityId commodity)const;`
- `26bed1728576a283` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> SetMarketIndicator(MarketIndicator indicator);`
- `2a0ad6554b78219a` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] static constexpr GameplayDomainId Domain()noexcept`
- `2a8ca833d6940ab1` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<MarketId> CreateMarket(MarketState market);`
- `2e3a157873b29a15` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] Fixed GetAvailableBalance(EconomicAccountId id)const noexcept;`
- `2ed552da83bcfc9e` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> ReserveTrade(TradeTransactionId transaction);`
- `3010ab29ce55871c` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] const EconomicAccount*FindAccount(EconomicAccountId id)const noexcept;`
- `302a05ff0d12c657` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::vector<DebtRecord> FindDebts(GameplayObjectRef subject)const;`
- `32fa5f4b8cd915e0` | `QUERY` | `epidemic::gameplay::economy::PriceProviderId` | `[[nodiscard]] constexpr auto operator<=>(const PriceProviderId&)const noexcept=default;`
- `3446c62ac48e847e` | `QUERY` | `epidemic::gameplay::economy::DebtId` | `[[nodiscard]] constexpr bool operator==(const DebtId&)const noexcept=default;`
- `3582a9ce6662890b` | `QUERY` | `epidemic::gameplay::economy::EconomicContractId` | `[[nodiscard]] constexpr auto operator<=>(const EconomicContractId&)const noexcept=default;`
- `3c4a91a554dce133` | `QUERY` | `epidemic::gameplay::economy::TaxRuleId` | `[[nodiscard]] constexpr auto operator<=>(const TaxRuleId&)const noexcept=default;`
- `3cf24e4e2627c751` | `QUERY` | `epidemic::gameplay::economy::IPriceProvider` | `[[nodiscard]] virtual std::optional<PriceQuote> Quote(const PriceQuoteRequest&request)const=0;`
- `3d62ca5d6f599b7d` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::vector<EconomicAccount> FindAccounts(GameplayObjectRef owner,std::optional<CurrencyId> currency=std::nullopt)const;`
- `4296948dff11a19c` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<DebtId> CreateDebt(DebtRecord debt);`
- `4956521438472c32` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] const TradeTransaction*FindTransaction(TradeTransactionId id)const noexcept;`
- `49e8aad4d425707b` | `QUERY` | `epidemic::gameplay::economy::EconomicValueTypeId` | `[[nodiscard]] constexpr auto operator<=>(const EconomicValueTypeId&)const noexcept=default;`
- `4c2947e7f056c114` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> ResolveDebt(DebtId debt,DebtState state,GameplayContext context={});`
- `4ce352d89bd08be5` | `FACTORY` | `epidemic::gameplay::economy::EconomicContractId` | `static constexpr EconomicContractId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `4ff0ff8b64983b3d` | `QUERY` | `epidemic::gameplay::economy::EconomicCommodityId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `5ce1ab98bcce8490` | `QUERY` | `epidemic::gameplay::economy::FundsReservationId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `5f4bb8101d7ac115` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> SetAccountState(EconomicAccountId id,AccountState state,GameplayContext context={});`
- `5fe995b5850311f3` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::optional<PriceQuote> GetPriceQuote(const PriceQuoteRequest&request)const;`
- `620a0d3cf66a4632` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CompactDebt(DebtId debt,GameplayContext context={});`
- `62635d883f57db99` | `QUERY` | `epidemic::gameplay::economy::PriceProviderId` | `[[nodiscard]] constexpr bool operator==(const PriceProviderId&)const noexcept=default;`
- `62b8482b30677f58` | `FACTORY` | `epidemic::gameplay::economy::EconomicValueTypeId` | `static constexpr EconomicValueTypeId FromString(std::string_view s)noexcept`
- `641ca737315dd478` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> Debit(EconomicAccountId id,Fixed amount,GameplayContext context={});`
- `67e1cd7a3855ac1a` | `QUERY` | `epidemic::gameplay::economy::MarketId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `6c190b5019511710` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> AddPriceProvider(PriceProviderId id,std::int32_t priority,const IPriceProvider*provider);`
- `6d1da4abc52f82a2` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CompactContract(EconomicContractId contract,GameplayContext context={});`
- `6e29ea01f0db0fb2` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] Fixed GetBalance(EconomicAccountId id)const noexcept;`
- `7294b2719a366122` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] EconomyChangeBatch ReadChangesSince(ChangeCursor cursor)const`
- `7402d81c468617a4` | `QUERY` | `epidemic::gameplay::economy::OfferTypeId` | `[[nodiscard]] constexpr auto operator<=>(const OfferTypeId&)const noexcept=default;`
- `745bfdb2c332eaac` | `QUERY` | `epidemic::gameplay::economy::OfferId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `763d0bbf2dae9f1d` | `QUERY` | `epidemic::gameplay::economy::DebtId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `772b23297c4e5dc9` | `FACTORY` | `epidemic::gameplay::economy::FundsReservationId` | `static constexpr FundsReservationId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `787cb36c3546f2a7` | `QUERY` | `epidemic::gameplay::economy::ContractTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `78a0f9c34a5aacfd` | `QUERY` | `epidemic::gameplay::economy::FundsReservationId` | `[[nodiscard]] constexpr bool operator==(const FundsReservationId&)const noexcept=default;`
- `78d9b6f06aa32c71` | `QUERY` | `epidemic::gameplay::economy::OfferId` | `[[nodiscard]] constexpr bool operator==(const OfferId&)const noexcept=default;`
- `7a689d62ceadca09` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::optional<TradeTransaction> FindTransactionCopy(TradeTransactionId id)const noexcept;`
- `7beca23d9c89ae8d` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<TradeTransactionId> PrepareTrade(TradePlan plan);`
- `7d25c837486ff45e` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<TradeTransactionId> AcceptOffer(OfferAcceptanceRequest request);`
- `7d4ff03a76d271c7` | `QUERY` | `epidemic::gameplay::economy::TaxRuleId` | `[[nodiscard]] constexpr bool operator==(const TaxRuleId&)const noexcept=default;`
- `824d66ecca0d5fea` | `QUERY` | `epidemic::gameplay::economy::MarketId` | `[[nodiscard]] constexpr auto operator<=>(const MarketId&)const noexcept=default;`
- `833baf73f1552bc3` | `QUERY` | `epidemic::gameplay::economy::OfferTypeId` | `[[nodiscard]] constexpr bool operator==(const OfferTypeId&)const noexcept=default;`
- `8bfb9c68d6006e2e` | `FACTORY` | `epidemic::gameplay::economy::EconomicCommodityId` | `static constexpr EconomicCommodityId FromString(std::string_view s)noexcept`
- `8c0796e6825847b1` | `QUERY` | `epidemic::gameplay::economy::EconomicContractId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `8cc54ad69d64916c` | `QUERY` | `epidemic::gameplay::economy::EconomicAccountId` | `[[nodiscard]] constexpr auto operator<=>(const EconomicAccountId&)const noexcept=default;`
- `8e7f6267dc58db00` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<CurrencyId> RegisterCurrency(CurrencyDefinition definition);`
- `94191b42a5b4139e` | `QUERY` | `epidemic::gameplay::economy::DebtId` | `[[nodiscard]] constexpr auto operator<=>(const DebtId&)const noexcept=default;`
- `96e30b10f09d7c65` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> SetContractState(EconomicContractId contract,ContractState state,GameplayContext context={});`
- `97cc2aeaeeb9302f` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<FundsReservationId> ReserveFunds(EconomicAccountId account,Fixed amount,GameplayObjectRef beneficiary={},TypeId reason={},GameplayContext context={});`
- `9973dd7c528d87dc` | `LIFECYCLE` | `epidemic::gameplay::economy::EconomyService` | `void Freeze()noexcept;`
- `9f12f54b5aef51f3` | `FACTORY` | `epidemic::gameplay::economy::TradeTransactionId` | `static constexpr TradeTransactionId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `9f34656b9b03a2c1` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CommitFunds(FundsReservationId reservation,EconomicAccountId destination,GameplayContext context={});`
- `a2e1df98695fae1e` | `QUERY` | `epidemic::gameplay::economy::CurrencyId` | `[[nodiscard]] constexpr bool operator==(const CurrencyId&)const noexcept=default;`
- `a424eb9b7f26cb7b` | `FACTORY` | `epidemic::gameplay::economy::OfferTypeId` | `static constexpr OfferTypeId FromString(std::string_view s)noexcept`
- `ae3f5161f0f95b67` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CancelOffer(OfferId offer,GameplayContext context={});`
- `b0a1d92859cca223` | `QUERY` | `epidemic::gameplay::economy::OfferId` | `[[nodiscard]] constexpr auto operator<=>(const OfferId&)const noexcept=default;`
- `b1336133d919d828` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<OfferId> CreateOffer(EconomicOffer offer);`
- `b1abe76148859f9d` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] Revision CurrentRevision()const noexcept`
- `b2b45ffd95af4ad1` | `QUERY` | `epidemic::gameplay::economy::EconomicValueTypeId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `b2e7991b34203572` | `FACTORY` | `epidemic::gameplay::economy::DebtId` | `static constexpr DebtId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `bc13c1262591412c` | `QUERY` | `epidemic::gameplay::economy::EconomicContractId` | `[[nodiscard]] constexpr bool operator==(const EconomicContractId&)const noexcept=default;`
- `bc32b519621c6a31` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] EconomyDiagnostics GetDiagnostics()const noexcept;`
- `c9c5ca835a8510a5` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<EconomicAccountId> CreateAccount(EconomicAccount account);`
- `cd7686ff99d54fa9` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] std::vector<EconomicOffer> FindOffers(GameplayObjectRef seller={},OfferState state=OfferState::Active)const;`
- `cfbd70f93ac0cf66` | `QUERY` | `epidemic::gameplay::economy::EconomicAccountId` | `[[nodiscard]] constexpr bool operator==(const EconomicAccountId&)const noexcept=default;`
- `d04bcb9a58ccdce3` | `QUERY` | `epidemic::gameplay::economy::TradeTransactionId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `d1cb1f0a2ed9d755` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> ReleaseFunds(FundsReservationId reservation,GameplayContext context={});`
- `d5de811ad89b0f40` | `FACTORY` | `epidemic::gameplay::economy::PriceProviderId` | `static constexpr PriceProviderId FromString(std::string_view s)noexcept`
- `d637994d756be783` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] EconomySnapshot CaptureSnapshot()const;`
- `da33a42003c08b05` | `QUERY` | `epidemic::gameplay::economy::CurrencyId` | `[[nodiscard]] constexpr auto operator<=>(const CurrencyId&)const noexcept=default;`
- `ddc1bdb2346fcb55` | `QUERY` | `epidemic::gameplay::economy::MarketId` | `[[nodiscard]] constexpr bool operator==(const MarketId&)const noexcept=default;`
- `e0a1cbe0ca09b25e` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> Credit(EconomicAccountId id,Fixed amount,GameplayContext context={});`
- `e0f61ddfa3a2c1e7` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<std::vector<OfferId>> ExpireOffers(GameplayTimePoint now,GameplayContext context={});`
- `e34e6f4972d2b24d` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> RestoreSnapshot(EconomySnapshot snapshot);`
- `e4ce0a292a783123` | `QUERY` | `epidemic::gameplay::economy::EconomicCommodityId` | `[[nodiscard]] constexpr bool operator==(const EconomicCommodityId&)const noexcept=default;`
- `e52adf68c2f2725e` | `QUERY` | `epidemic::gameplay::economy::FundsReservationId` | `[[nodiscard]] constexpr auto operator<=>(const FundsReservationId&)const noexcept=default;`
- `e612bb1dbcd2d7e4` | `QUERY` | `epidemic::gameplay::economy::ContractTypeId` | `[[nodiscard]] constexpr auto operator<=>(const ContractTypeId&)const noexcept=default;`
- `eac4b7ccb252a9c6` | `FACTORY` | `epidemic::gameplay::economy::MarketId` | `static constexpr MarketId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `eb916df649363b62` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> RegisterContractTermsSchema(ContractTermsSchema schema);`
- `edbc76a7fdd5f5e1` | `FACTORY` | `epidemic::gameplay::economy::EconomicAccountId` | `static constexpr EconomicAccountId FromRaw(std::uint64_t h,std::uint64_t l)noexcept`
- `ee8db77c1174bd32` | `QUERY` | `epidemic::gameplay::economy::ContractTypeId` | `[[nodiscard]] constexpr bool operator==(const ContractTypeId&)const noexcept=default;`
- `f29ab5fff85988ba` | `DESTRUCTOR` | `epidemic::gameplay::economy::IPriceProvider` | `virtual ~IPriceProvider()=default;`
- `f2da25fe6b97422b` | `QUERY` | `epidemic::gameplay::economy::TaxRuleId` | `[[nodiscard]] constexpr bool IsValid()const noexcept`
- `f771000060459e82` | `FACTORY` | `epidemic::gameplay::economy::TaxRuleId` | `static constexpr TaxRuleId FromString(std::string_view s)noexcept`
- `f80415915ef5b9cc` | `MUTATOR` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] foundation::Result<void> CancelTrade(TradeTransactionId transaction);`
- `fe1ccf2b3e98ce4f` | `QUERY` | `epidemic::gameplay::economy::EconomyService` | `[[nodiscard]] ChangeCursor LatestChangeCursor()const noexcept`
- `ffa23a3216fca121` | `FACTORY` | `epidemic::gameplay::economy::ContractTypeId` | `static constexpr ContractTypeId FromString(std::string_view s)noexcept`

## Local-ready projection

All 37 Goal 4 local criteria have a block-local `PASS` decision with concrete contract/state/test evidence in `_goal4_handoff/B03/local_ready.json`. All 15 dossier fields are `REVIEWED`. Canonical `docs/freeze/**` regeneration remains the serial integrator step after all eight Bxx deltas are merged.
