#pragma once

#include "Epidemic/Core/task_scheduler.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <any>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>

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
    ResidentOnly,
    Partial,
    Approximate,
};

enum class QueryCoverageRequirement
{
    AllowApproximate,
    RequireResidentData,
    RequireComplete,
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
    QueryCoverageRequirement coverage = QueryCoverageRequirement::RequireComplete;
    QueryBudget budget{};
    Revision snapshot_revision{};
};

struct QueryMetadata
{
    QueryId query_id{};
    Revision revision{};
    QueryCoverage coverage = QueryCoverage::Complete;
    std::uint64_t result_count = 0;
    std::uint64_t work_units = 0;
    bool deterministic_order = true;
};

template <typename TValue> struct QueryResponse
{
    TValue value{};
    QueryMetadata metadata{};
};

struct QueryCursor
{
    QueryId query_id{};
    Revision revision{};
    std::uint64_t offset = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept { return query_id.IsValid(); }
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
struct AnyQueryResponse
{
    std::any value;
    std::type_index value_type{typeid(void)};
    QueryMetadata metadata{};
};

class IQueryProvider
{
  public:
    virtual ~IQueryProvider() = default;
    [[nodiscard]] virtual QueryTypeId Type() const noexcept = 0;
    [[nodiscard]] virtual std::type_index QueryCppType() const noexcept = 0;
    [[nodiscard]] virtual std::type_index ResultCppType() const noexcept = 0;
    [[nodiscard]] virtual QueryProviderCapabilities Capabilities() const noexcept = 0;
    [[nodiscard]] virtual foundation::Result<AnyQueryResponse> Execute(const std::any& query, const QueryContext& context) const = 0;
};

template <typename TQuery> class FunctionQueryProvider final : public IQueryProvider
{
  public:
    using ResultType = typename TQuery::ResultType;
    using Handler = std::function<foundation::Result<QueryResponse<ResultType>>(const TQuery&, const QueryContext&)>;

    FunctionQueryProvider(QueryTypeId type, QueryProviderCapabilities capabilities, Handler handler)
        : type_(type), capabilities_(capabilities), handler_(std::move(handler))
    {
    }

    [[nodiscard]] QueryTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] std::type_index QueryCppType() const noexcept override { return typeid(TQuery); }
    [[nodiscard]] std::type_index ResultCppType() const noexcept override { return typeid(ResultType); }
    [[nodiscard]] QueryProviderCapabilities Capabilities() const noexcept override { return capabilities_; }

    [[nodiscard]] foundation::Result<AnyQueryResponse> Execute(const std::any& query, const QueryContext& context) const override
    {
        if (query.type() != typeid(TQuery))
        {
            return foundation::Result<AnyQueryResponse>::Failure(
                foundation::Error::Create("gameplay.query_type_mismatch", "query payload C++ type does not match provider"));
        }

        const auto result = handler_(std::any_cast<const TQuery&>(query), context);
        if (!result)
        {
            return foundation::Result<AnyQueryResponse>::Failure(result.GetError());
        }

        auto typed = result.Value();
        return foundation::Result<AnyQueryResponse>::Success(
            AnyQueryResponse{std::any(std::move(typed.value)), typeid(ResultType), typed.metadata});
    }

  private:
    QueryTypeId type_{};
    QueryProviderCapabilities capabilities_{};
    Handler handler_;
};

struct QueryServiceState
{
    std::unordered_map<QueryTypeId, std::shared_ptr<IQueryProvider>> providers;
    std::unordered_map<QueryTypeId, std::string> names;
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

    template <typename TQuery>
    [[nodiscard]] foundation::Result<void> RegisterProvider(
        std::string_view canonical_name,
        QueryProviderCapabilities capabilities,
        typename detail::FunctionQueryProvider<TQuery>::Handler handler)
    {
        if (frozen_)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "query provider registry is frozen"));
        }
        if (!handler)
        {
            return foundation::Result<void>::Failure(
                foundation::Error::Create("gameplay.invalid_query_provider", "query provider handler must be valid"));
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

        state_->providers.emplace(expected, std::make_shared<detail::FunctionQueryProvider<TQuery>>(expected, capabilities, std::move(handler)));
        state_->names.emplace(expected, std::string(canonical_name));
        return foundation::Result<void>::Success();
    }

    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t ProviderCount() const noexcept { return state_->providers.size(); }

    template <typename TQuery>
    [[nodiscard]] foundation::Result<QueryResponse<typename TQuery::ResultType>> Execute(const TQuery& query, QueryContext context) const
    {
        using ResultType = typename TQuery::ResultType;
        ++state_->executed;

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

        auto any_result = provider->Execute(std::any(query), context);
        if (!any_result)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(any_result.GetError());
        }

        auto response = std::move(any_result).Value();
        response.metadata.query_id = NextQueryId();

        if ((response.metadata.coverage == QueryCoverage::Partial && !capabilities.can_return_partial) ||
            (response.metadata.coverage == QueryCoverage::Approximate && !capabilities.can_return_approximate))
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_provider_contract_violation", "query provider returned coverage it did not declare"));
        }
        if (!CoverageSatisfies(response.metadata.coverage, context.coverage))
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_coverage_insufficient", "query result coverage does not satisfy request"));
        }
        if ((context.budget.HasResultLimit() && response.metadata.result_count > context.budget.max_results) ||
            (context.budget.HasWorkLimit() && response.metadata.work_units > context.budget.max_work_units))
        {
            ++state_->failed;
            ++state_->budget_exceeded;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_budget_exceeded", "query provider exceeded declared query budget"));
        }
        if (response.metadata.coverage == QueryCoverage::Partial || response.metadata.coverage == QueryCoverage::ResidentOnly)
        {
            ++state_->partial_results;
        }
        if (response.value_type != typeid(ResultType))
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_result_type_mismatch", "query provider returned an unexpected C++ result type"));
        }

        try
        {
            return foundation::Result<QueryResponse<ResultType>>::Success(
                QueryResponse<ResultType>{std::any_cast<ResultType>(std::move(response.value)), response.metadata});
        }
        catch (const std::bad_any_cast&)
        {
            ++state_->failed;
            return foundation::Result<QueryResponse<ResultType>>::Failure(
                foundation::Error::Create("gameplay.query_result_type_mismatch", "query result payload could not be cast to expected type"));
        }
    }

    template <typename TQuery>
    [[nodiscard]] std::shared_future<foundation::Result<QueryResponse<typename TQuery::ResultType>>> SubmitSnapshot(
        TQuery query,
        QueryContext context,
        core::tasks::ITaskScheduler& scheduler) const
    {
        using ReturnType = foundation::Result<QueryResponse<typename TQuery::ResultType>>;
        context.consistency = QueryConsistency::Snapshot;
        auto promise = std::make_shared<std::promise<ReturnType>>();
        auto future = promise->get_future().share();
        const auto state = state_;
        const auto task = scheduler.Schedule(
            [state, promise, query = std::move(query), context]() mutable {
                try
                {
                    promise->set_value(ExecuteFromState<TQuery>(state, query, context));
                }
                catch (...)
                {
                    promise->set_exception(std::current_exception());
                }
            },
            "GameplayQuerySnapshot");
        if (!task.IsValid() && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            promise->set_value(ReturnType::Failure(foundation::Error::Create(
                "gameplay.query_schedule_failed", "scheduler rejected async query task")));
        }
        return future;
    }

    [[nodiscard]] QueryDiagnostics GetDiagnostics() const noexcept
    {
        return QueryDiagnostics{state_->executed.load(), state_->failed.load(), state_->budget_exceeded.load(), state_->partial_results.load()};
    }

    [[nodiscard]] static bool IsCursorStale(const QueryCursor& cursor, Revision current_revision) noexcept
    {
        return cursor.IsValid() && cursor.revision != current_revision;
    }

  private:
    [[nodiscard]] QueryId NextQueryId() const noexcept;

    template <typename TQuery>
    [[nodiscard]] static foundation::Result<QueryResponse<typename TQuery::ResultType>> ExecuteFromState(
        const std::shared_ptr<detail::QueryServiceState>& state,
        const TQuery& query,
        QueryContext context)
    {
        GameplayQueryService executor;
        executor.state_ = state;
        executor.frozen_ = true;
        return executor.Execute(query, context);
    }
    [[nodiscard]] static bool CoverageSatisfies(QueryCoverage actual, QueryCoverageRequirement required) noexcept;

    std::shared_ptr<detail::QueryServiceState> state_ = std::make_shared<detail::QueryServiceState>();
    bool frozen_ = false;
};
} // namespace epidemic::gameplay::queries





