#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <mutex>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace epidemic::gameplay::facts
{
enum class HistoryPolicy
{
    None,
    Recent,
    Persistent,
    PersistentCompactable,
};

enum class FactPersistence
{
    Transient,
    Session,
    Persistent,
    Timed,
};

struct EventEnvelope
{
    EventId id{};
    EventTypeId type{};
    GameplayTickId tick{};
    GameplayTimePoint time{};
    GameplayContext context{};
    GameplayObjectRef subject{};
    ProducerId producer{};
    std::uint64_t sequence = 0;
};

struct EventRecord
{
    EventEnvelope envelope{};
    std::any payload;
    std::type_index payload_type{typeid(void)};
};

struct FactKey
{
    FactTypeId type{};
    GameplayObjectRef subject{};
    GameplayObjectRef scope{};

    [[nodiscard]] constexpr bool operator==(const FactKey&) const noexcept = default;
};


struct FactKeyHash
{
    [[nodiscard]] std::size_t operator()(const FactKey& key) const noexcept
    {
        auto seed = std::hash<FactTypeId>{}(key.type);
        const auto subject = std::hash<GameplayObjectRef>{}(key.subject);
        const auto scope = std::hash<GameplayObjectRef>{}(key.scope);
        seed ^= subject + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
        seed ^= scope + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

struct FactRecord
{
    FactId id{};
    FactKey key{};
    GameplayDomainId owner{};
    GameplayTimePoint created_at{};
    GameplayTimePoint updated_at{};
    Revision revision{};
    OperationId source_operation{};
    EventId source_event{};
    FactPersistence persistence = FactPersistence::Session;
    std::optional<GameplayTimePoint> expires_at{};
    std::any value;
    std::type_index value_type{typeid(void)};
};

enum class FactChangeKind
{
    Created,
    Updated,
    Removed,
};

struct FactChange
{
    FactChangeKind kind = FactChangeKind::Created;
    FactKey key{};
    std::optional<FactRecord> before;
    std::optional<FactRecord> after;
};

struct EventDispatchLimits
{
    std::uint64_t max_events = 100000;
    std::uint32_t max_waves = 64;
};

struct FactsDiagnostics
{
    std::uint64_t published_events = 0;
    std::uint64_t dispatched_events = 0;
    std::uint64_t dispatch_waves = 0;
    std::uint64_t rejected_events = 0;
    std::uint64_t fact_count = 0;
    std::uint64_t persistent_fact_count = 0;
    std::uint64_t history_count = 0;
};

class IHistoryCompactor
{
  public:
    virtual ~IHistoryCompactor() = default;
    [[nodiscard]] virtual std::vector<EventRecord> Compact(std::span<const EventRecord> records) const = 0;
};

class GameplayEventBatch
{
  public:
    GameplayEventBatch() = default;
    GameplayEventBatch(ProducerId producer, std::uint64_t batch_order) : producer_(producer), batch_order_(batch_order) {}

    template <typename TPayload>
    void Publish(EventTypeId type, GameplayContext context, GameplayObjectRef subject, TPayload payload)
    {
        pending_.push_back(PendingEvent{type,
                                        std::move(context),
                                        subject,
                                        std::any(std::move(payload)),
                                        typeid(TPayload),
                                        producer_,
                                        next_local_sequence_++});
    }

    [[nodiscard]] ProducerId Producer() const noexcept { return producer_; }
    [[nodiscard]] std::uint64_t BatchOrder() const noexcept { return batch_order_; }
    [[nodiscard]] bool Empty() const noexcept { return pending_.empty(); }

  private:
    struct PendingEvent
    {
        EventTypeId type{};
        GameplayContext context{};
        GameplayObjectRef subject{};
        std::any payload;
        std::type_index payload_type{typeid(void)};
        ProducerId producer{};
        std::uint64_t local_sequence = 0;
    };

    ProducerId producer_{};
    std::uint64_t batch_order_ = 0;
    std::uint64_t next_local_sequence_ = 0;
    std::vector<PendingEvent> pending_;

    friend class GameplayFactsService;
};

class FactTransaction
{
  public:
    explicit FactTransaction(GameplayDomainId owner = {}) : owner_(owner) {}

    template <typename TValue>
    void Set(FactTypeId type,
             GameplayObjectRef subject,
             GameplayObjectRef scope,
             TValue value,
             FactPersistence persistence = FactPersistence::Session,
             std::optional<GameplayTimePoint> expires_at = std::nullopt)
    {
        mutations_.push_back(Mutation{MutationKind::Set,
                                      FactKey{type, subject, scope},
                                      std::any(std::move(value)),
                                      typeid(TValue),
                                      persistence,
                                      expires_at});
    }

    void Remove(FactKey key)
    {
        mutations_.push_back(Mutation{MutationKind::Remove, key, {}, typeid(void), FactPersistence::Session, std::nullopt});
    }

    [[nodiscard]] GameplayDomainId Owner() const noexcept { return owner_; }
    [[nodiscard]] bool Empty() const noexcept { return mutations_.empty(); }

  private:
    enum class MutationKind
    {
        Set,
        Remove,
    };

    struct Mutation
    {
        MutationKind kind = MutationKind::Set;
        FactKey key{};
        std::any value;
        std::type_index value_type{typeid(void)};
        FactPersistence persistence = FactPersistence::Session;
        std::optional<GameplayTimePoint> expires_at{};
    };

    GameplayDomainId owner_{};
    std::vector<Mutation> mutations_;

    friend class GameplayFactsService;
};

struct FactsSnapshot
{
    std::vector<FactRecord> facts;
    std::vector<EventRecord> history;
    MonotonicIdGenerator<FactId>::Snapshot fact_ids{};
    MonotonicIdGenerator<EventId>::Snapshot event_ids{};
    Revision fact_revision{};
    std::uint64_t next_event_sequence = 1;
};

class GameplayFactsService
{
  public:
    GameplayFactsService();

    template <typename TPayload>
    [[nodiscard]] foundation::Result<EventTypeId> RegisterEventType(
        std::string_view canonical_name,
        GameplayDomainId owner,
        HistoryPolicy history_policy = HistoryPolicy::None,
        std::size_t recent_limit = 256)
    {
        if (frozen_)
        {
            return foundation::Result<EventTypeId>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "event registry is frozen", std::string(canonical_name)));
        }
        const EventTypeId type = EventTypeId::FromString(canonical_name);
        if (!type.IsValid() || !owner.IsValid())
        {
            return foundation::Result<EventTypeId>::Failure(
                foundation::Error::Create("gameplay.invalid_event_type", "event type name and owner must be valid", std::string(canonical_name)));
        }
        const auto found = event_types_.find(type);
        if (found != event_types_.end())
        {
            if (found->second.canonical_name != canonical_name)
            {
                return foundation::Result<EventTypeId>::Failure(
                    foundation::Error::Create("gameplay.id_collision", "event type id collision", std::string(canonical_name)));
            }
            return foundation::Result<EventTypeId>::Failure(
                foundation::Error::Create("gameplay.already_registered", "event type is already registered", std::string(canonical_name)));
        }

        event_types_.emplace(type, EventTypeInfo{type, std::string(canonical_name), owner, typeid(TPayload), history_policy, recent_limit});
        return foundation::Result<EventTypeId>::Success(type);
    }

    template <typename TValue>
    [[nodiscard]] foundation::Result<FactTypeId> RegisterFactType(std::string_view canonical_name, GameplayDomainId owner)
    {
        if (frozen_)
        {
            return foundation::Result<FactTypeId>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "fact registry is frozen", std::string(canonical_name)));
        }
        const FactTypeId type = FactTypeId::FromString(canonical_name);
        if (!type.IsValid() || !owner.IsValid())
        {
            return foundation::Result<FactTypeId>::Failure(
                foundation::Error::Create("gameplay.invalid_fact_type", "fact type name and owner must be valid", std::string(canonical_name)));
        }
        const auto found = fact_types_.find(type);
        if (found != fact_types_.end())
        {
            if (found->second.canonical_name != canonical_name)
            {
                return foundation::Result<FactTypeId>::Failure(
                    foundation::Error::Create("gameplay.id_collision", "fact type id collision", std::string(canonical_name)));
            }
            return foundation::Result<FactTypeId>::Failure(
                foundation::Error::Create("gameplay.already_registered", "fact type is already registered", std::string(canonical_name)));
        }

        fact_types_.emplace(type, FactTypeInfo{type, std::string(canonical_name), owner, typeid(TValue)});
        return foundation::Result<FactTypeId>::Success(type);
    }

    template <typename TPayload, typename TCallback>
    [[nodiscard]] foundation::Result<void> Subscribe(EventTypeId type, SubscriberId subscriber, int priority, TCallback&& callback)
    {
        if (frozen_)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "event subscriber registry is frozen"));
        }
        const auto found_type = event_types_.find(type);
        if (found_type == event_types_.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.event_unknown", "event type is not registered"));
        }
        if (found_type->second.payload_type != typeid(TPayload) || !subscriber.IsValid())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.event_subscription_invalid", "subscriber id or payload type is invalid"));
        }

        auto& list = subscribers_[type];
        const auto duplicate = std::find_if(list.begin(), list.end(), [subscriber](const Subscriber& item) { return item.id == subscriber; });
        if (duplicate != list.end())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.already_registered", "subscriber already registered for event type"));
        }

        list.push_back(Subscriber{subscriber,
                                  priority,
                                  [fn = std::forward<TCallback>(callback)](const EventRecord& event) {
                                      fn(event.envelope, std::any_cast<const TPayload&>(event.payload));
                                  }});
        return foundation::Result<void>::Success();
    }

    void Freeze();
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    template <typename TPayload>
    [[nodiscard]] foundation::Result<void> Publish(
        EventTypeId type,
        GameplayContext context,
        GameplayObjectRef subject,
        TPayload payload,
        ProducerId producer = {})
    {
        const auto validation = ValidateEventPayload(type, typeid(TPayload));
        if (!validation)
        {
            ++rejected_events_;
            return validation;
        }
        pending_events_.push_back(PendingEvent{type,
                                               std::move(context),
                                               subject,
                                               std::any(std::move(payload)),
                                               typeid(TPayload),
                                               producer,
                                               next_direct_order_++});
        ++published_events_;
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] GameplayEventBatch CreateBatch(ProducerId producer, std::uint64_t batch_order) const
    {
        return GameplayEventBatch(producer, batch_order);
    }

    [[nodiscard]] foundation::Result<void> SubmitBatch(GameplayEventBatch batch);
    [[nodiscard]] foundation::Result<std::uint64_t> Dispatch(EventDispatchLimits limits = {});

    [[nodiscard]] FactTransaction BeginTransaction(GameplayDomainId owner) const { return FactTransaction(owner); }
    [[nodiscard]] foundation::Result<std::vector<FactChange>> Commit(
        FactTransaction transaction,
        GameplayContext context,
        EventId source_event = {});

    [[nodiscard]] const FactRecord* FindFact(const FactKey& key) const noexcept;

    template <typename TValue> [[nodiscard]] const TValue* FindFactValue(const FactKey& key) const noexcept
    {
        const auto* fact = FindFact(key);
        if (fact == nullptr || fact->value_type != typeid(TValue))
        {
            return nullptr;
        }
        return std::any_cast<TValue>(&fact->value);
    }

    [[nodiscard]] std::vector<FactRecord> FindFacts(FactTypeId type, GameplayObjectRef subject = {}, GameplayObjectRef scope = {}) const;
    [[nodiscard]] foundation::Result<std::uint64_t> ExpireDueFacts(GameplayTimePoint now, GameplayContext context);

    [[nodiscard]] const std::vector<EventRecord>& History() const noexcept { return history_; }
    [[nodiscard]] std::vector<EventRecord> FindHistory(EventTypeId type, GameplayObjectRef subject = {}) const;
    [[nodiscard]] foundation::Result<void> CompactHistory(EventTypeId type, const IHistoryCompactor& compactor);

    [[nodiscard]] FactsSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(FactsSnapshot snapshot);
    [[nodiscard]] Revision FactRevision() const noexcept { return fact_revision_; }
    [[nodiscard]] EventTypeId FactChangedEventType() const noexcept { return fact_changed_event_type_; }
    [[nodiscard]] FactsDiagnostics GetDiagnostics() const noexcept;

  private:
    struct EventTypeInfo
    {
        EventTypeId id{};
        std::string canonical_name;
        GameplayDomainId owner{};
        std::type_index payload_type{typeid(void)};
        HistoryPolicy history_policy = HistoryPolicy::None;
        std::size_t recent_limit = 256;
    };

    struct FactTypeInfo
    {
        FactTypeId id{};
        std::string canonical_name;
        GameplayDomainId owner{};
        std::type_index value_type{typeid(void)};
    };

    struct Subscriber
    {
        SubscriberId id{};
        int priority = 0;
        std::function<void(const EventRecord&)> callback;
    };

    struct PendingEvent
    {
        EventTypeId type{};
        GameplayContext context{};
        GameplayObjectRef subject{};
        std::any payload;
        std::type_index payload_type{typeid(void)};
        ProducerId producer{};
        std::uint64_t order = 0;
    };

    [[nodiscard]] foundation::Result<void> ValidateEventPayload(EventTypeId type, std::type_index payload_type) const;
    [[nodiscard]] foundation::Result<void> ValidateMutation(const FactTransaction& transaction, const FactTransaction::Mutation& mutation) const;
    void MergeSubmittedBatches();
    void AppendHistory(const EventRecord& event);
    void PublishFactChanges(const std::vector<FactChange>& changes, const GameplayContext& context, EventId source_event);

    static constexpr std::string_view kFactChangedEventName = "framework.fact.changed";
    static constexpr std::string_view kFactsDomainName = "framework.facts";

    std::unordered_map<EventTypeId, EventTypeInfo> event_types_;
    std::unordered_map<FactTypeId, FactTypeInfo> fact_types_;
    std::unordered_map<EventTypeId, std::vector<Subscriber>> subscribers_;
    std::unordered_map<FactKey, FactRecord, FactKeyHash> facts_;
    std::vector<EventRecord> history_;
    std::vector<PendingEvent> pending_events_;
    std::vector<GameplayEventBatch> submitted_batches_;
    mutable std::mutex batch_mutex_;

    MonotonicIdGenerator<EventId> event_ids_{EventId::FromString("framework.events").High()};
    MonotonicIdGenerator<FactId> fact_ids_{FactId::FromString("framework.facts").High()};
    Revision fact_revision_{};
    std::uint64_t next_event_sequence_ = 1;
    std::uint64_t next_direct_order_ = 0;
    bool frozen_ = false;
    bool dispatching_ = false;
    EventTypeId fact_changed_event_type_{};

    std::atomic<std::uint64_t> published_events_{0};
    std::uint64_t dispatched_events_ = 0;
    std::uint64_t dispatch_waves_ = 0;
    std::atomic<std::uint64_t> rejected_events_{0};
};
} // namespace epidemic::gameplay::facts

