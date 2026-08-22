#include "Epidemic/GameFramework/Queries/gameplay_queries.h"

#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::queries;

namespace
{
void Check(bool condition, int code)
{
    if (!condition)
    {
        std::exit(code);
    }
}

template <typename TValue> void Check(const foundation::Result<TValue>& result, int code)
{
    Check(result.HasValue(), code);
}
struct NumberQuery
{
    using ResultType = std::vector<int>;
    int limit = 0;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.numbers"); }
};

struct TextQuery
{
    using ResultType = std::string;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.text"); }
};

struct UnknownQuery
{
    using ResultType = int;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.unknown"); }
};

struct UnavailableQuery
{
    using ResultType = int;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.unavailable"); }
};

struct ApproxQuery
{
    using ResultType = int;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.approximate"); }
};

struct BadOrderQuery
{
    using ResultType = std::vector<int>;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.bad_order"); }
};

struct BudgetViolatingQuery
{
    using ResultType = std::vector<int>;
    [[nodiscard]] static constexpr QueryTypeId Type() noexcept { return QueryTypeId::FromString("framework.test.budget_violation"); }
};


class TestSnapshotCoordinator final : public IQuerySnapshotCoordinator
{
  public:
    explicit TestSnapshotCoordinator(std::shared_mutex* mutex = nullptr) : mutex_(mutex) {}

    [[nodiscard]] foundation::Result<QuerySnapshotReadEpoch> AcquireReadEpoch(GameplayTickId) const override
    {
        struct Lease final
        {
            std::optional<std::shared_lock<std::shared_mutex>> lock;
            explicit Lease(std::shared_mutex* mutex)
            {
                if (mutex != nullptr)
                {
                    lock.emplace(*mutex);
                }
            }
        };
        auto lease = std::make_shared<Lease>(mutex_);
        return foundation::Result<QuerySnapshotReadEpoch>::Success(
            QuerySnapshotReadEpoch{next_.fetch_add(1, std::memory_order_relaxed), std::move(lease)});
    }

  private:
    std::shared_mutex* mutex_ = nullptr;
    mutable std::atomic<std::uint64_t> next_{1};
};

QueryMetadata MakeMetadata(Revision revision, QueryCoverage coverage, std::uint64_t count)
{
    QueryMetadata metadata;
    metadata.revision = revision;
    metadata.coverage = coverage;
    metadata.result_count = count;
    metadata.work_units = count;
    return metadata;
}
} // namespace

int main()
{
    GameplayQueryService lifecycle;
    QueryProviderCapabilities caps;
    caps.can_return_partial = true;

    Check(lifecycle.RegisterProvider<NumberQuery>(
              "framework.test.numbers",
              caps,
              [](const NumberQuery& query, const QueryContext&) {
                  std::vector<int> values(static_cast<std::size_t>(query.limit), 1);
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success({std::move(values), MakeMetadata(Revision{1}, QueryCoverage::Complete, static_cast<std::uint64_t>(query.limit))});
              }),
          1);
    Check(!lifecycle.RegisterProvider<NumberQuery>(
              "framework.test.numbers",
              caps,
              [](const NumberQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success({NumberQuery::ResultType{}, {}});
              }),
          2);
    Check(!lifecycle.Execute(NumberQuery{1}) && lifecycle.Execute(NumberQuery{1}).GetError().HasCode("gameplay.query_registry_not_frozen"), 3);
    lifecycle.Freeze();
    Check(!lifecycle.RegisterProvider<TextQuery>(
              "framework.test.text",
              caps,
              [](const TextQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({std::string{}, {}});
              }),
          4);
    auto lifecycle_result = lifecycle.Execute(NumberQuery{2});
    Check(lifecycle_result && lifecycle_result.Value().value && lifecycle_result.Value().value->size() == 2 &&
              lifecycle_result.Value().metadata.query_id.IsValid(),
          5);
    Check(!lifecycle.Execute(UnknownQuery{}) && lifecycle.Execute(UnknownQuery{}).GetError().HasCode("gameplay.query_unknown"), 6);

    std::vector<int> numbers{1, 2, 3};
    Revision number_revision{10};
    std::string text = "alpha";
    Revision text_revision{20};

    GameplayQueryService snapshots;
    QueryProviderCapabilities snapshot_caps;
    snapshot_caps.supports_snapshot = true;
    snapshot_caps.can_return_partial = true;
    Check(snapshots.RegisterSnapshotProvider<NumberQuery, std::vector<int>>(
              "framework.test.numbers",
              snapshot_caps,
              [&numbers, &number_revision](const NumberQuery& query, const QueryContext& context) {
                  std::vector<int> values;
                  const auto wanted = static_cast<std::size_t>(query.limit);
                  const auto max_results = context.budget.HasResultLimit() ? static_cast<std::size_t>(context.budget.max_results) : wanted;
                  const auto allowed = wanted < max_results ? wanted : max_results;
                  for (std::size_t index = 0; index < allowed && index < numbers.size(); ++index)
                  {
                      values.push_back(numbers[index]);
                  }
                  const bool partial = allowed < wanted;
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success(
                      {std::move(values), MakeMetadata(number_revision, partial ? QueryCoverage::Partial : QueryCoverage::Complete, static_cast<std::uint64_t>(allowed))});
              },
              [&numbers, &number_revision](const QueryContext&) {
                  return foundation::Result<QueryProviderSnapshot<std::vector<int>>>::Success({numbers, number_revision});
              },
              [](const NumberQuery& query, const QueryContext&, const std::vector<int>& snapshot) {
                  std::vector<int> values;
                  for (std::size_t index = 0; index < static_cast<std::size_t>(query.limit) && index < snapshot.size(); ++index)
                  {
                      values.push_back(snapshot[index]);
                  }
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success(
                      {std::move(values), MakeMetadata(Revision{10}, QueryCoverage::Complete, static_cast<std::uint64_t>(values.size()))});
              }),
          7);
    Check(snapshots.RegisterSnapshotProvider<TextQuery, std::string>(
              "framework.test.text",
              snapshot_caps,
              [&text, &text_revision](const TextQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({text, MakeMetadata(text_revision, QueryCoverage::Complete, 1)});
              },
              [&text, &text_revision](const QueryContext&) {
                  return foundation::Result<QueryProviderSnapshot<std::string>>::Success({text, text_revision});
              },
              [](const TextQuery&, const QueryContext&, const std::string& snapshot) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({snapshot, MakeMetadata(Revision{20}, QueryCoverage::Complete, 1)});
              }),
          8);
    TestSnapshotCoordinator snapshot_coordinator;
    Check(snapshots.SetSnapshotCoordinator(&snapshot_coordinator), 9);
    snapshots.Freeze();

    QueryContext snapshot_context;
    snapshot_context.tick = GameplayTickId{77};
    auto snapshot = snapshots.AcquireSnapshot(snapshot_context);
    Check(snapshot && snapshot.Value().IsValid() && snapshot.Value().ProviderCount() == 2 && snapshot.Value().Tick() == GameplayTickId{77}, 9);

    numbers = {9, 8, 7};
    number_revision = Revision{11};
    text = "beta";
    text_revision = Revision{21};

    auto current_numbers = snapshots.Execute(NumberQuery{3});
    auto old_numbers = snapshots.Execute(NumberQuery{3}, snapshot.Value());
    auto current_text = snapshots.Execute(TextQuery{}, QueryContext{});
    auto old_text = snapshots.Execute(TextQuery{}, snapshot.Value());
    Check(current_numbers && current_numbers.Value().value && (*current_numbers.Value().value)[0] == 9, 10);
    Check(old_numbers && old_numbers.Value().value && (*old_numbers.Value().value)[0] == 1, 11);
    Check(current_text && current_text.Value().value && *current_text.Value().value == "beta", 12);
    Check(old_text && old_text.Value().value && *old_text.Value().value == "alpha", 13);

    GameplayQueryService missing_coordinator;
    Check(missing_coordinator.RegisterSnapshotProvider<TextQuery, std::string>(
              "framework.test.text",
              snapshot_caps,
              [](const TextQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({"current", MakeMetadata(Revision{1}, QueryCoverage::Complete, 1)});
              },
              [](const QueryContext&) {
                  return foundation::Result<QueryProviderSnapshot<std::string>>::Success({"snapshot", Revision{1}});
              },
              [](const TextQuery&, const QueryContext&, const std::string& value) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({value, MakeMetadata(Revision{1}, QueryCoverage::Complete, 1)});
              }),
          30);
    missing_coordinator.Freeze();
    auto missing_epoch = missing_coordinator.AcquireSnapshot();
    Check(!missing_epoch && missing_epoch.GetError().HasCode("gameplay.query_snapshot_coordinator_missing"), 31);

    // The coordinator lease spans every provider capture. A writer using the same read epoch
    // cannot mutate the shared state between provider snapshots.
    std::shared_mutex epoch_mutex;
    int coherent_number = 41;
    std::string coherent_text = "epoch-41";
    std::atomic<bool> first_capture_started{false};
    std::atomic<bool> writer_completed{false};
    GameplayQueryService coherent_service;
    TestSnapshotCoordinator coherent_coordinator{&epoch_mutex};
    Check(coherent_service.SetSnapshotCoordinator(&coherent_coordinator), 32);
    Check(coherent_service.RegisterSnapshotProvider<NumberQuery, int>(
              "framework.test.numbers",
              snapshot_caps,
              [](const NumberQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success({std::vector<int>{}, MakeMetadata(Revision{1}, QueryCoverage::Complete, 0)});
              },
              [&coherent_number, &first_capture_started](const QueryContext& context) {
                  Check(context.snapshot_epoch != 0, 33);
                  first_capture_started.store(true, std::memory_order_release);
                  return foundation::Result<QueryProviderSnapshot<int>>::Success({coherent_number, Revision{41}});
              },
              [](const NumberQuery&, const QueryContext&, const int& value) {
                  return foundation::Result<QueryResponse<NumberQuery::ResultType>>::Success({std::vector<int>{value}, MakeMetadata(Revision{41}, QueryCoverage::Complete, 1)});
              }),
          34);
    Check(coherent_service.RegisterSnapshotProvider<TextQuery, std::string>(
              "framework.test.text",
              snapshot_caps,
              [](const TextQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({"", MakeMetadata(Revision{1}, QueryCoverage::Complete, 1)});
              },
              [&coherent_text](const QueryContext& context) {
                  Check(context.snapshot_epoch != 0, 35);
                  std::this_thread::yield();
                  return foundation::Result<QueryProviderSnapshot<std::string>>::Success({coherent_text, Revision{41}});
              },
              [](const TextQuery&, const QueryContext&, const std::string& value) {
                  return foundation::Result<QueryResponse<TextQuery::ResultType>>::Success({value, MakeMetadata(Revision{41}, QueryCoverage::Complete, 1)});
              }),
          36);
    coherent_service.Freeze();
    std::thread writer([&]() {
        while (!first_capture_started.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        std::unique_lock lock(epoch_mutex);
        coherent_number = 42;
        coherent_text = "epoch-42";
        writer_completed.store(true, std::memory_order_release);
    });
    auto coherent_snapshot = coherent_service.AcquireSnapshot(QueryContext{GameplayTickId{99}});
    Check(coherent_snapshot, 37);
    writer.join();
    Check(writer_completed.load(std::memory_order_acquire), 38);
    auto coherent_number_result = coherent_service.Execute(NumberQuery{1}, coherent_snapshot.Value());
    auto coherent_text_result = coherent_service.Execute(TextQuery{}, coherent_snapshot.Value());
    Check(coherent_number_result && coherent_number_result.Value().value && (*coherent_number_result.Value().value)[0] == 41, 39);
    Check(coherent_text_result && coherent_text_result.Value().value && *coherent_text_result.Value().value == "epoch-41", 40);

    QueryContext partial_context;
    partial_context.requirements.require_complete = false;
    partial_context.budget.max_results = 2;
    auto partial = snapshots.Execute(NumberQuery{5}, partial_context);
    Check(partial && partial.Value().value && partial.Value().value->size() == 2 && partial.Value().metadata.coverage == QueryCoverage::Partial, 14);

    GameplayQueryService unavailable_service;
    Check(unavailable_service.RegisterProvider<UnavailableQuery>(
              "framework.test.unavailable",
              {},
              [](const UnavailableQuery&, const QueryContext&) {
                  QueryMetadata metadata;
                  metadata.coverage = QueryCoverage::Unavailable;
                  return foundation::Result<QueryResponse<UnavailableQuery::ResultType>>::Success({std::nullopt, metadata});
              }),
          15);
    unavailable_service.Freeze();
    Check(!unavailable_service.Execute(UnavailableQuery{}), 16);
    QueryContext allow_unavailable;
    allow_unavailable.requirements.require_complete = false;
    allow_unavailable.requirements.allow_unavailable = true;
    auto unavailable = unavailable_service.Execute(UnavailableQuery{}, allow_unavailable);
    Check(unavailable && !unavailable.Value().value && unavailable.Value().metadata.coverage == QueryCoverage::Unavailable, 17);

    GameplayQueryService approximate_service;
    QueryProviderCapabilities approximate_caps;
    approximate_caps.can_return_approximate = true;
    Check(approximate_service.RegisterProvider<ApproxQuery>(
              "framework.test.approximate",
              approximate_caps,
              [](const ApproxQuery&, const QueryContext&) {
                  QueryMetadata metadata = MakeMetadata(Revision{1}, QueryCoverage::Complete, 1);
                  metadata.accuracy = QueryAccuracy::Approximate;
                  return foundation::Result<QueryResponse<ApproxQuery::ResultType>>::Success({7, metadata});
              }),
          18);
    approximate_service.Freeze();
    Check(!approximate_service.Execute(ApproxQuery{}), 19);
    QueryContext allow_approximate;
    allow_approximate.requirements.allow_approximate = true;
    Check(approximate_service.Execute(ApproxQuery{}, allow_approximate), 20);

    GameplayQueryService bad_order_service;
    Check(bad_order_service.RegisterProvider<BadOrderQuery>(
              "framework.test.bad_order",
              {},
              [](const BadOrderQuery&, const QueryContext&) {
                  QueryMetadata metadata = MakeMetadata(Revision{1}, QueryCoverage::Complete, 2);
                  metadata.deterministic_order = false;
                  return foundation::Result<QueryResponse<BadOrderQuery::ResultType>>::Success({std::vector<int>{2, 1}, metadata});
              }),
          21);
    bad_order_service.Freeze();
    Check(!bad_order_service.Execute(BadOrderQuery{}) &&
              bad_order_service.Execute(BadOrderQuery{}).GetError().HasCode("gameplay.query_provider_contract_violation"),
          22);

    GameplayQueryService budget_violation_service;
    Check(budget_violation_service.RegisterProvider<BudgetViolatingQuery>(
              "framework.test.budget_violation",
              {},
              [](const BudgetViolatingQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<BudgetViolatingQuery::ResultType>>::Success(
                      {std::vector<int>{1, 2, 3}, MakeMetadata(Revision{1}, QueryCoverage::Complete, 3)});
              }),
          23);
    budget_violation_service.Freeze();
    QueryContext tiny_budget;
    tiny_budget.budget.max_results = 2;
    auto budget_violation = budget_violation_service.Execute(BudgetViolatingQuery{}, tiny_budget);
    Check(!budget_violation && budget_violation.GetError().HasCode("gameplay.query_budget_exceeded"), 24);

    GameplayQueryService id_service{std::numeric_limits<std::uint64_t>::max() - 1};
    Check(id_service.RegisterProvider<ApproxQuery>(
              "framework.test.approximate",
              {},
              [](const ApproxQuery&, const QueryContext&) {
                  return foundation::Result<QueryResponse<ApproxQuery::ResultType>>::Success({1, MakeMetadata(Revision{1}, QueryCoverage::Complete, 1)});
              }),
          25);
    id_service.Freeze();
    Check(id_service.Execute(ApproxQuery{}), 26);
    auto exhausted = id_service.Execute(ApproxQuery{});
    Check(!exhausted && exhausted.GetError().HasCode("gameplay.query_id_exhausted"), 27);

    std::atomic<int> concurrent_success{0};
    std::vector<std::thread> threads;
    for (int index = 0; index < 4; ++index)
    {
        threads.emplace_back([&snapshots, &concurrent_success]() {
            for (int iteration = 0; iteration < 16; ++iteration)
            {
                if (snapshots.Execute(NumberQuery{1}))
                {
                    ++concurrent_success;
                }
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    Check(concurrent_success == 64, 28);

    const auto diagnostics = snapshots.GetDiagnostics();
    Check(diagnostics.executed >= 68 && diagnostics.partial_results >= 1, 29);

    return 0;
}
