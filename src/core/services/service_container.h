#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace epidemic::core
{
class ServiceContainer
{
  public:
    template <typename TService, typename TImplementation, typename... TArgs>
    std::shared_ptr<TService> Emplace(TArgs &&...args)
    {
        static_assert(std::is_base_of_v<TService, TImplementation>,
                      "Implementation must derive from the requested service interface");

        auto instance = std::make_shared<TImplementation>(std::forward<TArgs>(args)...);
        RegisterInstance<TService>(instance);
        return instance;
    }

    template <typename TService> void RegisterInstance(std::shared_ptr<TService> instance)
    {
        if (!instance)
        {
            throw std::invalid_argument("Service instance must not be null");
        }

        std::unique_lock lock(mutex_);
        const auto key = std::type_index(typeid(TService));
        if (services_.contains(key))
        {
            throw std::runtime_error("Service already registered");
        }

        services_.emplace(key, std::move(instance));
    }

    template <typename TService> [[nodiscard]] std::shared_ptr<TService> Get() const
    {
        std::shared_lock lock(mutex_);
        const auto it = services_.find(std::type_index(typeid(TService)));
        if (it == services_.end())
        {
            throw std::runtime_error("Requested service is not registered");
        }

        return std::static_pointer_cast<TService>(it->second);
    }

    template <typename TService> [[nodiscard]] bool Contains() const
    {
        std::shared_lock lock(mutex_);
        return services_.contains(std::type_index(typeid(TService)));
    }

  private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};
} // namespace epidemic::core
