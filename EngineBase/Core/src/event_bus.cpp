#include <Epidemic/Core/event_bus.h>

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/profiling.h>

#include <stdexcept>
#include <utility>

namespace epidemic::core::events
{
IEventBus::HandlerToken EventBus::SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler)
{
    if (!handler)
    {
        throw std::invalid_argument("Event handler must be valid");
    }

    std::scoped_lock lock(mutex_);

    auto &subscriptions = mode == EventDispatchMode::Sync ? sync_subscriptions_ : queued_subscriptions_;
    auto &bucket = subscriptions[event_type];
    bucket.push_back(Subscription{next_token_, std::move(handler)});
    return next_token_++;
}

void EventBus::PublishSyncImpl(std::type_index event_type, std::any event)
{
    DispatchSync(event_type, event);
}

void EventBus::EnqueueImpl(std::type_index event_type, std::any event)
{
    std::scoped_lock lock(mutex_);
    queued_events_.push(QueuedEvent{event_type, std::move(event)});
    diagnostics::GlobalCounters().Increment(diagnostics::CounterId::EventBusQueuedEvents);
}

std::size_t EventBus::DrainQueued()
{
    EPIDEMIC_PROFILE_SCOPE("EventBus::DrainQueued");
    std::size_t drained = 0;

    while (true)
    {
        QueuedEvent next_event;
        {
            std::scoped_lock lock(mutex_);
            if (queued_events_.empty())
            {
                break;
            }

            next_event = std::move(queued_events_.front());
            queued_events_.pop();
        }

        diagnostics::GlobalCounters().Decrement(diagnostics::CounterId::EventBusQueuedEvents);
        diagnostics::GlobalCounters().Increment(diagnostics::CounterId::EventBusDispatchedEvents);
        DispatchQueued(next_event.type, next_event.payload);
        ++drained;
    }

    return drained;
}

bool EventBus::Unsubscribe(HandlerToken token)
{
    std::scoped_lock lock(mutex_);

    auto erase_from_subscriptions = [token](auto &subscriptions) {
        for (auto subscription_it = subscriptions.begin(); subscription_it != subscriptions.end(); ++subscription_it)
        {
            auto &bucket = subscription_it->second;
            for (auto bucket_it = bucket.begin(); bucket_it != bucket.end(); ++bucket_it)
            {
                if (bucket_it->token == token)
                {
                    bucket.erase(bucket_it);
                    if (bucket.empty())
                    {
                        subscriptions.erase(subscription_it);
                    }
                    return true;
                }
            }
        }

        return false;
    };

    return erase_from_subscriptions(sync_subscriptions_) || erase_from_subscriptions(queued_subscriptions_);
}

void EventBus::DispatchSync(std::type_index event_type, const std::any &event)
{
    std::vector<AnyEventHandler> handlers;
    {
        std::scoped_lock lock(mutex_);
        const auto it = sync_subscriptions_.find(event_type);
        if (it == sync_subscriptions_.end())
        {
            return;
        }

        handlers.reserve(it->second.size());
        for (const auto &subscription : it->second)
        {
            handlers.push_back(subscription.handler);
        }
    }

    for (const auto &handler : handlers)
    {
        handler(event);
    }
}

void EventBus::DispatchQueued(std::type_index event_type, const std::any &event)
{
    std::vector<AnyEventHandler> handlers;
    {
        std::scoped_lock lock(mutex_);
        const auto it = queued_subscriptions_.find(event_type);
        if (it == queued_subscriptions_.end())
        {
            return;
        }

        handlers.reserve(it->second.size());
        for (const auto &subscription : it->second)
        {
            handlers.push_back(subscription.handler);
        }
    }

    for (const auto &handler : handlers)
    {
        handler(event);
    }
}
} // namespace epidemic::core::events