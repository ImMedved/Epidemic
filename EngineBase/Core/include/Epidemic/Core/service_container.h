#pragma once

#include <array>
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

    // Atomically registers a bundle of externally created service instances.
    // Relationship: a candidate map is built under the container lock and swapped into place only after
    // every insertion succeeds, so duplicate/sealed/allocation failures leave the live graph unchanged.
    template <typename... TServices> void RegisterInstancesAtomic(std::shared_ptr<TServices>... instances)
    {
        RegisterInstancesAtomicWithPreCommit([] {}, std::move(instances)...);
    }

    // Prepares the service-map entry before invoking a factory that may commit external state.
    // Once the factory has returned a non-null instance, filling the reserved node and swapping
    // the candidate map are no-throw operations, so the external owner and service graph commit together.
    // The factory executes while the container is exclusively locked and must not re-enter this container.
    template <typename TService, typename TFactory>
    std::shared_ptr<TService> RegisterInstanceFromFactoryAtomic(TFactory &&factory)
    {
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

        auto candidate = services_;
        candidate.reserve(candidate.size() + 1);
        const auto [slot, inserted] = candidate.emplace(key, std::shared_ptr<void>{});
        if (!inserted)
        {
            throw std::runtime_error("Service already registered");
        }

        auto instance = std::forward<TFactory>(factory)();
        if (!instance)
        {
            throw std::invalid_argument("Service factory returned a null instance");
        }

        // `slot` already owns a node and shared_ptr assignment is noexcept.
        slot->second = instance;
        services_.swap(candidate);
        return instance;
    }

    // Stages a service bundle, runs one external strong-guarantee pre-commit action, then publishes the
    // service map with a no-allocation swap. The callback must not re-enter this ServiceContainer.
    template <typename TPreCommit, typename... TServices>
    void RegisterInstancesAtomicWithPreCommit(TPreCommit &&pre_commit, std::shared_ptr<TServices>... instances)
    {
        static_assert(sizeof...(TServices) > 0, "Atomic service registration requires at least one service");

        if ((!instances || ...))
        {
            throw std::invalid_argument("Service bundle instances must not be null");
        }

        const std::array<std::type_index, sizeof...(TServices)> keys{std::type_index(typeid(TServices))...};
        for (std::size_t left = 0; left < keys.size(); ++left)
        {
            for (std::size_t right = left + 1; right < keys.size(); ++right)
            {
                if (keys[left] == keys[right])
                {
                    throw std::invalid_argument("Service bundle contains duplicate service types");
                }
            }
        }

        std::unique_lock lock(mutex_);
        if (sealed_)
        {
            throw std::runtime_error("Service container is sealed");
        }

        if ((services_.contains(std::type_index(typeid(TServices))) || ...))
        {
            throw std::runtime_error("Service already registered");
        }

        auto candidate = services_;
        (candidate.emplace(std::type_index(typeid(TServices)), std::shared_ptr<void>(instances)), ...);

        // The service candidate is complete before external composition is touched. Support uses this
        // hook only with operations that themselves provide a strong exception guarantee.
        std::forward<TPreCommit>(pre_commit)();
        services_.swap(candidate);
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
