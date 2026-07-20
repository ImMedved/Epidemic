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
// This file defines the simple service locator used by EngineBase composition.
// The container supports one long-lived instance per service type and becomes sealed after
// successful application initialization so runtime code cannot keep mutating the graph.

class ServiceContainer
{
  public:
    // Constructs and registers a service implementation under the requested interface type.
    // Relationship: validation happens before construction so sealed or duplicate failures do not build the instance.
    template <typename TService, typename TImplementation, typename... TArgs>
    std::shared_ptr<TService> Emplace(TArgs &&...args)
    {
        static_assert(std::is_base_of_v<TService, TImplementation>,
                      "Implementation must derive from the requested service interface");

        EnsureCanRegister<TService>();
        auto instance = std::make_shared<TImplementation>(std::forward<TArgs>(args)...);
        RegisterInstance<TService>(instance);
        return instance;
    }

    // Registers an externally created service instance under the requested interface type.
    template <typename TService> void RegisterInstance(std::shared_ptr<TService> instance)
    {
        if (!instance)
        {
            throw std::invalid_argument("Service instance must not be null");
        }

        std::unique_lock lock(mutex_);
        if (sealed_)
        {
            throw std::runtime_error("Service container is sealed");
        }

        const auto key = std::type_index(typeid(TService));
        if (services_.contains(key))
        {
            throw std::runtime_error("Service already registered");
        }

        services_.emplace(key, std::move(instance));
    }

    // Returns the registered service instance for TService.
    // TODO: If optional lookup becomes common, consider a non-throwing TryGet instead of forcing exception control flow.
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

    // Returns whether a service for TService is currently registered.
    template <typename TService> [[nodiscard]] bool Contains() const
    {
        std::shared_lock lock(mutex_);
        return services_.contains(std::type_index(typeid(TService)));
    }

    // Prevents further service registration.
    void Seal() noexcept
    {
        std::unique_lock lock(mutex_);
        sealed_ = true;
    }

    // Returns whether the container has been sealed.
    [[nodiscard]] bool IsSealed() const noexcept
    {
        std::shared_lock lock(mutex_);
        return sealed_;
    }

  private:
    // Checks duplicate and sealed state before constructing a new service implementation.
    template <typename TService> void EnsureCanRegister() const
    {
        std::shared_lock lock(mutex_);
        if (sealed_)
        {
            throw std::runtime_error("Service container is sealed");
        }

        if (services_.contains(std::type_index(typeid(TService))))
        {
            throw std::runtime_error("Service already registered");
        }
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
    bool sealed_{false};
};
} // namespace epidemic::core