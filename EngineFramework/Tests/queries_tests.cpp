#include "Epidemic/GameFramework/Queries/gameplay_queries.h"

#include <functional>
#include <string>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::queries;

namespace
{
struct NumberQuery
{
    using ResultType = std::vector<int>;
    int limit = 0;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.numbers"); }
};

class ImmediateScheduler final : public core::tasks::ITaskScheduler
{
  public:
    core::tasks::TaskHandle Schedule(Task task, std::string = {}) override
    {
        task();
        return {};
    }
    core::tasks::TaskHandle Schedule(Task task, core::tasks::TaskGroup&, std::string = {}) override
    {
        task();
        return {};
    }
    void Wait(const core::tasks::TaskHandle&) override {}
    void Wait(const core::tasks::TaskGroup&) override {}
    void WaitIdle() override {}
    void Shutdown() override {}
    [[nodiscard]] std::size_t WorkerCount() const noexcept override { return 0; }
    [[nodiscard]] core::tasks::TaskDiagnostics GetDiagnostics() const noexcept override { return {}; }
    [[nodiscard]] std::vector<std::string> WorkerThreadNames() const override { return {}; }
};
} // namespace

int main()
{
    GameplayQueryService service;
    QueryProviderCapabilities caps;
    caps.supports_snapshot = true;
    const auto registered = service.RegisterProvider<NumberQuery>(
        "framework.test.numbers",
        caps,
        [](const NumberQuery& query, const QueryContext& context) {
            std::vector<int> values;
            for (int i = 0; i < query.limit; ++i)
            {
                values.push_back(i);
            }
            QueryMetadata metadata;
            metadata.revision = context.consistency == QueryConsistency::Snapshot ? context.snapshot_revision : Revision{7};
            metadata.coverage = QueryCoverage::Complete;
            metadata.result_count = values.size();
            metadata.work_units = values.size();
            return foundation::Result<QueryResponse<std::vector<int>>>::Success({std::move(values), metadata});
        });
    if (!registered)
    {
        return 1;
    }
    service.Freeze();

    QueryContext context;
    context.budget.max_results = 4;
    auto result = service.Execute(NumberQuery{4}, context);
    if (!result || result.Value().value.size() != 4 || !result.Value().metadata.query_id.IsValid())
    {
        return 2;
    }

    context.budget.max_results = 3;
    result = service.Execute(NumberQuery{4}, context);
    if (result || !result.GetError().HasCode("gameplay.query_budget_exceeded"))
    {
        return 3;
    }

    ImmediateScheduler scheduler;
    QueryContext snapshot_context;
    snapshot_context.snapshot_revision = Revision{99};
    auto future = service.SubmitSnapshot(NumberQuery{2}, snapshot_context, scheduler);
    const auto async_result = future.get();
    if (!async_result || async_result.Value().metadata.revision.value != 99)
    {
        return 4;
    }

    const QueryCursor cursor{async_result.Value().metadata.query_id, Revision{99}, 10};
    if (GameplayQueryService::IsCursorStale(cursor, Revision{99}) || !GameplayQueryService::IsCursorStale(cursor, Revision{100}))
    {
        return 5;
    }

    return 0;
}
