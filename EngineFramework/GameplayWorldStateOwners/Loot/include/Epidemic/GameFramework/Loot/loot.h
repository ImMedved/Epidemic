#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include "Epidemic/GameFramework/SupportRandom/deterministic_random.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace epidemic::gameplay::loot
{
struct RewardTypeId
{
    TypeId value{};
    [[nodiscard]] static constexpr RewardTypeId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const RewardTypeId &) const noexcept = default;
};
struct RewardDefinitionId
{
    TypeId value{};
    [[nodiscard]] static constexpr RewardDefinitionId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const RewardDefinitionId &) const noexcept = default;
};
struct LootTableId
{
    TypeId value{};
    [[nodiscard]] static constexpr LootTableId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const LootTableId &) const noexcept = default;
};
struct LootEntryId
{
    TypeId value{};
    [[nodiscard]] static constexpr LootEntryId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const LootEntryId &) const noexcept = default;
};
struct LootQualityId
{
    TypeId value{};
    [[nodiscard]] static constexpr LootQualityId FromString(std::string_view n) noexcept
    {
        return {TypeId::FromString(n)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const LootQualityId &) const noexcept = default;
};
struct RewardExecutionId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr auto operator<=>(const RewardExecutionId &) const noexcept = default;
};
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        if constexpr (requires { id.value.Raw(); })
            return std::hash<TypeId>{}(id.value);
        else
            return std::hash<GameplayObjectId>{}(id.value);
    }
};

struct RegisteredRewardPayload
{
    TypeId type{};
    std::vector<std::byte> bytes;
    template <class T> [[nodiscard]] static RegisteredRewardPayload FromTrivial(TypeId t, const T &v)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredRewardPayload p;
        p.type = t;
        p.bytes.resize(sizeof(T));
        std::memcpy(p.bytes.data(), &v, sizeof(T));
        return p;
    }
    template <class T> [[nodiscard]] std::optional<T> AsTrivial(TypeId t) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (type != t || bytes.size() != sizeof(T))
            return std::nullopt;
        T v{};
        std::memcpy(&v, bytes.data(), sizeof(T));
        return v;
    }
};

struct RewardDefinition
{
    RewardDefinitionId id{};
    std::string canonical_name;
    RewardTypeId type{};
    std::int64_t min_quantity_micro = 1'000'000;
    std::int64_t max_quantity_micro = 1'000'000;
    LootQualityId quality{};
    RegisteredRewardPayload payload;
};

enum class LootRollPolicy
{
    WeightedOne,
    WeightedMany,
    IndependentChance,
    GuaranteedAll,
    PickNWithoutReplacement
};
struct LootEntry
{
    LootEntryId id{};
    std::uint64_t weight = 1;
    std::uint32_t chance_micro = 1'000'000;
    RewardDefinitionId reward{};
    LootTableId nested_table{};
    TypeId condition{};
};
struct LootTableDefinition
{
    LootTableId id{};
    std::string canonical_name;
    LootRollPolicy policy = LootRollPolicy::WeightedOne;
    std::uint32_t rolls = 1;
    std::vector<LootEntry> entries;
};

struct LootContext
{
    GameplayObjectRef source{};
    GameplayObjectRef recipient{};
    GameplayObjectRef instigator{};
    GameplayObjectRef area{};
    GameplayTagSet tags;
    GameplayContext gameplay{};
    random::RandomSeed seed{};
};

class ILootConditionProvider
{
  public:
    virtual ~ILootConditionProvider() = default;
    [[nodiscard]] virtual bool Evaluate(TypeId condition, const LootContext &context) const = 0;
};

struct RewardOperation
{
    RewardTypeId type{};
    RewardDefinitionId definition{};
    GameplayObjectRef recipient{};
    std::int64_t quantity_micro = 0;
    LootQualityId quality{};
    RegisteredRewardPayload payload;
    GameplayContext context{};
};
struct RewardBundle
{
    RewardExecutionId id{};
    std::vector<RewardOperation> rewards;
    random::RandomSeed seed{};
    GameplayContext context{};
};

enum class RewardDeliveryDisposition
{
    Delivered,
    NoOp,
    Unavailable,
    Rejected
};
struct RewardDeliveryStage
{
    RewardOperation operation;
    RewardDeliveryDisposition disposition = RewardDeliveryDisposition::Delivered;
};
class IRewardHandler
{
  public:
    virtual ~IRewardHandler() = default;
    [[nodiscard]] virtual RewardTypeId Type() const noexcept = 0;
    // Prepare is the only authoritative fallible validation stage. UI/preflight checks
    // must be treated as advisory outside LootService and must call Prepare again before commit.
    // A successful Delivered stage must reserve
    // everything required for Commit. Commit and Cancel are infallible and idempotent.
    [[nodiscard]] virtual foundation::Result<RewardDeliveryStage> Prepare(const RewardOperation &operation) = 0;
    virtual void Commit(RewardDeliveryStage &stage) noexcept = 0;
    virtual void Cancel(RewardDeliveryStage &stage) noexcept = 0;
};

enum class PendingRewardState
{
    Generated,
    Available,
    Claimed,
    Expired,
    Cancelled
};
struct PendingReward
{
    RewardBundle bundle;
    PendingRewardState state = PendingRewardState::Available;
    std::optional<GameplayTimePoint> expires_at{};
    std::optional<ScheduleId> schedule{};
    Revision revision{};
};

enum class LootChangeKind
{
    Generated,
    Discarded,
    Available,
    Claimed,
    Expired,
    Cancelled,
    DeliveryFailed
};
struct LootChange
{
    std::uint64_t sequence = 0;
    LootChangeKind kind = LootChangeKind::Generated;
    RewardExecutionId reward{};
    GameplayObjectRef recipient{};
    GameplayContext context{};
};
struct LootChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<LootChange> changes;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};
struct LootSnapshot
{
    std::vector<RewardBundle> generated;
    std::vector<PendingReward> pending;
    std::vector<RewardExecutionId> claimed;
    std::uint64_t claimed_history_floor_low = 0;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot execution_ids{};
    std::vector<LootChange> journal;
    std::uint64_t next_change_sequence = 1;

    std::uint64_t change_epoch = 1;
};
enum class RewardClaimHistoryStatus
{
    Unknown,
    Claimed,
    HistoryExpired
};

struct LootDiagnostics
{
    std::uint64_t rolls = 0, entries_evaluated = 0, bundles = 0, pending = 0, claims = 0, delivery_failures = 0,
                  max_nested_depth = 0;
};

class LootService
{
  public:
    LootService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.loot");
    }
    [[nodiscard]] foundation::Result<RewardTypeId> RegisterRewardHandler(std::string canonical_name,
                                                                         IRewardHandler *handler);
    [[nodiscard]] foundation::Result<RewardDefinitionId> RegisterRewardDefinition(RewardDefinition definition);
    [[nodiscard]] foundation::Result<LootTableId> RegisterLootTable(LootTableDefinition definition);
    [[nodiscard]] foundation::Result<void> Freeze();
    void SetConditionProvider(const ILootConditionProvider *provider) noexcept
    {
        conditions_ = provider;
    }

    // Pure deterministic resolution. The returned bundle has no tracked execution ID and
    // does not consume generated-bundle capacity. It cannot be passed to MakePending().
    [[nodiscard]] foundation::Result<RewardBundle> Preview(LootTableId table, LootContext context);
    [[nodiscard]] foundation::Result<RewardBundle> GenerateTracked(LootTableId table, LootContext context);
    // Compatibility alias for authoritative tracked generation.
    [[nodiscard]] foundation::Result<RewardBundle> Generate(LootTableId table, LootContext context)
    {
        return GenerateTracked(table, std::move(context));
    }
    [[nodiscard]] foundation::Result<void> DiscardGenerated(RewardExecutionId reward, GameplayContext context = {});
    [[nodiscard]] foundation::Result<RewardExecutionId> MakePending(RewardBundle bundle,
                                                                    std::optional<GameplayTimePoint> expires_at = {});
    // Idempotent transition for a tracked generated bundle. Useful for durable integration delivery.
    [[nodiscard]] foundation::Result<RewardExecutionId> MakePending(RewardExecutionId reward,
                                                                    std::optional<GameplayTimePoint> expires_at = {});
    [[nodiscard]] const RewardBundle* FindGenerated(RewardExecutionId reward) const noexcept;
    [[nodiscard]] foundation::Result<void> BindSchedule(RewardExecutionId reward, ScheduleId schedule);
    [[nodiscard]] foundation::Result<void> Claim(RewardExecutionId reward, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Expire(RewardExecutionId reward, GameplayTimePoint now,
                                                  GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Cancel(RewardExecutionId reward, GameplayContext context = {});
    [[nodiscard]] const PendingReward *FindPending(RewardExecutionId reward) const noexcept;
    [[nodiscard]] std::vector<PendingReward> PendingFor(GameplayObjectRef recipient) const;
    // Stable recovery enumeration for Time/SaveGame orchestration after restore.
    [[nodiscard]] std::vector<PendingReward> AllPending() const;
    [[nodiscard]] RewardClaimHistoryStatus ClaimHistoryStatus(RewardExecutionId reward) const noexcept;
    [[nodiscard]] bool WasClaimed(RewardExecutionId reward) const noexcept;

    private:
        [[nodiscard]] std::vector<LootChange> ChangesSinceSequence(std::uint64_t sequence) const;
        [[nodiscard]] LootChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] LootChangeBatch ReadChangesSince(ChangeCursor cursor) const
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
    [[nodiscard]] LootSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(LootSnapshot snapshot);
    [[nodiscard]] LootDiagnostics GetDiagnostics() const noexcept;

  private:
    [[nodiscard]] foundation::Result<void> GenerateTable(LootTableId table, const LootContext &context,
                                                         random::RandomSequence &sequence,
                                                         std::vector<RewardOperation> &out, std::uint32_t depth);
    [[nodiscard]] foundation::Result<void> EmitEntry(const LootEntry &entry, const LootContext &context,
                                                     random::RandomSequence &sequence,
                                                     std::vector<RewardOperation> &out, std::uint32_t depth);
    [[nodiscard]] foundation::Result<bool> EntryAllowed(const LootEntry &entry, const LootContext &context) const;
    [[nodiscard]] foundation::Result<void> ValidateNestedTables() const;
    void Record(LootChange change);

    std::unordered_map<RewardTypeId, IRewardHandler *, IdHash> handlers_;
    std::unordered_map<RewardDefinitionId, RewardDefinition, IdHash> rewards_;
    std::unordered_map<LootTableId, LootTableDefinition, IdHash> tables_;
    const ILootConditionProvider *conditions_ = nullptr;
    std::unordered_map<RewardExecutionId, RewardBundle, IdHash> generated_;
    std::unordered_map<RewardExecutionId, PendingReward, IdHash> pending_;
    std::unordered_set<RewardExecutionId, IdHash> claimed_;
    std::deque<RewardExecutionId> claimed_order_;
    std::uint64_t claimed_history_floor_low_ = 0;
    MonotonicIdGenerator<GameplayObjectId> execution_ids_;
    bool frozen_ = false;
    static constexpr std::size_t kGeneratedCapacity = 4096;
    static constexpr std::size_t kClaimedTombstoneCapacity = 8192;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    std::deque<LootChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    LootDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::loot
