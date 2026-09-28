#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace epidemic::tests::pre_state
{
enum class StateFacet
{
    PrimaryRecords,
    RecordPayloads,
    SecondaryIndexes,
    IdGenerators,
    Revisions,
    Journal,
    JournalEpoch,
    JournalSequence,
    JournalRetainedRecords,
    JournalLatestCursor,
    Diagnostics,
    Budgets,
    PendingQueues,
    ExternalCallbacks,
    PublicReadModels,
};

[[nodiscard]] inline std::string_view ToString(StateFacet facet) noexcept;

struct FacetCheck
{
    StateFacet facet{StateFacet::PrimaryRecords};
    bool passed{false};
    std::string detail;
};

struct ComparisonReport
{
    std::string scope;
    std::vector<FacetCheck> checks;

    [[nodiscard]] bool HasFacet(StateFacet facet) const noexcept
    {
        for (const auto &check : checks)
        {
            if (check.facet == facet)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool Passed() const noexcept
    {
        if (checks.empty())
        {
            return false;
        }

        for (const auto &check : checks)
        {
            if (!check.passed)
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool Covers(std::initializer_list<StateFacet> required_facets) const noexcept
    {
        for (const auto facet : required_facets)
        {
            if (!HasFacet(facet))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool Covers(std::span<const StateFacet> required_facets) const noexcept
    {
        for (const auto facet : required_facets)
        {
            if (!HasFacet(facet))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool PassedAndCovers(std::initializer_list<StateFacet> required_facets) const noexcept
    {
        const auto passed = Passed();
        const auto covers = Covers(required_facets);
        if (!passed || !covers)
        {
            std::cerr << scope << " pre-state comparison failed: passed=" << passed << " covers=" << covers << '\n';
            for (const auto &check : checks)
            {
                if (!check.passed)
                {
                    std::cerr << "  failed facet=" << ToString(check.facet) << " detail=" << check.detail << '\n';
                }
            }
        }
        return passed && covers;
    }

    [[nodiscard]] bool PassedAndCovers(std::span<const StateFacet> required_facets) const noexcept
    {
        const auto passed = Passed();
        const auto covers = Covers(required_facets);
        if (!passed || !covers)
        {
            std::cerr << scope << " pre-state comparison failed: passed=" << passed << " covers=" << covers << '\n';
            for (const auto &check : checks)
            {
                if (!check.passed)
                {
                    std::cerr << "  failed facet=" << ToString(check.facet) << " detail=" << check.detail << '\n';
                }
            }
        }
        return passed && covers;
    }
};

inline constexpr std::array RequiredMutationFacets{StateFacet::PrimaryRecords,
                                                   StateFacet::RecordPayloads,
                                                   StateFacet::SecondaryIndexes,
                                                   StateFacet::IdGenerators,
                                                   StateFacet::Revisions,
                                                   StateFacet::PublicReadModels};

inline constexpr std::array RequiredJournaledMutationFacets{StateFacet::PrimaryRecords,
                                                            StateFacet::RecordPayloads,
                                                            StateFacet::SecondaryIndexes,
                                                            StateFacet::IdGenerators,
                                                            StateFacet::Revisions,
                                                            StateFacet::Journal,
                                                            StateFacet::JournalEpoch,
                                                            StateFacet::JournalSequence,
                                                            StateFacet::JournalRetainedRecords,
                                                            StateFacet::JournalLatestCursor,
                                                            StateFacet::PublicReadModels};

inline constexpr std::array RequiredJournaledMutationFacetsWithoutIdGenerator{StateFacet::PrimaryRecords,
                                                                              StateFacet::RecordPayloads,
                                                                              StateFacet::SecondaryIndexes,
                                                                              StateFacet::Revisions,
                                                                              StateFacet::Journal,
                                                                              StateFacet::JournalEpoch,
                                                                              StateFacet::JournalSequence,
                                                                              StateFacet::JournalRetainedRecords,
                                                                              StateFacet::JournalLatestCursor,
                                                                              StateFacet::PublicReadModels};

inline constexpr std::array RequiredExternalMutationFacets{StateFacet::PrimaryRecords,
                                                           StateFacet::RecordPayloads,
                                                           StateFacet::SecondaryIndexes,
                                                           StateFacet::IdGenerators,
                                                           StateFacet::Revisions,
                                                           StateFacet::Journal,
                                                           StateFacet::JournalEpoch,
                                                           StateFacet::JournalSequence,
                                                           StateFacet::JournalRetainedRecords,
                                                           StateFacet::JournalLatestCursor,
                                                           StateFacet::ExternalCallbacks,
                                                           StateFacet::PublicReadModels};

class StateComparator
{
  public:
    explicit StateComparator(std::string scope) : report_{std::move(scope), {}}
    {
    }

    StateComparator &Require(StateFacet facet, bool condition, std::string detail)
    {
        report_.checks.push_back(FacetCheck{facet, condition, std::move(detail)});
        return *this;
    }

    template <typename TValue>
    StateComparator &RequireEqual(StateFacet facet, const TValue &before, const TValue &after, std::string detail)
    {
        return Require(facet, before == after, std::move(detail));
    }

    [[nodiscard]] const ComparisonReport &Report() const noexcept
    {
        return report_;
    }

    [[nodiscard]] ComparisonReport Finish() const &
    {
        return report_;
    }

    [[nodiscard]] ComparisonReport Finish() &&
    {
        return std::move(report_);
    }

  private:
    ComparisonReport report_;
};

[[nodiscard]] inline std::string_view ToString(StateFacet facet) noexcept
{
    switch (facet)
    {
    case StateFacet::PrimaryRecords:
        return "primary_records";
    case StateFacet::RecordPayloads:
        return "record_payloads";
    case StateFacet::SecondaryIndexes:
        return "secondary_indexes";
    case StateFacet::IdGenerators:
        return "id_generators";
    case StateFacet::Revisions:
        return "revisions";
    case StateFacet::Journal:
        return "journal";
    case StateFacet::JournalEpoch:
        return "journal_epoch";
    case StateFacet::JournalSequence:
        return "journal_sequence";
    case StateFacet::JournalRetainedRecords:
        return "journal_retained_records";
    case StateFacet::JournalLatestCursor:
        return "journal_latest_cursor";
    case StateFacet::Diagnostics:
        return "diagnostics";
    case StateFacet::Budgets:
        return "budgets";
    case StateFacet::PendingQueues:
        return "pending_queues";
    case StateFacet::ExternalCallbacks:
        return "external_callbacks";
    case StateFacet::PublicReadModels:
        return "public_read_models";
    }

    return "unknown";
}

template <typename TSnapshot>
[[nodiscard]] ComparisonReport CompareSnapshotState(std::string scope,
                                                    const TSnapshot &before,
                                                    const TSnapshot &after,
                                                    std::initializer_list<StateFacet> facets)
{
    StateComparator comparator(std::move(scope));
    for (const auto facet : facets)
    {
        comparator.RequireEqual(facet, before, after, std::string(ToString(facet)));
    }
    return std::move(comparator).Finish();
}

template <typename TCaptureBefore, typename TCaptureAfter, typename TCompare>
[[nodiscard]] ComparisonReport CompareCapturedState(std::string scope,
                                                   TCaptureBefore &&capture_before,
                                                   TCaptureAfter &&capture_after,
                                                   TCompare &&compare)
{
    auto before = capture_before();
    auto after = capture_after();
    return compare(std::move(scope), before, after);
}
} // namespace epidemic::tests::pre_state
