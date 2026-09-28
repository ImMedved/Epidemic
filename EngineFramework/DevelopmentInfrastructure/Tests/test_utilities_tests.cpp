#include "pre_state_verification.h"

#include <cstdlib>
#include <utility>

namespace
{
void Check(bool condition, int code)
{
    if (!condition)
    {
        std::exit(code);
    }
}

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

    using pre_state::StateFacet;
    const MutationState unchanged{7, 3};
    const auto snapshot_report = pre_state::CompareSnapshotState(
        "test.utilities.snapshot", unchanged, unchanged, {StateFacet::PrimaryRecords, StateFacet::PublicReadModels});
    Check(snapshot_report.PassedAndCovers({StateFacet::PrimaryRecords, StateFacet::PublicReadModels}), 1);

    int capture_order = 0;
    const auto captured_report = pre_state::CompareCapturedState(
        "test.utilities.captured",
        [&] {
            Check(capture_order++ == 0, 2);
            return unchanged;
        },
        [&] {
            Check(capture_order++ == 1, 3);
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
          4);
    Check(!pre_state::StateComparator("test.utilities.empty").Finish().Passed(), 5);
    Check(!pre_state::StateComparator("test.utilities.missing")
               .Require(StateFacet::PrimaryRecords, true, "present")
               .Finish()
               .Covers({StateFacet::SecondaryIndexes}),
          6);
    Check(!pre_state::StateComparator("test.utilities.mismatch")
               .RequireEqual(StateFacet::Revisions, 1, 2, "revision")
               .Finish()
               .Passed(),
          7);
    return 0;
}
