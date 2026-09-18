#include <Epidemic/Core/event_bus.h>

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/profiling.h>

#include "event_bus_token_policy.h"

#include <stdexcept>
#include <utility>

namespace epidemic::core::events
{
// This file implements the baseline event bus.
// Delivery copies the current handler list before invocation so handlers run outside the internal mutex.

// Registers a type-erased handler in the sync or queued subscription table.
IEventBus::HandlerToken EventBus::SubscribeImpl(std::type_index event_type, EventDispatchMode mode, AnyEventHandler handler)
{
    if (!handler)
    {
        throw std::invalid_argument("Event handler must be valid");
    }

    std::scoped_lock lock(mutex_);

    if (!detail::CanAllocateHandlerToken(next_token_))
    {
        throw std::overflow_error("EventBus handler token space is exhausted");
    }

    auto &subscriptions = mode == EventDispatchMode::Sync ? sync_subscriptions_ : queued_subscriptions_;
    const auto token = next_token_;
    if (const auto existing = subscriptions.find(event_type); existing != subscriptions.end())
    {
        existing->second.push_back(Subscription{token, std::move(handler)});
    }
    else
    {
        std::vector<Subscription> candidate_bucket;
        candidate_bucket.push_back(Subscription{token, std::move(handler)});
        subscriptions.emplace(event_type, std::move(candidate_bucket));
    }
    next_token_ = detail::AdvanceHandlerToken(token);
    return token;
}

// Immediately dispatches a type-erased event to synchronous subscribers.
void EventBus::PublishSyncImpl(std::type_index event_type, std::any event)
{
    DispatchSync(event_type, event);
}

// Stores a type-erased event for delivery during DrainQueued().
void EventBus::EnqueueImpl(std::type_index event_type, std::any event)
{
    std::scoped_lock lock(mutex_);
    queued_events_.push(QueuedEvent{event_type, std::move(event)});
    diagnostics::GlobalCounters().Increment(diagnostics::CounterId::EventBusQueuedEvents);
}

// Drains queued events in FIFO order and returns the number dispatched.
std::size_t EventBus::DrainQueued()
{
    EPIDEMIC_PROFILE_SCOPE("EventBus::DrainQueued");
    std::scoped_lock drain_lock(drain_mutex_);
    std::size_t drained = 0;
    std::size_t drain_budget = 0;
    {
        std::scoped_lock lock(mutex_);
        drain_budget = queued_events_.size();
    }

    while (drained < drain_budget)
    {
        QueuedEvent next_event;
        std::vector<AnyEventHandler> handlers;
        {
            std::scoped_lock lock(mutex_);
            if (queued_events_.empty())
            {
                break;
            }

            // Copy every potentially throwing subscriber before removing the event. If copying fails,
            // the queued event remains authoritative and a later DrainQueued() can retry it.
            const auto &front = queued_events_.front();
            if (const auto subscriptions = queued_subscriptions_.find(front.type); subscriptions != queued_subscriptions_.end())
            {
                handlers.reserve(subscriptions->second.size());
                for (const auto &subscription : subscriptions->second)
                {
                    handlers.push_back(subscription.handler);
                }
            }

            next_event = std::move(queued_events_.front());
            queued_events_.pop();
        }

        diagnostics::GlobalCounters().Decrement(diagnostics::CounterId::EventBusQueuedEvents);
        diagnostics::GlobalCounters().Increment(diagnostics::CounterId::EventBusDispatchedEvents);
        for (const auto &handler : handlers)
        {
            handler(next_event.payload);
        }
        ++drained;
    }

    return drained;
}

// Removes a handler token from either subscription table.
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

// Copies synchronous subscribers and invokes them outside the lock.
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

// Copies queued subscribers and invokes them outside the lock.
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
