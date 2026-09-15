#include "mutation_fault_sweep.h"
#include "pre_state_verification.h"
#include "restore_fault_sweep.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace
{
void Check(bool condition, int code)
{
    if (!condition)
    {
        std::exit(code);
    }
}

struct SyntheticError
{
    std::string code;
};

struct SyntheticResult
{
    bool success{false};
    SyntheticError error;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return success;
    }

    [[nodiscard]] const SyntheticError &GetError() const noexcept
    {
        return error;
    }
};

struct MutationFixture
{
    int serial{0};
    int value{0};
    std::vector<int> payload;
};

struct MutationState
{
    int value{0};
    std::size_t payload_size{0};

    [[nodiscard]] bool operator==(const MutationState &) const = default;
};
} // namespace

int main()
{
    using namespace epidemic::tests;
    using allocation_fault::FailureKind;
    using allocation_fault::InvocationResult;

    Check(allocation_fault::EqualsIgnoreAsciiCase("OutOfMemory", "outofmemory"), 1);
    Check(!allocation_fault::EqualsIgnoreAsciiCase("alloc", "allocate"), 2);
    Check(allocation_fault::ContainsIgnoreAsciiCase("error.bad_ALLOC", "alloc"), 3);
    Check(allocation_fault::LooksLikeAllocationCode("memory.exhausted"), 4);
    Check(!allocation_fault::LooksLikeAllocationCode("validation.failed"), 5);

    Check(allocation_fault::ClassifyInvocation(true).failure == FailureKind::None, 6);
    Check(allocation_fault::ClassifyInvocation(false).failure == FailureKind::ControlledFailure, 7);
    Check(allocation_fault::ClassifyInvocation(SyntheticResult{false, {"allocation.failed"}}).failure ==
              FailureKind::ControlledAllocationFailure,
          8);
    Check(allocation_fault::ClassifyInvocation(SyntheticResult{false, {"validation.failed"}}).failure ==
              FailureKind::ControlledFailure,
          9);

    const auto allocation_probe = allocation_fault::CountObservedAllocations([] {
        return [](allocation_fault::SweepRunContext &context) {
            context.MarkMethodInvoked();
            std::vector<int> values(8, 1);
            return values.size() == 8;
        };
    });
    Check(allocation_probe.observed_allocations > 0 && allocation_probe.invocation.method_invoked &&
              allocation_probe.invocation.failure == FailureKind::None,
          10);

    int bounded_value = 0;
    const auto bounded_report = allocation_fault::RunBoundedSweep(
        "test.utilities.bounded",
        0,
        [&](allocation_fault::SweepRunContext &context) {
            context.MarkMethodInvoked();
            std::vector<int> values(8, 1);
            bounded_value = static_cast<int>(values.size());
            return InvocationResult::Success();
        },
        [&](const allocation_fault::SweepIteration &iteration) {
            return iteration.failure == FailureKind::None ? bounded_value == 8 : bounded_value == 0;
        });
    Check(bounded_report.Passed() && bounded_report.saw_failure && bounded_report.saw_success &&
              bounded_report.iterations.size() == 2 && bounded_report.iterations.front().method_invoked &&
              bounded_report.iterations.back().fault_index == -1,
          11);

    using pre_state::StateFacet;
    const MutationState unchanged{7, 3};
    const auto snapshot_report = pre_state::CompareSnapshotState(
        "test.utilities.snapshot", unchanged, unchanged, {StateFacet::PrimaryRecords, StateFacet::PublicReadModels});
    Check(snapshot_report.PassedAndCovers({StateFacet::PrimaryRecords, StateFacet::PublicReadModels}), 12);

    int capture_order = 0;
    const auto captured_report = pre_state::CompareCapturedState(
        "test.utilities.captured",
        [&] {
            Check(capture_order++ == 0, 13);
            return unchanged;
        },
        [&] {
            Check(capture_order++ == 1, 14);
            return unchanged;
        },
        [](std::string scope, const MutationState &before, const MutationState &after) {
            return pre_state::StateComparator(std::move(scope))
                .RequireEqual(StateFacet::PrimaryRecords, before.value, after.value, "value")
                .RequireEqual(StateFacet::PublicReadModels, before.payload_size, after.payload_size, "payload size")
                .Finish();
        });
    Check(capture_order == 2 &&
              captured_report.PassedAndCovers({StateFacet::PrimaryRecords, StateFacet::PublicReadModels}),
          15);
    Check(!pre_state::StateComparator("test.utilities.empty").Finish().Passed(), 16);
    Check(!pre_state::StateComparator("test.utilities.missing")
               .Require(StateFacet::PrimaryRecords, true, "present")
               .Finish()
               .Covers({StateFacet::SecondaryIndexes}),
          17);
    Check(!pre_state::StateComparator("test.utilities.mismatch")
               .RequireEqual(StateFacet::Revisions, 1, 2, "revision")
               .Finish()
               .Passed(),
          18);

    int fixtures_created = 0;
    int last_verified_serial = 0;
    int mutation_verifications = 0;
    const auto mutation_report = mutation_fault::RunObservedMutationSweep(
        "test.utilities.mutation",
        1,
        [&] { return MutationFixture{++fixtures_created, 0, {}}; },
        [](MutationFixture &fixture, allocation_fault::SweepRunContext &) {
            fixture.payload.resize(8, fixture.serial);
            fixture.value = fixture.serial;
            return InvocationResult::Success();
        },
        [](const MutationFixture &fixture) { return MutationState{fixture.value, fixture.payload.size()}; },
        [&](const allocation_fault::SweepIteration &iteration,
            const MutationState &baseline,
            const MutationFixture &fixture) {
            ++mutation_verifications;
            const bool fresh_fixture = fixture.serial > last_verified_serial;
            last_verified_serial = fixture.serial;
            if (iteration.failure == FailureKind::None)
            {
                return fresh_fixture && fixture.value == fixture.serial && fixture.payload.size() == 8;
            }
            return fresh_fixture && MutationState{fixture.value, fixture.payload.size()} == baseline;
        });
    Check(mutation_fault::PassedObservedMutationSweep(mutation_report) && mutation_report.saw_failure &&
              mutation_report.saw_injected_failure && mutation_report.iterations.back().fault_index == -1 &&
              mutation_verifications == static_cast<int>(mutation_report.iterations.size()) && fixtures_created > 2,
          19);

    int restore_value = 0;
    int targets_created = 0;
    int latest_target = 0;
    int restore_verifications = 0;
    const auto restore_report = restore_fault::RunObservedRestoreSweep(
        "test.utilities.restore",
        1,
        [&] {
            latest_target = ++targets_created;
            return std::vector<int>(8, latest_target);
        },
        [&](std::vector<int> target) {
            std::vector<int> staged = target;
            restore_value = staged.front();
            return InvocationResult::Success();
        },
        [&] { return restore_value; },
        [&](const allocation_fault::SweepIteration &iteration, int baseline) {
            ++restore_verifications;
            return iteration.failure == FailureKind::None ? restore_value == latest_target : restore_value == baseline;
        });
    Check(restore_fault::PassedObservedRestoreSweep(restore_report) && restore_report.saw_failure &&
              restore_report.saw_success && restore_report.iterations.front().method_invoked &&
              restore_verifications == static_cast<int>(restore_report.iterations.size()) && targets_created >= 3,
          20);

    const auto simple_restore_report = restore_fault::RunObservedRestoreSweep(
        "test.utilities.simple_restore",
        0,
        [] { return std::vector<int>(4, 1); },
        [](std::vector<int> target) {
            std::vector<int> staged = target;
            return staged.size() == 4;
        },
        [](const allocation_fault::SweepIteration &iteration) { return iteration.method_invoked; });
    Check(restore_fault::PassedObservedRestoreSweep(simple_restore_report), 21);

    allocation_fault::Disable();
    return 0;
}
