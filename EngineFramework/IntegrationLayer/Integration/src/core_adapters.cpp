#include "Epidemic/GameFramework/Integration/core_adapters.h"

#include <algorithm>
#include <bit>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>

namespace epidemic::gameplay::integration
{
namespace
{
[[nodiscard]] bool ContainsHandler(
    const std::vector<ScheduledTriggerHandlerId>& handlers,
    ScheduledTriggerHandlerId id) noexcept
{
    return std::find(handlers.begin(), handlers.end(), id) != handlers.end();
}

struct TriggerIdentityKey
{
    std::uint64_t schedule_high = 0;
    std::uint64_t schedule_low = 0;
    std::uint64_t clock = 0;
    std::uint64_t action = 0;
    std::int64_t scheduled_for = 0;

    [[nodiscard]] bool operator==(const TriggerIdentityKey&) const noexcept = default;
};

struct TriggerIdentityHash
{
    [[nodiscard]] std::size_t operator()(const TriggerIdentityKey& key) const noexcept
    {
        auto mix = [](std::uint64_t value) noexcept {
            value ^= value >> 30u;
            value *= 0xBF58476D1CE4E5B9ull;
            value ^= value >> 27u;
            value *= 0x94D049BB133111EBull;
            value ^= value >> 31u;
            return value;
        };
        std::uint64_t hash = mix(key.schedule_high) ^ mix(key.schedule_low + 0x9E3779B97F4A7C15ull);
        hash ^= mix(key.clock + 0xD1B54A32D192ED03ull);
        hash ^= mix(key.action + 0x94D049BB133111EBull);
        hash ^= mix(std::bit_cast<std::uint64_t>(key.scheduled_for));
        return static_cast<std::size_t>(hash);
    }
};

[[nodiscard]] TriggerIdentityKey TriggerIdentity(const time::ScheduledTrigger& trigger) noexcept
{
    return TriggerIdentityKey{trigger.schedule.High(),
                              trigger.schedule.Low(),
                              trigger.clock.Raw(),
                              trigger.action.Raw(),
                              trigger.scheduled_for.ticks};
}

[[nodiscard]] OperationId DeliveryOperationId(const time::ScheduledTrigger& trigger) noexcept
{
    auto mix = [](std::uint64_t value) noexcept {
        value ^= value >> 30u;
        value *= 0xBF58476D1CE4E5B9ull;
        value ^= value >> 27u;
        value *= 0x94D049BB133111EBull;
        value ^= value >> 31u;
        return value;
    };

    const auto scheduled = static_cast<std::uint64_t>(trigger.scheduled_for.ticks);
    auto high = mix(trigger.schedule.High() ^ trigger.action.Raw() ^ scheduled);
    auto low = mix(trigger.schedule.Low() ^ trigger.clock.Raw() ^ (scheduled + 0x9E3779B97F4A7C15ull));
    if (high == 0 && low == 0)
    {
        low = 1;
    }
    return OperationId::FromRaw(high, low);
}

constexpr std::uint32_t kTriggerDispatcherWireMagic = 0x53544449u; // "STDI"
constexpr std::uint32_t kTriggerDispatcherWireVersion = 1;

class TriggerSnapshotWriter
{
  public:
    void U8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void U32(std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
    void U64(std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            U8(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
    void I64(std::int64_t value) { U64(std::bit_cast<std::uint64_t>(value)); }
    template <typename Id> void Id128(Id value)
    {
        U64(value.High());
        U64(value.Low());
    }
    void Ref(GameplayObjectRef ref)
    {
        U64(ref.domain.Raw());
        Id128(ref.id);
    }
    void Context(const GameplayContext& context)
    {
        U64(context.tick.Raw());
        I64(context.time.ticks);
        Id128(context.operation);
        Id128(context.correlation);
        Ref(context.actor);
        Ref(context.instigator);
        Ref(context.source);
        Id128(context.parent_operation);
        Id128(context.cause_event);
    }
    [[nodiscard]] std::vector<std::byte> Take() && { return std::move(bytes_); }

  private:
    std::vector<std::byte> bytes_;
};

class TriggerSnapshotReader
{
  public:
    explicit TriggerSnapshotReader(std::span<const std::byte> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] bool U8(std::uint8_t& value) noexcept
    {
        if (offset_ >= bytes_.size()) return false;
        value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
        return true;
    }
    [[nodiscard]] bool U32(std::uint32_t& value) noexcept
    {
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!U8(byte)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }
    [[nodiscard]] bool U64(std::uint64_t& value) noexcept
    {
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!U8(byte)) return false;
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }
    [[nodiscard]] bool I64(std::int64_t& value) noexcept
    {
        std::uint64_t raw = 0;
        if (!U64(raw)) return false;
        value = std::bit_cast<std::int64_t>(raw);
        return true;
    }
    template <typename Id> [[nodiscard]] bool Id128(Id& value) noexcept
    {
        std::uint64_t high = 0;
        std::uint64_t low = 0;
        if (!U64(high) || !U64(low)) return false;
        value = Id::FromRaw(high, low);
        return true;
    }
    [[nodiscard]] bool Ref(GameplayObjectRef& ref) noexcept
    {
        std::uint64_t domain = 0;
        if (!U64(domain) || !Id128(ref.id)) return false;
        ref.domain = GameplayDomainId::FromRaw(domain);
        return true;
    }
    [[nodiscard]] bool Context(GameplayContext& context) noexcept
    {
        std::uint64_t tick = 0;
        if (!U64(tick) || !I64(context.time.ticks) || !Id128(context.operation) || !Id128(context.correlation) ||
            !Ref(context.actor) || !Ref(context.instigator) || !Ref(context.source) ||
            !Id128(context.parent_operation) || !Id128(context.cause_event))
            return false;
        context.tick = GameplayTickId{tick};
        return true;
    }
    [[nodiscard]] bool Finished() const noexcept { return offset_ == bytes_.size(); }

  private:
    std::span<const std::byte> bytes_;
    std::size_t offset_ = 0;
};

void NormalizeTriggerDispatcherSnapshot(ScheduledTriggerDispatcherSnapshot& snapshot)
{
    for (auto& delivery : snapshot.pending)
    {
        std::sort(delivery.completed_handlers.begin(), delivery.completed_handlers.end());
    }
}

[[nodiscard]] std::vector<std::byte> EncodeTriggerDispatcherSnapshot(
    const ScheduledTriggerDispatcherSnapshot& snapshot)
{
    TriggerSnapshotWriter writer;
    writer.U32(kTriggerDispatcherWireMagic);
    writer.U32(kTriggerDispatcherWireVersion);
    writer.U32(static_cast<std::uint32_t>(snapshot.observer_handlers.size()));
    for (const auto handler : snapshot.observer_handlers)
        writer.U64(handler.Raw());
    writer.U32(static_cast<std::uint32_t>(snapshot.pending.size()));
    for (const auto& delivery : snapshot.pending)
    {
        writer.Id128(delivery.trigger.schedule);
        writer.U64(delivery.trigger.clock.Raw());
        writer.Ref(delivery.trigger.owner);
        writer.U64(delivery.trigger.action.Raw());
        writer.I64(delivery.trigger.scheduled_for.ticks);
        writer.I64(delivery.trigger.observed_at.ticks);
        writer.U64(delivery.trigger.occurrence_count);
        writer.Context(delivery.context);
        writer.U8(delivery.action_declared ? 1u : 0u);
        writer.U8(static_cast<std::uint8_t>(delivery.action_mode));
        writer.U64(delivery.required_action_handler.Raw());
        writer.U32(static_cast<std::uint32_t>(delivery.completed_handlers.size()));
        for (const auto handler : delivery.completed_handlers)
            writer.U64(handler.Raw());
    }
    return std::move(writer).Take();
}

[[nodiscard]] foundation::Result<ScheduledTriggerDispatcherSnapshot> DecodeTriggerDispatcherSnapshot(
    std::span<const std::byte> bytes,
    std::size_t max_pending,
    std::size_t max_observers,
    std::size_t max_handlers_per_delivery)
{
    TriggerSnapshotReader reader(bytes);
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t observer_count = 0;
    if (!reader.U32(magic) || !reader.U32(version) || magic != kTriggerDispatcherWireMagic ||
        version != kTriggerDispatcherWireVersion || !reader.U32(observer_count) || observer_count > max_observers)
    {
        return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
            foundation::Error::Create("integration.trigger_save_decode", "invalid scheduled trigger dispatcher save header"));
    }

    ScheduledTriggerDispatcherSnapshot snapshot;
    snapshot.observer_handlers.reserve(observer_count);
    for (std::uint32_t index = 0; index < observer_count; ++index)
    {
        std::uint64_t raw = 0;
        if (!reader.U64(raw))
            return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
                foundation::Error::Create("integration.trigger_save_decode", "truncated scheduled trigger observer manifest"));
        snapshot.observer_handlers.push_back(ScheduledTriggerHandlerId::FromRaw(raw));
    }

    std::uint32_t pending_count = 0;
    if (!reader.U32(pending_count) || pending_count > max_pending)
        return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
            foundation::Error::Create("integration.trigger_save_decode", "scheduled trigger pending count exceeds configured capacity"));
    snapshot.pending.reserve(pending_count);
    for (std::uint32_t index = 0; index < pending_count; ++index)
    {
        ScheduledTriggerDeliveryRecord delivery;
        std::uint64_t clock = 0;
        std::uint64_t action = 0;
        if (!reader.Id128(delivery.trigger.schedule) || !reader.U64(clock) || !reader.Ref(delivery.trigger.owner) ||
            !reader.U64(action) || !reader.I64(delivery.trigger.scheduled_for.ticks) ||
            !reader.I64(delivery.trigger.observed_at.ticks) || !reader.U64(delivery.trigger.occurrence_count) ||
            !reader.Context(delivery.context))
        {
            return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
                foundation::Error::Create("integration.trigger_save_decode", "truncated scheduled trigger delivery record"));
        }
        delivery.trigger.clock = ClockId::FromRaw(clock);
        delivery.trigger.action = ActionTypeId::FromRaw(action);

        std::uint8_t action_declared = 0;
        std::uint8_t action_mode = 0;
        std::uint64_t action_handler = 0;
        if (!reader.U8(action_declared) || action_declared > 1u || !reader.U8(action_mode) ||
            !reader.U64(action_handler) ||
            (action_mode != static_cast<std::uint8_t>(ScheduledTriggerActionDeliveryMode::RequiresActionHandler) &&
             action_mode != static_cast<std::uint8_t>(ScheduledTriggerActionDeliveryMode::ObserverOnly)))
        {
            return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
                foundation::Error::Create("integration.trigger_save_decode", "invalid scheduled trigger action delivery contract"));
        }
        delivery.action_declared = action_declared != 0;
        delivery.action_mode = static_cast<ScheduledTriggerActionDeliveryMode>(action_mode);
        delivery.required_action_handler = ScheduledTriggerHandlerId::FromRaw(action_handler);

        std::uint32_t completed_count = 0;
        if (!reader.U32(completed_count) || completed_count > max_handlers_per_delivery)
            return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
                foundation::Error::Create("integration.trigger_save_decode", "invalid scheduled trigger completed-handler count"));
        delivery.completed_handlers.reserve(completed_count);
        for (std::uint32_t completed_index = 0; completed_index < completed_count; ++completed_index)
        {
            std::uint64_t handler = 0;
            if (!reader.U64(handler))
                return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
                    foundation::Error::Create("integration.trigger_save_decode", "truncated scheduled trigger completed-handler list"));
            delivery.completed_handlers.push_back(ScheduledTriggerHandlerId::FromRaw(handler));
        }
        snapshot.pending.push_back(std::move(delivery));
    }

    if (!reader.Finished())
        return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
            foundation::Error::Create("integration.trigger_save_decode", "scheduled trigger dispatcher save contains trailing bytes"));
    NormalizeTriggerDispatcherSnapshot(snapshot);
    return foundation::Result<ScheduledTriggerDispatcherSnapshot>::Success(std::move(snapshot));
}

class ScheduledTriggerDispatcherRestoreStage final : public savegame::IRestoreStage
{
  public:
    explicit ScheduledTriggerDispatcherRestoreStage(ScheduledTriggerDispatcherSnapshot value)
        : snapshot(std::move(value))
    {
    }
    ScheduledTriggerDispatcherSnapshot snapshot;
};
} // namespace

foundation::Result<void> FactsQueryAdapter::RegisterProviders()
{
    if (registered_)
    {
        return foundation::Result<void>::Success();
    }

    const queries::QueryProviderCapabilities capabilities{true, true, false, false};

    const auto facts_result = queries_.RegisterSnapshotProvider<FindFactsQuery, facts::FactsSnapshot>(
        "framework.query.facts.find",
        capabilities,
        [this](const FindFactsQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindFacts(query.type, query.subject, query.scope);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindFactsQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::FactRecord> values;
            for (const auto& fact : snapshot.facts)
            {
                if (fact.key.type == query.type && (!query.subject.IsValid() || fact.key.subject == query.subject) &&
                    (!query.scope.IsValid() || fact.key.scope == query.scope))
                {
                    values.push_back(fact);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindFactsQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
    if (!facts_result)
    {
        return facts_result;
    }

    const auto history_result = queries_.RegisterSnapshotProvider<FindHistoryQuery, facts::FactsSnapshot>(
        "framework.query.history.find",
        capabilities,
        [this](const FindHistoryQuery& query, const queries::QueryContext&) {
            auto values = facts_.FindHistory(query.type, query.subject);
            queries::QueryMetadata metadata;
            metadata.revision = facts_.FactRevision();
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        },
        [this](const queries::QueryContext&) {
            auto snapshot = facts_.CaptureSnapshot();
            if (!snapshot)
            {
                return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Failure(snapshot.GetError());
            }
            return foundation::Result<queries::QueryProviderSnapshot<facts::FactsSnapshot>>::Success(
                {snapshot.Value(), snapshot.Value().fact_revision});
        },
        [](const FindHistoryQuery& query, const queries::QueryContext&, const facts::FactsSnapshot& snapshot) {
            std::vector<facts::EventRecord> values;
            for (const auto& event : snapshot.history)
            {
                if (event.envelope.type == query.type && (!query.subject.IsValid() || event.envelope.subject == query.subject))
                {
                    values.push_back(event);
                }
            }
            std::sort(values.begin(), values.end(), [](const auto& left, const auto& right) {
                return left.envelope.sequence < right.envelope.sequence;
            });
            queries::QueryMetadata metadata;
            metadata.revision = snapshot.fact_revision;
            metadata.coverage = queries::QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            metadata.deterministic_order = true;
            return foundation::Result<queries::QueryResponse<FindHistoryQuery::ResultType>>::Success(
                {std::move(values), metadata});
        });
    if (!history_result)
    {
        return history_result;
    }

    registered_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> RuntimeTimeAdapter::Synchronize()
{
    const auto snapshot = runtime_clock_.GetSnapshot();
    return gameplay_time_.SynchronizeClock(clock_, GameplayTimePoint{snapshot.now.ticks}, Revision{snapshot.revision});
}

foundation::Result<void> ScheduledTriggerDispatcher::RegisterObserver(
    ScheduledTriggerHandlerId id,
    int priority,
    Handler handler)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_frozen", "scheduled trigger dispatcher registry is frozen"));
    }
    if (!id.IsValid() || !handler)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_invalid", "scheduled trigger observer id/callback must be valid"));
    }
    if (HandlerExists(id))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_duplicate", "scheduled trigger handler id is already registered"));
    }

    observers_.push_back(RegisteredHandler{id, priority, std::move(handler)});
    std::sort(observers_.begin(), observers_.end(), [](const RegisteredHandler& left, const RegisteredHandler& right) {
        if (left.priority != right.priority)
        {
            return left.priority > right.priority;
        }
        return left.id < right.id;
    });
    return foundation::Result<void>::Success();
}

foundation::Result<void> ScheduledTriggerDispatcher::DeclareActionDelivery(
    ActionTypeId action,
    ScheduledTriggerActionDeliveryMode mode)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_frozen", "scheduled trigger dispatcher registry is frozen"));
    }
    if (!action.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_action_manifest_invalid", "scheduled trigger action manifest requires a valid action"));
    }
    if (mode != ScheduledTriggerActionDeliveryMode::RequiresActionHandler &&
        mode != ScheduledTriggerActionDeliveryMode::ObserverOnly)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_action_manifest_invalid", "scheduled trigger action manifest mode is invalid"));
    }

    const auto existing = action_manifest_.find(action);
    if (existing != action_manifest_.end())
    {
        if (existing->second != mode)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_action_manifest_conflict", "scheduled trigger action has a conflicting delivery declaration"));
        }
        return foundation::Result<void>::Success();
    }
    if (mode == ScheduledTriggerActionDeliveryMode::ObserverOnly && action_handlers_.contains(action))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_action_manifest_conflict", "observer-only action already has a gameplay action handler"));
    }

    action_manifest_.emplace(action, mode);
    return foundation::Result<void>::Success();
}

foundation::Result<void> ScheduledTriggerDispatcher::DeclareRequiresActionHandler(ActionTypeId action)
{
    return DeclareActionDelivery(action, ScheduledTriggerActionDeliveryMode::RequiresActionHandler);
}

foundation::Result<void> ScheduledTriggerDispatcher::DeclareObserverOnlyAction(ActionTypeId action)
{
    return DeclareActionDelivery(action, ScheduledTriggerActionDeliveryMode::ObserverOnly);
}

foundation::Result<void> ScheduledTriggerDispatcher::RegisterActionHandler(
    ActionTypeId action,
    ScheduledTriggerHandlerId id,
    Handler handler)
{
    if (frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_frozen", "scheduled trigger dispatcher registry is frozen"));
    }
    if (!action.IsValid() || !id.IsValid() || !handler)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_invalid", "scheduled trigger action/id/callback must be valid"));
    }
    if (HandlerExists(id) || action_handlers_.contains(action))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_handler_duplicate", "scheduled trigger action or handler id is already registered"));
    }

    const auto declared = DeclareActionDelivery(action, ScheduledTriggerActionDeliveryMode::RequiresActionHandler);
    if (!declared)
    {
        return declared;
    }
    action_handlers_.emplace(action, RegisteredHandler{id, 0, std::move(handler)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ScheduledTriggerDispatcher::Freeze()
{
    if (frozen_)
    {
        return foundation::Result<void>::Success();
    }
    for (const auto& [action, mode] : action_manifest_)
    {
        const auto handler = action_handlers_.find(action);
        if (mode == ScheduledTriggerActionDeliveryMode::RequiresActionHandler && handler == action_handlers_.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_action_handler_missing", "required scheduled trigger action handler is not registered"));
        }
        if (mode == ScheduledTriggerActionDeliveryMode::ObserverOnly && handler != action_handlers_.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_action_manifest_conflict", "observer-only scheduled trigger action has an action handler"));
        }
    }
    for (const auto& [action, handler] : action_handlers_)
    {
        (void)handler;
        const auto manifest = action_manifest_.find(action);
        if (manifest == action_manifest_.end() || manifest->second != ScheduledTriggerActionDeliveryMode::RequiresActionHandler)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_action_manifest_missing", "scheduled trigger action handler has no compatible manifest"));
        }
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}

bool ScheduledTriggerDispatcher::HandlerExists(ScheduledTriggerHandlerId id) const noexcept
{
    if (std::any_of(observers_.begin(), observers_.end(), [id](const RegisteredHandler& value) { return value.id == id; }))
    {
        return true;
    }
    return std::any_of(action_handlers_.begin(), action_handlers_.end(), [id](const auto& value) {
        return value.second.id == id;
    });
}

const ScheduledTriggerDispatcher::RegisteredHandler* ScheduledTriggerDispatcher::FindHandler(
    ScheduledTriggerHandlerId id) const noexcept
{
    const auto observer = std::find_if(observers_.begin(), observers_.end(), [id](const RegisteredHandler& value) {
        return value.id == id;
    });
    if (observer != observers_.end())
    {
        return &*observer;
    }

    const auto action = std::find_if(action_handlers_.begin(), action_handlers_.end(), [id](const auto& value) {
        return value.second.id == id;
    });
    return action == action_handlers_.end() ? nullptr : &action->second;
}

std::vector<ScheduledTriggerHandlerId> ScheduledTriggerDispatcher::RequiredHandlersFor(
    const ScheduledTriggerDeliveryRecord& delivery) const
{
    std::vector<ScheduledTriggerHandlerId> result;
    result.reserve(observers_.size() + 1);
    for (const auto& observer : observers_)
    {
        result.push_back(observer.id);
    }
    if (delivery.action_declared &&
        delivery.action_mode == ScheduledTriggerActionDeliveryMode::RequiresActionHandler &&
        delivery.required_action_handler.IsValid() &&
        !ContainsHandler(result, delivery.required_action_handler))
    {
        result.push_back(delivery.required_action_handler);
    }
    return result;
}

foundation::Result<std::uint64_t> ScheduledTriggerDispatcher::CollectDue(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget budget)
{
    if (!frozen_)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_not_frozen", "scheduled trigger handlers must be frozen before collection"));
    }
    if (!clock.IsValid() || policy_.max_pending_deliveries == 0 || budget.max_triggers == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_collection_invalid", "scheduled trigger collection arguments/policy are invalid"));
    }
    if (pending_.size() >= policy_.max_pending_deliveries)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_capacity", "scheduled trigger pending delivery capacity is exhausted"));
    }

    const auto available = policy_.max_pending_deliveries - pending_.size();
    const auto max_collect = std::min<std::uint64_t>(budget.max_triggers, static_cast<std::uint64_t>(available));
    if (max_collect == 0)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_capacity", "scheduled trigger pending delivery capacity is exhausted"));
    }

    try
    {
        pending_.reserve(pending_.size() + static_cast<std::size_t>(max_collect));
    }
    catch (...)
    {
        return foundation::Result<std::uint64_t>::Failure(
            foundation::Error::Create("integration.trigger_pending_allocation", "failed to reserve pending trigger delivery capacity"));
    }

    budget.max_triggers = max_collect;
    auto collected = time_.CollectDue(clock, budget);
    if (!collected)
    {
        return foundation::Result<std::uint64_t>::Failure(collected.GetError());
    }

    auto triggers = std::move(collected).Value();
    for (auto& trigger : triggers)
    {
        auto trigger_context = context;
        trigger_context.time = trigger.observed_at;
        ScheduledTriggerDeliveryRecord delivery;
        delivery.trigger = std::move(trigger);
        delivery.context = std::move(trigger_context);
        const auto manifest = action_manifest_.find(delivery.trigger.action);
        if (manifest != action_manifest_.end())
        {
            delivery.action_declared = true;
            delivery.action_mode = manifest->second;
            if (manifest->second == ScheduledTriggerActionDeliveryMode::RequiresActionHandler)
            {
                const auto handler = action_handlers_.find(delivery.trigger.action);
                delivery.required_action_handler = handler == action_handlers_.end() ? ScheduledTriggerHandlerId{} : handler->second.id;
            }
        }
        pending_.push_back(std::move(delivery));
    }
    return foundation::Result<std::uint64_t>::Success(static_cast<std::uint64_t>(triggers.size()));
}

ScheduledTriggerPumpReport ScheduledTriggerDispatcher::DispatchPending(std::uint64_t max_attempts)
{
    ScheduledTriggerPumpReport report;
    if (max_attempts == 0)
    {
        max_attempts = policy_.max_delivery_attempts_per_pump;
    }
    if (max_attempts == 0 || pending_.empty())
    {
        report.pending = static_cast<std::uint64_t>(pending_.size());
        return report;
    }

    std::vector<ScheduledTriggerDeliveryRecord> remaining;
    remaining.reserve(pending_.size());
    std::uint64_t attempts = 0;

    for (std::size_t index = 0; index < pending_.size(); ++index)
    {
        auto delivery = std::move(pending_[index]);
        const auto required = RequiredHandlersFor(delivery);
        const bool action_known = delivery.action_declared;
        if (!action_known && required.empty())
        {
            ++report.unhandled;
            remaining.push_back(std::move(delivery));
            continue;
        }

        bool retry = false;
        bool discarded = false;
        for (const auto handler_id : required)
        {
            if (ContainsHandler(delivery.completed_handlers, handler_id))
            {
                continue;
            }
            if (attempts >= max_attempts)
            {
                retry = true;
                break;
            }

            const auto* handler = FindHandler(handler_id);
            if (handler == nullptr)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            ++attempts;
            foundation::Result<ScheduledTriggerDisposition> result =
                foundation::Result<ScheduledTriggerDisposition>::Failure(
                    foundation::Error::Create("integration.trigger_handler_failed", "scheduled trigger handler failed"));
            try
            {
                result = handler->callback(delivery.trigger, delivery.context);
            }
            catch (const std::exception&)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }
            catch (...)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            if (!result)
            {
                ++report.handler_failures;
                ++report.retry_requests;
                retry = true;
                break;
            }

            switch (result.Value())
            {
            case ScheduledTriggerDisposition::Ack:
                delivery.completed_handlers.push_back(handler_id);
                break;
            case ScheduledTriggerDisposition::DiscardTerminal:
                delivery.completed_handlers.push_back(handler_id);
                discarded = true;
                break;
            case ScheduledTriggerDisposition::Retry:
                ++report.retry_requests;
                retry = true;
                break;
            }

            if (retry)
            {
                break;
            }
        }

        const auto all_complete = std::all_of(required.begin(), required.end(), [&delivery](ScheduledTriggerHandlerId id) {
            return ContainsHandler(delivery.completed_handlers, id);
        });
        if (all_complete && action_known)
        {
            ++report.acknowledged;
            if (discarded)
            {
                ++report.discarded_terminal;
            }
            continue;
        }
        if (all_complete && !action_known)
        {
            ++report.unhandled;
        }

        remaining.push_back(std::move(delivery));
    }

    pending_.swap(remaining);
    report.pending = static_cast<std::uint64_t>(pending_.size());
    return report;
}

foundation::Result<ScheduledTriggerPumpReport> ScheduledTriggerDispatcher::Pump(
    ClockId clock,
    GameplayContext context,
    time::SchedulerBudget budget)
{
    const auto collected = CollectDue(clock, std::move(context), budget);
    if (!collected)
    {
        // A full pending queue is backpressure, not loss. Caller may retry DispatchPending before collecting again.
        return foundation::Result<ScheduledTriggerPumpReport>::Failure(collected.GetError());
    }

    auto report = DispatchPending();
    report.collected = collected.Value();
    return foundation::Result<ScheduledTriggerPumpReport>::Success(report);
}

ScheduledTriggerDispatcherSnapshot ScheduledTriggerDispatcher::CaptureSnapshot() const
{
    ScheduledTriggerDispatcherSnapshot snapshot;
    snapshot.observer_handlers.reserve(observers_.size());
    for (const auto& observer : observers_)
    {
        snapshot.observer_handlers.push_back(observer.id);
    }
    snapshot.pending = pending_;
    NormalizeTriggerDispatcherSnapshot(snapshot);
    return snapshot;
}

foundation::Result<void> ScheduledTriggerDispatcher::ValidateSnapshot(
    const ScheduledTriggerDispatcherSnapshot& snapshot) const
{
    if (!frozen_)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_dispatcher_not_frozen", "dispatcher must be frozen before restoring pending deliveries"));
    }
    if (snapshot.pending.size() > policy_.max_pending_deliveries)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_snapshot_capacity", "trigger dispatcher snapshot exceeds configured capacity"));
    }

    std::vector<ScheduledTriggerHandlerId> expected_observers;
    expected_observers.reserve(observers_.size());
    for (const auto& observer : observers_)
    {
        expected_observers.push_back(observer.id);
    }
    if (snapshot.observer_handlers != expected_observers)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_snapshot_manifest_mismatch", "pending trigger snapshot does not match the frozen observer manifest"));
    }

    std::unordered_set<TriggerIdentityKey, TriggerIdentityHash> trigger_identities;
    trigger_identities.reserve(snapshot.pending.size());
    for (const auto& delivery : snapshot.pending)
    {
        const auto& trigger = delivery.trigger;
        if (!trigger.schedule.IsValid() || !trigger.clock.IsValid() || !trigger.owner.IsValid() || !trigger.action.IsValid() ||
            trigger.occurrence_count == 0 || delivery.context.time != trigger.observed_at)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_snapshot_invalid", "pending scheduled trigger snapshot record is invalid"));
        }

        if (!trigger_identities.insert(TriggerIdentity(trigger)).second)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("integration.trigger_snapshot_duplicate", "pending scheduled trigger snapshot contains duplicate occurrence"));
        }

        const auto current_manifest = action_manifest_.find(trigger.action);
        if (!delivery.action_declared)
        {
            if (current_manifest != action_manifest_.end() || delivery.required_action_handler.IsValid())
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_manifest_mismatch", "pending trigger action delivery contract changed"));
            }
        }
        else
        {
            if (current_manifest == action_manifest_.end() || current_manifest->second != delivery.action_mode)
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_manifest_mismatch", "pending trigger action delivery contract changed"));
            }
            if (delivery.action_mode == ScheduledTriggerActionDeliveryMode::RequiresActionHandler)
            {
                const auto current_handler = action_handlers_.find(trigger.action);
                if (!delivery.required_action_handler.IsValid() || current_handler == action_handlers_.end() ||
                    current_handler->second.id != delivery.required_action_handler)
                {
                    return foundation::Result<void>::Failure(
                        foundation::Error::Create("integration.trigger_snapshot_manifest_mismatch", "pending trigger required action handler changed"));
                }
            }
            else if (delivery.required_action_handler.IsValid())
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_manifest_mismatch", "observer-only pending trigger stores an action handler"));
            }
        }

        const auto required = RequiredHandlersFor(delivery);
        std::unordered_set<ScheduledTriggerHandlerId> completed;
        completed.reserve(delivery.completed_handlers.size());
        for (const auto handler_id : delivery.completed_handlers)
        {
            if (!handler_id.IsValid() || !ContainsHandler(required, handler_id) || !completed.insert(handler_id).second)
            {
                return foundation::Result<void>::Failure(
                    foundation::Error::Create("integration.trigger_snapshot_handler_invalid", "pending trigger snapshot contains invalid completed handler"));
            }
        }
    }
    return foundation::Result<void>::Success();
}

void ScheduledTriggerDispatcher::CommitRestoredSnapshot(ScheduledTriggerDispatcherSnapshot&& snapshot) noexcept
{
    pending_.swap(snapshot.pending);
}

foundation::Result<void> ScheduledTriggerDispatcher::RestoreSnapshot(ScheduledTriggerDispatcherSnapshot snapshot)
{
    NormalizeTriggerDispatcherSnapshot(snapshot);
    const auto validation = ValidateSnapshot(snapshot);
    if (!validation)
    {
        return validation;
    }
    CommitRestoredSnapshot(std::move(snapshot));
    return foundation::Result<void>::Success();
}

savegame::SaveParticipantId ScheduledTriggerDispatcherSaveParticipant::Id() const noexcept
{
    return savegame::SaveParticipantId::FromString("integration.scheduled_trigger_dispatcher");
}

foundation::Result<savegame::SaveSection> ScheduledTriggerDispatcherSaveParticipant::CaptureSnapshot(
    const savegame::SaveContext&) const
{
    if (!dispatcher_.IsFrozen())
    {
        return foundation::Result<savegame::SaveSection>::Failure(
            foundation::Error::Create("integration.trigger_save_not_frozen", "scheduled trigger dispatcher must be frozen before save capture"));
    }

    auto snapshot = dispatcher_.CaptureSnapshot();
    if (snapshot.pending.size() > std::numeric_limits<std::uint32_t>::max() ||
        snapshot.observer_handlers.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return foundation::Result<savegame::SaveSection>::Failure(
            foundation::Error::Create("integration.trigger_save_too_large", "scheduled trigger dispatcher snapshot exceeds wire limits"));
    }
    for (const auto& delivery : snapshot.pending)
    {
        if (delivery.completed_handlers.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return foundation::Result<savegame::SaveSection>::Failure(
                foundation::Error::Create("integration.trigger_save_too_large", "scheduled trigger completed-handler list exceeds wire limits"));
        }
    }

    savegame::SaveSection section;
    section.participant = Id();
    section.schema_version = SchemaVersion();
    try
    {
        section.payload = EncodeTriggerDispatcherSnapshot(snapshot);
    }
    catch (...)
    {
        return foundation::Result<savegame::SaveSection>::Failure(
            foundation::Error::Create("integration.trigger_save_allocation", "failed to encode scheduled trigger dispatcher snapshot"));
    }
    section.payload_hash = savegame::SaveGameOrchestrator::HashBytes(section.payload);
    return foundation::Result<savegame::SaveSection>::Success(std::move(section));
}

foundation::Result<void> ScheduledTriggerDispatcherSaveParticipant::ValidateSnapshot(
    const savegame::SaveSection& section,
    const savegame::RestoreContext&) const
{
    if (section.participant != Id() || section.schema_version != SchemaVersion() ||
        section.payload_hash != savegame::SaveGameOrchestrator::HashBytes(section.payload))
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_save_section_invalid", "scheduled trigger dispatcher save section is invalid"));
    }

    foundation::Result<ScheduledTriggerDispatcherSnapshot> decoded =
        foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
            foundation::Error::Create("integration.trigger_save_decode", "scheduled trigger dispatcher snapshot decode failed"));
    try
    {
        decoded = DecodeTriggerDispatcherSnapshot(
            section.payload,
            dispatcher_.policy_.max_pending_deliveries,
            dispatcher_.observers_.size(),
            dispatcher_.observers_.size() + 1);
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.trigger_save_allocation", "failed to decode scheduled trigger dispatcher snapshot"));
    }
    if (!decoded)
    {
        return foundation::Result<void>::Failure(decoded.GetError());
    }
    return dispatcher_.ValidateSnapshot(decoded.Value());
}

foundation::Result<std::unique_ptr<savegame::IRestoreStage>> ScheduledTriggerDispatcherSaveParticipant::StageRestore(
    const savegame::SaveSection& section,
    const savegame::RestoreContext&)
{
    if (section.participant != Id() || section.schema_version != SchemaVersion() ||
        section.payload_hash != savegame::SaveGameOrchestrator::HashBytes(section.payload))
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(
            foundation::Error::Create("integration.trigger_save_section_invalid", "scheduled trigger dispatcher save section is invalid"));
    }

    foundation::Result<ScheduledTriggerDispatcherSnapshot> decoded =
        foundation::Result<ScheduledTriggerDispatcherSnapshot>::Failure(
            foundation::Error::Create("integration.trigger_save_decode", "scheduled trigger dispatcher snapshot decode failed"));
    try
    {
        decoded = DecodeTriggerDispatcherSnapshot(
            section.payload,
            dispatcher_.policy_.max_pending_deliveries,
            dispatcher_.observers_.size(),
            dispatcher_.observers_.size() + 1);
    }
    catch (...)
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(
            foundation::Error::Create("integration.trigger_save_allocation", "failed to stage scheduled trigger dispatcher snapshot"));
    }
    if (!decoded)
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(decoded.GetError());
    }
    const auto valid = dispatcher_.ValidateSnapshot(decoded.Value());
    if (!valid)
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(valid.GetError());
    }

    try
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Success(
            std::make_unique<ScheduledTriggerDispatcherRestoreStage>(std::move(decoded.Value())));
    }
    catch (...)
    {
        return foundation::Result<std::unique_ptr<savegame::IRestoreStage>>::Failure(
            foundation::Error::Create("integration.trigger_save_allocation", "failed to allocate scheduled trigger restore stage"));
    }
}

void ScheduledTriggerDispatcherSaveParticipant::CommitRestore(savegame::IRestoreStage& stage) noexcept
{
    auto* typed = dynamic_cast<ScheduledTriggerDispatcherRestoreStage*>(&stage);
    if (typed == nullptr)
    {
        return;
    }
    dispatcher_.CommitRestoredSnapshot(std::move(typed->snapshot));
}

foundation::Result<EventTypeId> TimeFactsAdapter::RegisterContracts()
{
    if (scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<EventTypeId>::Success(scheduled_due_event_type_);
    }

    const auto registered = facts_.RegisterEventType<time::ScheduledTrigger>(
        "framework.schedule.due",
        GameplayDomainId::FromString("framework.time"),
        facts::HistoryPolicy::None);
    if (registered)
    {
        scheduled_due_event_type_ = registered.Value();
    }
    return registered;
}

foundation::Result<void> TimeFactsAdapter::RegisterWithDispatcher(
    ScheduledTriggerDispatcher& dispatcher,
    int priority)
{
    if (dispatcher_registered_)
    {
        return foundation::Result<void>::Success();
    }
    if (!scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("integration.time_facts_unregistered", "time/facts contracts must be registered before dispatcher binding"));
    }

    const auto registered = dispatcher.RegisterObserver(
        ScheduledTriggerHandlerId::FromString("framework.integration.time_facts"),
        priority,
        [this](const time::ScheduledTrigger& trigger, const GameplayContext& context) {
            return PublishTrigger(trigger, context);
        });
    if (registered)
    {
        dispatcher_registered_ = true;
    }
    return registered;
}

foundation::Result<ScheduledTriggerDisposition> TimeFactsAdapter::PublishTrigger(
    const time::ScheduledTrigger& trigger,
    const GameplayContext& context)
{
    if (!scheduled_due_event_type_.IsValid())
    {
        return foundation::Result<ScheduledTriggerDisposition>::Failure(
            foundation::Error::Create("integration.time_facts_unregistered", "time/facts contracts must be registered before use"));
    }
    if (!trigger.schedule.IsValid() || !trigger.action.IsValid() || !trigger.owner.IsValid())
    {
        return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::DiscardTerminal);
    }

    auto event_context = context;
    event_context.time = trigger.observed_at;
    const auto delivery_operation = DeliveryOperationId(trigger);
    if (event_context.operation.IsValid() && event_context.operation != delivery_operation)
    {
        event_context.parent_operation = event_context.operation;
    }
    event_context.operation = delivery_operation;

    const auto published = facts_.Publish<time::ScheduledTrigger>(
        scheduled_due_event_type_,
        event_context,
        trigger.owner,
        trigger,
        ProducerId::FromString("framework.time"));
    if (!published)
    {
        return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Retry);
    }
    return foundation::Result<ScheduledTriggerDisposition>::Success(ScheduledTriggerDisposition::Ack);
}

foundation::Result<std::uint64_t> TimeFactsAdapter::ExpireTimedFacts(GameplayContext context)
{
    return facts_.ExpireDueFacts(context.time, context);
}
} // namespace epidemic::gameplay::integration
