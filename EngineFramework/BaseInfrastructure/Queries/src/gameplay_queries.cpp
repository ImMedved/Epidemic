#include "Epidemic/GameFramework/Queries/gameplay_queries.h"

#include <limits>

namespace epidemic::gameplay::queries
{
foundation::Result<QueryId> GameplayQueryService::NextQueryId() const noexcept
{
    constexpr auto kScope = QueryId::FromString("framework.gameplay_queries").High();
    auto current = state_->next_query_id.load(std::memory_order_relaxed);
    for (;;)
    {
        if (current == 0 || current == std::numeric_limits<std::uint64_t>::max())
        {
            state_->next_query_id.store(std::numeric_limits<std::uint64_t>::max(), std::memory_order_relaxed);
            return foundation::Result<QueryId>::Failure(
                foundation::Error::Create("gameplay.query_id_exhausted", "query id generator is exhausted"));
        }

        const auto next = current + 1;
        if (state_->next_query_id.compare_exchange_weak(current, next, std::memory_order_relaxed, std::memory_order_relaxed))
        {
            return foundation::Result<QueryId>::Success(QueryId::FromRaw(kScope == 0 ? 1 : kScope, current));
        }
    }
}

foundation::Result<void> GameplayQueryService::ValidateResponse(
    const detail::AnyQueryResponse& response,
    QueryProviderCapabilities capabilities,
    const QueryContext& context) noexcept
{
    if (!response.metadata.deterministic_order)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_provider_contract_violation", "query provider returned non-deterministically ordered results"));
    }
    if (response.metadata.coverage == QueryCoverage::Partial && !capabilities.can_return_partial)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_provider_contract_violation", "query provider returned partial coverage it did not declare"));
    }
    if (response.metadata.accuracy == QueryAccuracy::Approximate && !capabilities.can_return_approximate)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_provider_contract_violation", "query provider returned approximate accuracy it did not declare"));
    }
    if (response.metadata.coverage == QueryCoverage::Unavailable && response.has_value)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_provider_contract_violation", "unavailable query response must not contain a value"));
    }
    if (response.metadata.coverage != QueryCoverage::Unavailable && !response.has_value)
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_provider_contract_violation", "available query response must contain a value"));
    }
    if (!RequirementsSatisfied(response.metadata, context.requirements))
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_coverage_insufficient", "query result does not satisfy request requirements"));
    }
    if ((context.budget.HasResultLimit() && response.metadata.result_count > context.budget.max_results) ||
        (context.budget.HasWorkLimit() && response.metadata.work_units > context.budget.max_work_units))
    {
        return foundation::Result<void>::Failure(foundation::Error::Create(
            "gameplay.query_budget_exceeded", "query provider exceeded declared query budget"));
    }
    return foundation::Result<void>::Success();
}

bool GameplayQueryService::RequirementsSatisfied(QueryMetadata metadata, QueryRequirements requirements) noexcept
{
    if (metadata.coverage == QueryCoverage::Unavailable)
    {
        return requirements.allow_unavailable;
    }
    if (requirements.require_complete && metadata.coverage != QueryCoverage::Complete)
    {
        return false;
    }
    if (!requirements.allow_approximate && metadata.accuracy == QueryAccuracy::Approximate)
    {
        return false;
    }
    return true;
}
} // namespace epidemic::gameplay::queries