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

    template <typename EventT> void PublishSync(const EventT &event)
    {
        PublishSyncImpl(typeid(EventT), std::any(event));
    }

    template <typename EventT> void Enqueue(const EventT &event)
    {
        EnqueueImpl(typeid(EventT), std::any(event));
    }

    [[nodiscard]] virtual std::size_t DrainQueued() = 0;
    [[nodiscard]] virtual bool Unsubscribe(HandlerToken token) = 0;

  private:
    virtual HandlerToken SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler) = 0;
    virtual void PublishSyncImpl(std::type_index event_type, std::any event) = 0;
    virtual void EnqueueImpl(std::type_index event_type, std::any event) = 0;
};

class EventBus final : public IEventBus
{
  public:
    [[nodiscard]] std::size_t DrainQueued() override;
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

    HandlerToken SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler) override;
    void PublishSyncImpl(std::type_index event_type, std::any event) override;
    void EnqueueImpl(std::type_index event_type, std::any event) override;

    void DispatchSync(std::type_index event_type, const std::any &event);
    void DispatchQueued(std::type_index event_type, const std::any &event);

    std::mutex mutex_;
    HandlerToken next_token_{1};
    std::unordered_map<std::type_index, std::vector<Subscription>> sync_subscriptions_;
    std::unordered_map<std::type_index, std::vector<Subscription>> queued_subscriptions_;
    std::queue<QueuedEvent> queued_events_;
};
} // namespace epidemic::core::events