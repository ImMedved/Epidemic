#pragma once

#include <any>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace epidemic::core::events
{
// This file defines the minimal in-process event bus used by EngineBase.
// The bus supports immediate dispatch and queued dispatch so systems can choose whether
// handlers run synchronously at publish time or later during the frame loop drain phase.

enum class EventDispatchMode
{
    Sync,
    Queued,
};

class IEventBus
{
  public:
    using HandlerToken = std::uint64_t;
    using AnyEventHandler = std::function<void(const std::any &)>;

    virtual ~IEventBus() = default;

    // Registers a synchronous handler for EventT.
    template <typename EventT, typename HandlerT> HandlerToken SubscribeSync(HandlerT &&handler)
    {
        auto typed_handler = std::function<void(const EventT &)>(std::forward<HandlerT>(handler));
        if (!typed_handler)
        {
            throw std::invalid_argument("Event handler must be valid");
        }

        return SubscribeImpl(typeid(EventT), EventDispatchMode::Sync,
                             [typed_handler = std::move(typed_handler)](const std::any &payload) {
                                 typed_handler(std::any_cast<const EventT &>(payload));
                             });
    }

    // Registers a queued handler for EventT.
    template <typename EventT, typename HandlerT> HandlerToken SubscribeQueued(HandlerT &&handler)
    {
        auto typed_handler = std::function<void(const EventT &)>(std::forward<HandlerT>(handler));
        if (!typed_handler)
        {
            throw std::invalid_argument("Event handler must be valid");
        }

        return SubscribeImpl(typeid(EventT), EventDispatchMode::Queued,
                             [typed_handler = std::move(typed_handler)](const std::any &payload) {
                                 typed_handler(std::any_cast<const EventT &>(payload));
                             });
    }

    // Immediately dispatches one event to synchronous subscribers of EventT.
    template <typename EventT> void PublishSync(const EventT &event)
    {
        PublishSyncImpl(typeid(EventT), std::any(event));
    }

    // Queues one event for later delivery to queued subscribers of EventT.
    template <typename EventT> void Enqueue(const EventT &event)
    {
        EnqueueImpl(typeid(EventT), std::any(event));
    }

    // Drains queued events and returns the number of dispatched items.
    [[nodiscard]] virtual std::size_t DrainQueued() = 0;

    // Removes a previously registered handler token.
    [[nodiscard]] virtual bool Unsubscribe(HandlerToken token) = 0;

  private:
    // Registers a type-erased handler for the specified event type and dispatch mode.
    virtual HandlerToken SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler) = 0;

    // Immediately dispatches a type-erased event to synchronous subscribers.
    virtual void PublishSyncImpl(std::type_index event_type, std::any event) = 0;

    // Enqueues a type-erased event for later dispatch.
    virtual void EnqueueImpl(std::type_index event_type, std::any event) = 0;
};

class EventBus final : public IEventBus
{
  public:
    // Drains queued events in FIFO order and returns the number processed.
    [[nodiscard]] std::size_t DrainQueued() override;

    // Removes a handler token from either the synchronous or queued subscription tables.
    [[nodiscard]] bool Unsubscribe(HandlerToken token) override;

  private:
    struct Subscription
    {
        HandlerToken token{};
        AnyEventHandler handler;
    };

    struct QueuedEvent
    {
        std::type_index type{typeid(void)};
        std::any payload;
    };

    // Registers a type-erased handler in the correct subscription map.
    HandlerToken SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler) override;

    // Immediately dispatches an event to synchronous subscribers.
    void PublishSyncImpl(std::type_index event_type, std::any event) override;

    // Stores an event in the queued FIFO.
    void EnqueueImpl(std::type_index event_type, std::any event) override;

    // Copies synchronous subscribers for an event type and invokes them outside the lock.
    void DispatchSync(std::type_index event_type, const std::any &event);

    // Copies queued subscribers for an event type and invokes them outside the lock.
    void DispatchQueued(std::type_index event_type, const std::any &event);

    std::mutex drain_mutex_;
    std::mutex mutex_;
    HandlerToken next_token_{1};
    std::unordered_map<std::type_index, std::vector<Subscription>> sync_subscriptions_;
    std::unordered_map<std::type_index, std::vector<Subscription>> queued_subscriptions_;
    std::queue<QueuedEvent> queued_events_;
};
} // namespace epidemic::core::events
