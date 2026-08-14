#include "Epidemic/GameFramework/Queries/gameplay_queries.h"

#include <limits>

namespace epidemic::gameplay::queries
{
QueryId GameplayQueryService::NextQueryId() const noexcept
{
    constexpr auto kScope = QueryId::FromString("framework.gameplay_queries").High();
    const auto value = next_query_id_.fetch_add(1, std::memory_order_relaxed);
    if (value == 0 || value == std::numeric_limits<std::uint64_t>::max())
    {
        return {};
    }
    return QueryId::FromRaw(kScope == 0 ? 1 : kScope, value);
}

bool GameplayQueryService::CoverageSatisfies(QueryCoverage actual, QueryCoverageRequirement required) noexcept
{
    switch (required)
    {
    case QueryCoverageRequirement::AllowApproximate:
        return true;
    case QueryCoverageRequirement::RequireResidentData:
        return actual == QueryCoverage::Complete || actual == QueryCoverage::ResidentOnly;
    case QueryCoverageRequirement::RequireComplete:
        return actual == QueryCoverage::Complete;
    }
    return false;
}
} // namespace epidemic::gameplay::queries
