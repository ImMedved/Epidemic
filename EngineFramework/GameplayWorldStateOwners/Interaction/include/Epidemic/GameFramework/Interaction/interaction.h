#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <limits>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::interaction
{
struct InteractionTypeId
{
    TypeId value{};
    static constexpr InteractionTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const InteractionTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const InteractionTypeId &) const noexcept = default;
};
struct InteractionProviderId
{
    TypeId value{};
    static constexpr InteractionProviderId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const InteractionProviderId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const InteractionProviderId &) const noexcept = default;
};
struct InteractionExecutionId
{
    GameplayObjectId value{};
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const InteractionExecutionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const InteractionExecutionId &) const noexcept = default;
};
using InteractionPriority = std::int32_t;
struct InteractionPoint
{
    std::int64_t x_mm = 0, y_mm = 0, z_mm = 0;
    [[nodiscard]] constexpr bool operator==(const InteractionPoint &) const noexcept = default;
};

enum class InteractionAvailability
{
    Available,
    Unavailable,
    Hidden,
    Deferred
};
enum class InteractionExecutionMode
{
    Instant,
    Timed,
    Continuous,
    Channelled
};
enum class InteractionMaterializationPolicy
{
    AbstractAllowed,
    RequiresMaterializedActor,
    RequiresMaterializedTarget,
    RequiresBothMaterialized
};
enum class InteractionSessionState
{
    Preparing,
    Active,
    Completed,
    Cancelled,
    Failed
};
enum class InteractionPersistence
{
    Transient,
    PersistentSession
};
struct InteractionDefinition
{
    InteractionTypeId type{};
    std::string canonical_name;
    GameplayTagSet tags;
    InteractionExecutionMode mode = InteractionExecutionMode::Instant;
    InteractionMaterializationPolicy materialization = InteractionMaterializationPolicy::RequiresBothMaterialized;
    InteractionPersistence persistence = InteractionPersistence::Transient;
    GameplayDuration duration{};
    ActionTypeId action{};
    std::vector<std::byte> payload;
};
struct InteractionContext
{
    GameplayObjectRef actor{};
    GameplayObjectRef target{};
    GameplayContext gameplay{};
    InteractionPoint point{};
    GameplayTagSet context_tags;
    Revision actor_revision{};
    Revision target_revision{};
};
struct InteractionCandidate
{
    InteractionTypeId type{};
    GameplayObjectRef actor{};
    GameplayObjectRef target{};
    InteractionProviderId provider{};
    InteractionPriority priority = 0;
    InteractionAvailability availability = InteractionAvailability::Available;
    TypeId reason{};
    std::vector<std::byte> payload;
};
struct InteractionSession
{
    InteractionExecutionId id{};
    InteractionTypeId type{};
    GameplayObjectRef actor{};
    GameplayObjectRef target{};
    InteractionSessionState state = InteractionSessionState::Preparing;
    GameplayTimePoint started_at{};
    std::optional<GameplayTimePoint> completes_at{};
    Revision actor_revision{};
    Revision target_revision{};
    std::vector<std::byte> payload;
    GameplayContext origin_context{};
    std::optional<ScheduleId> completion_schedule{};
    Revision revision{};
};
struct InteractionPlan
{
    InteractionCandidate candidate{};
    InteractionContext context{};
    Revision definition_revision{};
};
struct InteractionResult
{
    InteractionExecutionId execution{};
    InteractionSessionState state = InteractionSessionState::Failed;
    TypeId reason{};
};
struct InteractionSnapshot
{
    std::vector<InteractionSession> sessions;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot ids{};
    Revision revision{};

    std::uint64_t change_epoch = 1;
};
enum class InteractionChangeKind
{
    Started,
    Completed,
    Cancelled,
    Failed
};
struct InteractionChange
{
    std::uint64_t sequence = 0;
    InteractionChangeKind kind = InteractionChangeKind::Started;
    InteractionExecutionId execution{};
    InteractionTypeId type{};
    GameplayObjectRef actor{}, target{};
    GameplayContext context{};
    Revision revision{};
    TypeId reason{};
    std::optional<ScheduleId> completion_schedule{};
};
struct InteractionChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::vector<InteractionChange> changes;

    ChangeCursor oldest_available_cursor{};
    ChangeCursor latest_cursor{};
};

struct InteractionDiagnostics
{
    std::uint64_t candidate_requests = 0, candidates = 0, sessions = 0, completed = 0, cancelled = 0, failed = 0;
    std::uint64_t callback_failures = 0;
    std::uint64_t dropped_changes = 0;
};

class IInteractionProvider
{
  public:
    virtual ~IInteractionProvider() = default;
    [[nodiscard]] virtual InteractionProviderId Id() const noexcept = 0;
    [[nodiscard]] virtual std::vector<InteractionCandidate> Collect(const InteractionContext &context) const = 0;
};
class IInteractionExecutor
{
  public:
    virtual ~IInteractionExecutor() = default;
    [[nodiscard]] virtual foundation::Result<void> Validate(const InteractionPlan &plan) const = 0;
    // Commit is the irreversible stage. Implementations must prepare all fallible work in Validate/other preflight code.
    [[nodiscard]] virtual foundation::Result<void> Commit(const InteractionPlan &plan,
                                                          InteractionExecutionId execution) noexcept = 0;
};
class IInteractionStateProvider
{
  public:
    virtual ~IInteractionStateProvider() = default;
    [[nodiscard]] virtual bool IsMaterialized(GameplayObjectRef object) const = 0;
    [[nodiscard]] virtual Revision RevisionOf(GameplayObjectRef object) const = 0;
};

class InteractionService
{
  public:
    InteractionService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.interaction");
    }
    [[nodiscard]] foundation::Result<void> RegisterDefinition(InteractionDefinition definition);
    // Registered providers/executors are non-owning boot-time dependencies and must outlive this service.
    [[nodiscard]] foundation::Result<void> RegisterProvider(const IInteractionProvider &provider);
    [[nodiscard]] foundation::Result<void> RegisterExecutor(InteractionTypeId type, IInteractionExecutor &executor);
    void SetStateProvider(const IInteractionStateProvider *provider) noexcept
    {
        state_provider_ = provider;
    }
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    [[nodiscard]] bool IsFrozen() const noexcept
    {
        return frozen_;
    }
    [[nodiscard]] const InteractionDefinition *FindDefinition(InteractionTypeId type) const noexcept;
    [[nodiscard]] std::vector<InteractionCandidate> GetAvailableInteractions(const InteractionContext &context) const;
    [[nodiscard]] foundation::Result<InteractionPlan> Prepare(const InteractionContext &context,
                                                              InteractionCandidate candidate) const;
    [[nodiscard]] foundation::Result<InteractionResult> Commit(const InteractionPlan &plan);
    [[nodiscard]] foundation::Result<void> BindCompletionSchedule(InteractionExecutionId id, ScheduleId schedule);
    [[nodiscard]] foundation::Result<void> Complete(InteractionExecutionId id, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Cancel(InteractionExecutionId id, TypeId reason = {},
                                                  GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> SweepTimed(GameplayTimePoint now, GameplayContext context = {});
    [[nodiscard]] const InteractionSession *FindSession(InteractionExecutionId id) const noexcept;
    [[nodiscard]] std::optional<InteractionSession> FindSessionCopy(InteractionExecutionId id) const noexcept;
    [[nodiscard]] std::vector<InteractionSession> FindActive(GameplayObjectRef actor) const;
    // Persistent timed sessions restored from a snapshot intentionally have no scheduler handle.
    // Time integration must enumerate and bind these sessions before normal timed processing resumes.
    [[nodiscard]] std::vector<InteractionSession> SessionsRequiringSchedule() const;
    [[nodiscard]] InteractionSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(InteractionSnapshot snapshot);
    private:
        [[nodiscard]] std::vector<InteractionChange> ChangesSinceSequence(std::uint64_t sequence) const;
        [[nodiscard]] InteractionChangeBatch ReadChangesSinceSequence(std::uint64_t sequence) const;
    public:
    [[nodiscard]] InteractionChangeBatch ReadChangesSince(ChangeCursor cursor) const
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
    [[nodiscard]] Revision CurrentRevision() const noexcept { return revision_; }
    [[nodiscard]] InteractionDiagnostics GetDiagnostics() const noexcept;

  private:
    struct TypeHash
    {
        std::size_t operator()(InteractionTypeId id) const noexcept
        {
            return std::hash<TypeId>{}(id.value);
        }
    };
    struct ProviderHash
    {
        std::size_t operator()(InteractionProviderId id) const noexcept
        {
            return std::hash<TypeId>{}(id.value);
        }
    };
    struct ExecutionHash
    {
        std::size_t operator()(InteractionExecutionId id) const noexcept
        {
            return std::hash<GameplayObjectId>{}(id.value);
        }
    };
    void Record(InteractionChange c);
    void Bump() noexcept
    {
        ++revision_.value;
    }
    [[nodiscard]] bool MaterializationAllowed(const InteractionDefinition &d, const InteractionContext &c) const;
    std::unordered_map<InteractionTypeId, InteractionDefinition, TypeHash> definitions_;
    std::unordered_map<InteractionProviderId, const IInteractionProvider *, ProviderHash> providers_;
    std::unordered_map<InteractionTypeId, IInteractionExecutor *, TypeHash> executors_;
    std::unordered_map<InteractionExecutionId, InteractionSession, ExecutionHash> sessions_;
    std::unordered_map<ScheduleId, InteractionExecutionId> session_by_schedule_;
    const IInteractionStateProvider *state_provider_ = nullptr;
    MonotonicIdGenerator<GameplayObjectId> ids_;
    Revision revision_{};
    bool frozen_ = false;
    mutable std::uint64_t candidate_requests_ = 0, candidates_ = 0;
    std::uint64_t completed_ = 0, cancelled_ = 0, failed_ = 0;
    static constexpr std::size_t kChangeJournalCapacity = 4096;
    std::deque<InteractionChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    std::uint64_t journal_epoch_ = 1;
    mutable std::uint64_t callback_failures_ = 0;
    std::uint64_t dropped_changes_ = 0;
};
} // namespace epidemic::gameplay::interaction

