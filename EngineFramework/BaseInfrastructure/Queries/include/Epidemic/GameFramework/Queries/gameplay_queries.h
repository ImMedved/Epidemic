#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <any>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

namespace epidemic::gameplay::queries
{
enum class QueryConsistency
{
    Current,
    Snapshot,
};

enum class QueryCoverage
{
    Complete,
    Partial,
    Unavailable,
};

enum class QueryAccuracy
{
    Exact,
    Approximate,
};

struct QueryRequirements
{
    bool require_complete = true;
    bool allow_approximate = false;
    bool allow_unavailable = false;
};

struct QueryBudget
{
    std::uint64_t max_results = 0;
    std::uint64_t max_work_units = 0;

    [[nodiscard]] constexpr bool HasResultLimit() const noexcept { return max_results != 0; }
    [[nodiscard]] constexpr bool HasWorkLimit() const noexcept { return max_work_units != 0; }
};

struct QueryContext
{
    GameplayTickId tick{};
    QueryConsistency consistency = QueryConsistency::Current;
    QueryRequirements requirements{};
    QueryBudget budget{};
    std::uint64_t snapshot_epoch = 0;
};

struct QuerySnapshotReadEpoch
{
    std::uint64_t value = 0;
    std::shared_ptr<const void> lease{};

    [[nodiscard]] bool IsValid() const noexcept { return value != 0 && lease != nullptr; }
};

class IQuerySnapshotCoordinator
{
  public:
    virtual ~IQuerySnapshotCoordinator() = default;
    [[nodiscard]] virtual foundation::Result<QuerySnapshotReadEpoch> AcquireReadEpoch(GameplayTickId tick) const = 0;
};

struct QueryMetadata
{
    QueryId query_id{};
    Revision revision{};
    QueryCoverage coverage = QueryCoverage::Complete;
    std::uint64_t result_count = 0;
    std::uint64_t work_units = 0;
    bool deterministic_order = true;
    QueryAccuracy accuracy = QueryAccuracy::Exact;
    bool resident_only = false;
};

template <typename TValue> struct QueryResponse
{
    std::optional<TValue> value{};
    QueryMetadata metadata{};
};

template <typename TSnapshot> struct QueryProviderSnapshot
{
    TSnapshot value{};
    Revision revision{};
};

struct QueryProviderCapabilities
{
    bool supports_current = true;
    bool supports_snapshot = false;
    bool can_return_partial = false;
    bool can_return_approximate = false;
};

struct QueryDiagnostics
{
    std::uint64_t executed = 0;
    std::uint64_t failed = 0;
    std::uint64_t budget_exceeded = 0;
    std::uint64_t partial_results = 0;
};

namespace detail
{
struct AnyProviderSnapshot
{
    std::any value;
    std::type_index value_type{typeid(void)};
    Revision revision{};
};

struct AnyQueryResponse
{
    std::any value;
    std::type_index value_type{typeid(void)};
    QueryMetadata metadata{};
    bool has_value = false;
};
} // namespace detail

class QuerySnapshot
{
  public:
    QuerySnapshot() = default;

    [[nodiscard]] bool IsValid() const noexcept { return views_ != nullptr; }
    [[nodiscard]] GameplayTickId Tick() const noexcept { return tick_; }
    [[nodiscard]] std::size_t ProviderCount() const noexcept { return views_ ? views_->size() : 0; }

  private:
    using SnapshotMap = std::unordered_map<QueryTypeId, detail::AnyProviderSnapshot>;

    QuerySnapshot(GameplayTickId tick, std::shared_ptr<const SnapshotMap> views) noexcept
        : tick_(tick), views_(std::move(views))
    {
    }

    [[nodiscard]] const detail::AnyProviderSnapshot* Find(QueryTypeId type) const noexcept
    {
        if (!views_)
        {
            return nullptr;
        }
        const auto found = views_->find(type);
        return found == views_->end() ? nullptr : &found->second;
    }

    GameplayTickId tick_{};
    std::shared_ptr<const SnapshotMap> views_{};

    friend class GameplayQueryService;
};

namespace detail
{
class IQueryProvider
{
  public:
    virtual ~IQueryProvider() = default;
    [[nodiscard]] virtual QueryTypeId Type() const noexcept = 0;
    [[nodiscard]] virtual std::type_index QueryCppType() const noexcept = 0;
    [[nodiscard]] virtual std::type_index ResultCppType() const noexcept = 0;
    [[nodiscard]] virtual QueryProviderCapabilities Capabilities() const noexcept = 0;
    [[nodiscard]] virtual foundation::Result<AnyQueryResponse> ExecuteCurrent(const std::any& query, const QueryContext& context) const = 0;
    [[nodiscard]] virtual foundation::Result<AnyProviderSnapshot> CaptureSnapshot(const QueryContext& context) const = 0;
    [[nodiscard]] virtual foundation::Result<AnyQueryResponse> ExecuteSnapshot(
        const std::any& query,
        const QueryContext& context,
        const AnyProviderSnapshot& snapshot) const = 0;
};

template <typename TQuery> class FunctionQueryProvider final : public IQueryProvider
{
  public:
    using ResultType = typename TQuery::ResultType;
    using CurrentHandler = std::function<foundation::Result<QueryResponse<ResultType>>(const TQuery&, const QueryContext&)>;
    using SnapshotHandler = std::function<foundation::Result<QueryResponse<ResultType>>(const TQuery&, const QueryContext&, const std::any&)>;
    using SnapshotCapture = std::function<foundation::Result<AnyProviderSnapshot>(const QueryContext&)>;

    FunctionQueryProvider(
        QueryTypeId type,
        QueryProviderCapabilities capabilities,
        CurrentHandler current_handler,
        SnapshotCapture snapshot_capture = {},
        SnapshotHandler snapshot_handler = {})
        : type_(type),
          capabilities_(capabilities),
          current_handler_(std::move(current_handler)),
          snapshot_capture_(std::move(snapshot_capture)),
          snapshot_handler_(std::move(snapshot_handler))
    {
    }

    [[nodiscard]] QueryTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] std::type_index QueryCppType() const noexcept override { return typeid(TQuery); }
    [[nodiscard]] std::type_index ResultCppType() const noexcept override { return typeid(ResultType); }
    [[nodiscard]] QueryProviderCapabilities Capabilities() const noexcept override { return capabilities_; }

    [[nodiscard]] foundation::Result<AnyQueryResponse> ExecuteCurrent(const std::any& query, const QueryContext& context) const override
    {
        if (!current_handler_)
        {
            return foundation::Result<AnyQueryResponse>::Failure(
                foundation::Error::Create("gameplay.query_current_unsupported", "query provider does not support current-state reads"));
        }
        if (query.type() != typeid(TQuery))
        {
            return foundation::Result<AnyQueryResponse>::Failure(
                foundation::Error::Create("gameplay.query_type_mismatch", "query payload C++ type does not match provider"));
        }
        return Pack(current_handler_(std::any_cast<const TQuery&>(query), context));
    }

    [[nodiscard]] foundation::Result<AnyProviderSnapshot> CaptureSnapshot(const QueryContext& context) const override
    {
        if (!snapshot_capture_)
        {
            return foundation::Result<AnyProviderSnapshot>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_unsupported", "query provider does not expose a snapshot read-view"));
        }
        return snapshot_capture_(context);
    }

    [[nodiscard]] foundation::Result<AnyQueryResponse> ExecuteSnapshot(
        const std::any& query,
        const QueryContext& context,
        const AnyProviderSnapshot& snapshot) const override
    {
        if (!snapshot_handler_)
        {
            return foundation::Result<AnyQueryResponse>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_unsupported", "query provider does not support snapshot reads"));
        }
        if (query.type() != typeid(TQuery))
        {
            return foundation::Result<AnyQueryResponse>::Failure(
                foundation::Error::Create("gameplay.query_type_mismatch", "query payload C++ type does not match provider"));
        }
        return Pack(snapshot_handler_(std::any_cast<const TQuery&>(query), context, snapshot.value));
    }

  private:
    [[nodiscard]] static foundation::Result<AnyQueryResponse> Pack(foundation::Result<QueryResponse<ResultType>> result)
    {
        if (!result)
        {
            return foundation::Result<AnyQueryResponse>::Failure(result.GetError());
        }

        auto typed = result.Value();
        AnyQueryResponse response;
        response.value_type = typeid(ResultType);
        response.metadata = typed.metadata;
        response.has_value = typed.value.has_value();
        if (typed.value)
        {
            response.value = std::move(*typed.value);
        }
        return foundation::Result<AnyQueryResponse>::Success(std::move(response));
    }

    QueryTypeId type_{};
    QueryProviderCapabilities capabilities_{};
    CurrentHandler current_handler_{};
    SnapshotCapture snapshot_capture_{};
    SnapshotHandler snapshot_handler_{};
};

struct QueryServiceState
{
    std::unordered_map<QueryTypeId, std::shared_ptr<IQueryProvider>> providers;
    std::unordered_map<QueryTypeId, std::string> names;
    std::vector<QueryTypeId> provider_order;
    std::atomic<std::uint64_t> next_query_id{1};
    std::atomic<std::uint64_t> executed{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> budget_exceeded{0};
    std::atomic<std::uint64_t> partial_results{0};
};
} // namespace detail

class GameplayQueryService
{
  public:
    GameplayQueryService() = default;
    explicit GameplayQueryService(std::uint64_t initial_query_id) { state_->next_query_id.store(initial_query_id, std::memory_order_relaxed); }

    GameplayQueryService(const GameplayQueryService&) = delete;
    GameplayQueryService& operator=(const GameplayQueryService&) = delete;
    GameplayQueryService(GameplayQueryService&&) noexcept = default;
    GameplayQueryService& operator=(GameplayQueryService&&) noexcept = default;

    template <typename TQuery>
    [[nodiscard]] foundation::Result<void> RegisterProvider(
        std::string_view canonical_name,
        QueryProviderCapabilities capabilities,
        typename detail::FunctionQueryProvider<TQuery>::CurrentHandler handler)
    {
        return RegisterProviderInternal<TQuery>(canonical_name, capabilities, std::move(handler));
    }

    template <typename TQuery, typename TSnapshot>
    [[nodiscard]] foundation::Result<void> RegisterSnapshotProvider(
        std::string_view canonical_name,
        QueryProviderCapabilities capabilities,
        typename detail::FunctionQueryProvider<TQuery>::CurrentHandler current_handler,
        std::function<foundation::Result<QueryProviderSnapshot<TSnapshot>>(const QueryContext&)> capture_snapshot,
        std::function<foundation::Result<QueryResponse<typename TQuery::ResultType>>(const TQuery&, const QueryContext&, const TSnapshot&)> snapshot_handler)
    {
        if (!capture_snapshot || !snapshot_handler)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.invalid_query_provider", "snapshot provider requires capture and snapshot handlers"));
        }

        capabilities.supports_snapshot = true;
        typename detail::FunctionQueryProvider<TQuery>::SnapshotCapture capture =
            [capture_snapshot = std::move(capture_snapshot)](const QueryContext& context) -> foundation::Result<detail::AnyProviderSnapshot> {
            auto captured = capture_snapshot(context);
            if (!captured)
            {
                return foundation::Result<detail::AnyProviderSnapshot>::Failure(captured.GetError());
            }
            auto typed = captured.Value();
            return foundation::Result<detail::AnyProviderSnapshot>::Success(
                detail::AnyProviderSnapshot{std::any(std::move(typed.value)), typeid(TSnapshot), typed.revision});
        };
        typename detail::FunctionQueryProvider<TQuery>::SnapshotHandler execute_snapshot =
            [snapshot_handler = std::move(snapshot_handler)](
                const TQuery& query,
                const QueryContext& context,
                const std::any& snapshot) -> foundation::Result<QueryResponse<typename TQuery::ResultType>> {
            if (snapshot.type() != typeid(TSnapshot))
            {
                return foundation::Result<QueryResponse<typename TQuery::ResultType>>::Failure(
                    foundation::Error::Create("gameplay.query_snapshot_type_mismatch", "snapshot payload C++ type does not match provider"));
            }
            return snapshot_handler(query, context, std::any_cast<const TSnapshot&>(snapshot));
        };

        return RegisterProviderInternal<TQuery>(
            canonical_name,
            capabilities,
            std::move(current_handler),
            std::move(capture),
            std::move(execute_snapshot));
    }

    [[nodiscard]] foundation::Result<void> SetSnapshotCoordinator(const IQuerySnapshotCoordinator* coordinator)
    {
        if (frozen_)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "query snapshot coordinator cannot change after Freeze"));
        }
        if (coordinator == nullptr)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_coordinator_invalid", "query snapshot coordinator must be valid"));
        }
        snapshot_coordinator_ = coordinator;
        return foundation::Result<void>::Success();
    }

    void Freeze() noexcept
    {
        state_->provider_order.clear();
        state_->provider_order.reserve(state_->providers.size());
        for (const auto& [type, _] : state_->providers)
        {
            state_->provider_order.push_back(type);
        }
        std::sort(state_->provider_order.begin(), state_->provider_order.end());
        frozen_ = true;
    }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t ProviderCount() const noexcept { return state_->providers.size(); }

    [[nodiscard]] foundation::Result<QuerySnapshot> AcquireSnapshot(QueryContext context = {}) const
    {
        if (!frozen_)
        {
            ++state_->failed;
            return foundation::Result<QuerySnapshot>::Failure(
                foundation::Error::Create("gameplay.query_registry_not_frozen", "query provider registry must be frozen before acquiring snapshots"));
        }

        context.consistency = QueryConsistency::Snapshot;
        bool has_snapshot_provider = false;
        for (const auto type : state_->provider_order)
        {
            const auto found = state_->providers.find(type);
            if (found != state_->providers.end() && found->second->Capabilities().supports_snapshot)
            {
                has_snapshot_provider = true;
                break;
            }
        }
        QuerySnapshotReadEpoch epoch;
        if (has_snapshot_provider)
        {
            if (snapshot_coordinator_ == nullptr)
            {
                ++state_->failed;
                return foundation::Result<QuerySnapshot>::Failure(
                    foundation::Error::Create("gameplay.query_snapshot_coordinator_missing",
                                              "coherent snapshots require a configured read-epoch coordinator"));
            }
            auto acquired = snapshot_coordinator_->AcquireReadEpoch(context.tick);
            if (!acquired || !acquired.Value().IsValid())
            {
                ++state_->failed;
                return foundation::Result<QuerySnapshot>::Failure(
                    acquired ? foundation::Error::Create("gameplay.query_snapshot_epoch_invalid", "snapshot coordinator returned an invalid epoch")
                             : acquired.GetError());
            }
            epoch = std::move(acquired).Value();
            context.snapshot_epoch = epoch.value;
        }

        auto views = std::make_shared<QuerySnapshot::SnapshotMap>();
        for (const auto type : state_->provider_order)
        {
            const auto found = state_->providers.find(type);
            if (found == state_->providers.end())
            {
                continue;
            }
            const auto& provider = found->second;
            const auto capabilities = provider->Capabilities();
            if (!capabilities.supports_snapshot)
            {
                continue;
            }
            auto captured = provider->CaptureSnapshot(context);
            if (!captured)
            {
                ++state_->failed;
                return foundation::Result<QuerySnapshot>::Failure(captured.GetError());
            }
            views->emplace(type, std::move(captured).Value());
        }
        return foundation::Result<QuerySnapshot>::Success(QuerySnapshot{context.tick, std::move(views)});
    }

    template <typename TQuery>
    [[nodiscard]] foundation::Result<QueryResponse<typename TQuery::ResultType>> Execute(const TQuery& query, QueryContext context = {}) const
    {
        if (context.consistency == QueryConsistency::Snapshot)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<typename TQuery::ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_required", "snapshot queries require an explicit QuerySnapshot"));
        }
        return ExecuteImpl(query, nullptr, context);
    }

    template <typename TQuery>
    [[nodiscard]] foundation::Result<QueryResponse<typename TQuery::ResultType>> Execute(
        const TQuery& query,
        const QuerySnapshot& snapshot,
        QueryContext context = {}) const
    {
        if (!snapshot.IsValid())
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<typename TQuery::ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_invalid", "query snapshot is invalid"));
        }
        context.consistency = QueryConsistency::Snapshot;
        context.tick = snapshot.Tick();
        return ExecuteImpl(query, &snapshot, context);
    }

    [[nodiscard]] QueryDiagnostics GetDiagnostics() const noexcept
    {
        return QueryDiagnostics{state_->executed.load(), state_->failed.load(), state_->budget_exceeded.load(), state_->partial_results.load()};
    }

  private:
    template <typename TQuery>
    [[nodiscard]] foundation::Result<void> RegisterProviderInternal(
        std::string_view canonical_name,
        QueryProviderCapabilities capabilities,
        typename detail::FunctionQueryProvider<TQuery>::CurrentHandler current_handler,
        typename detail::FunctionQueryProvider<TQuery>::SnapshotCapture snapshot_capture = {},
        typename detail::FunctionQueryProvider<TQuery>::SnapshotHandler snapshot_handler = {})
    {
        if (frozen_)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "query provider registry is frozen"));
        }
        if (!current_handler && capabilities.supports_current)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.invalid_query_provider", "current query provider handler must be valid"));
        }

        const QueryTypeId expected = QueryTypeId::FromString(canonical_name);
        if (expected != TQuery::Type())
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.query_type_name_mismatch", "query canonical name does not match TQuery::Type()", std::string(canonical_name)));
        }
        if (state_->providers.contains(expected))
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.already_registered", "query provider already registered", std::string(canonical_name)));
        }

        state_->providers.emplace(
            expected,
            std::make_shared<detail::FunctionQueryProvider<TQuery>>(
                expected,
                capabilities,
                std::move(current_handler),
                std::move(snapshot_capture),
                std::move(snapshot_handler)));
        state_->names.emplace(expected, std::string(canonical_name));
        return foundation::Result<void>::Success();
    }

    template <typename TQuery>
    [[nodiscard]] foundation::Result<QueryResponse<typename TQuery::ResultType>> ExecuteImpl(
        const TQuery& query,
        const QuerySnapshot* snapshot,
        QueryContext context) const
    {
        using ResultType = typename TQuery::ResultType;
        ++state_->executed;

        if (!frozen_)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_registry_not_frozen", "query provider registry must be frozen before executing queries"));
        }

        const auto found = state_->providers.find(TQuery::Type());
        if (found == state_->providers.end())
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_unknown", "no provider registered for query type"));
        }

        const auto& provider = found->second;
        const auto capabilities = provider->Capabilities();
        if (context.consistency == QueryConsistency::Snapshot && !capabilities.supports_snapshot)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_snapshot_unsupported", "query provider does not support snapshot reads"));
        }
        if (context.consistency == QueryConsistency::Current && !capabilities.supports_current)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_current_unsupported", "query provider does not support current-state reads"));
        }

        foundation::Result<detail::AnyQueryResponse> any_result = foundation::Result<detail::AnyQueryResponse>::Failure(
            foundation::Error::Create("gameplay.query_internal_error", "query did not execute"));
        if (context.consistency == QueryConsistency::Snapshot)
        {
            const auto* provider_snapshot = snapshot ? snapshot->Find(TQuery::Type()) : nullptr;
            if (!provider_snapshot)
            {
                ++state_->failed;
                return foundation::Result<QueryResponse<ResultType>>::Failure(
                    foundation::Error::Create("gameplay.query_snapshot_unavailable", "snapshot does not contain a read-view for query provider"));
            }
            any_result = provider->ExecuteSnapshot(std::any(query), context, *provider_snapshot);
        }
        else
        {
            any_result = provider->ExecuteCurrent(std::any(query), context);
        }

        if (!any_result)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(any_result.GetError());
        }

        auto response = std::move(any_result).Value();
        auto query_id = NextQueryId();
        if (!query_id)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(query_id.GetError());
        }
        response.metadata.query_id = query_id.Value();

        const auto validation = ValidateResponse(response, capabilities, context);
        if (!validation)
        {
            ++state_->failed;
            if (validation.GetError().HasCode("gameplay.query_budget_exceeded"))
            {
                ++state_->budget_exceeded;
            }
            return foundation::Result<QueryResponse<ResultType>>::Failure(validation.GetError());
        }

        if (response.metadata.coverage == QueryCoverage::Partial)
        {
            ++state_->partial_results;
        }
        if (response.value_type != typeid(ResultType))
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_result_type_mismatch", "query provider returned an unexpected C++ result type"));
        }

        if (!response.has_value)
        {
            return foundation::Result<QueryResponse<ResultType>>::Success(QueryResponse<ResultType>{std::nullopt, response.metadata});
        }

        try
        {
            return foundation::Result<QueryResponse<ResultType>>::Success(
                QueryResponse<ResultType>{std::optional<ResultType>{std::any_cast<ResultType>(std::move(response.value))}, response.metadata});
        }
        catch (const std::bad_any_cast&)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_result_type_mismatch", "query result payload could not be cast to expected type"));
        }
    }

    [[nodiscard]] foundation::Result<QueryId> NextQueryId() const noexcept;
    [[nodiscard]] static foundation::Result<void> ValidateResponse(
        const detail::AnyQueryResponse& response,
        QueryProviderCapabilities capabilities,
        const QueryContext& context) noexcept;
    [[nodiscard]] static bool RequirementsSatisfied(QueryMetadata metadata, QueryRequirements requirements) noexcept;

    std::shared_ptr<detail::QueryServiceState> state_ = std::make_shared<detail::QueryServiceState>();
    const IQuerySnapshotCoordinator* snapshot_coordinator_ = nullptr;
    bool frozen_ = false;
};
} // namespace epidemic::gameplay::queries