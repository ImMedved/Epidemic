#pragma once

#include "allocation_fault_injection.h"

#include <iostream>
#include <string>
#include <utility>

namespace epidemic::tests::restore_fault
{
template <typename TMakeTarget, typename TRestore, typename TVerify>
[[nodiscard]] allocation_fault::SweepReport RunObservedRestoreSweep(std::string api,
                                                                    long long observed_allocation_spare,
                                                                    TMakeTarget &&make_target,
                                                                    TRestore &&restore,
                                                                    TVerify &&verify)
{
    return allocation_fault::RunObservedBoundedSweep(
        std::move(api),
        observed_allocation_spare,
        [&] {
            auto target = make_target();
            return [&, target = std::move(target)](allocation_fault::SweepRunContext &context) mutable {
                context.MarkMethodInvoked();
                return restore(std::move(target));
            };
        },
        std::forward<TVerify>(verify));
}

template <typename TMakeTarget, typename TRestore, typename TCaptureBaseline, typename TVerify>
[[nodiscard]] allocation_fault::SweepReport RunObservedRestoreSweep(std::string api,
                                                                    long long observed_allocation_spare,
                                                                    TMakeTarget &&make_target,
                                                                    TRestore &&restore,
                                                                    TCaptureBaseline &&capture_baseline,
                                                                    TVerify &&verify)
{
    const auto probe = allocation_fault::CountObservedAllocations([&] {
        auto target = make_target();
        return [&, target = std::move(target)](allocation_fault::SweepRunContext &context) mutable {
            context.MarkMethodInvoked();
            return restore(std::move(target));
        };
    });
    const auto baseline = capture_baseline();
    const auto spare = observed_allocation_spare < 0 ? 0 : observed_allocation_spare;

    allocation_fault::SweepReport report;
    report.api = std::move(api);
    report.observed_allocations = probe.observed_allocations;
    report.observed_allocation_spare = spare;
    report.max_fault_index = probe.observed_allocations + spare;

    for (long long fault_index = 0; fault_index <= report.max_fault_index && !report.saw_success; ++fault_index)
    {
        auto target = make_target();
        auto invoke = [&, target = std::move(target)](allocation_fault::SweepRunContext &context) mutable {
            context.MarkMethodInvoked();
            return restore(std::move(target));
        };
        allocation_fault::SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = fault_index;
        iteration.fault_injected = true;
        allocation_fault::SweepRunContext context{fault_index, false};

        {
            allocation_fault::FailAfter fault(fault_index);
            try
            {
                const auto invocation = allocation_fault::InvokeForSweep(invoke, context);
                iteration.method_invoked = invocation.method_invoked;
                iteration.failure = invocation.failure;
                iteration.detail = invocation.detail;
            }
            catch (const std::bad_alloc &)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = allocation_fault::FailureKind::BadAlloc;
            }
            catch (const std::exception &exception)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = allocation_fault::FailureKind::UnexpectedException;
                iteration.detail = exception.what();
            }
            catch (...)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = allocation_fault::FailureKind::UnexpectedException;
            }
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration, baseline));
        report.saw_success = report.saw_success || iteration.failure == allocation_fault::FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != allocation_fault::FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.iterations.push_back(std::move(iteration));
    }

    if (!report.saw_success)
    {
        auto target = make_target();
        auto invoke = [&, target = std::move(target)](allocation_fault::SweepRunContext &context) mutable {
            context.MarkMethodInvoked();
            return restore(std::move(target));
        };
        allocation_fault::SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = -1;
        iteration.fault_injected = false;
        allocation_fault::SweepRunContext context{-1, false};

        try
        {
            const auto invocation = allocation_fault::InvokeForSweep(invoke, context);
            iteration.method_invoked = invocation.method_invoked;
            iteration.failure = invocation.failure;
            iteration.detail = invocation.detail;
        }
        catch (const std::bad_alloc &)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = allocation_fault::FailureKind::BadAlloc;
        }
        catch (const std::exception &exception)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = allocation_fault::FailureKind::UnexpectedException;
            iteration.detail = exception.what();
        }
        catch (...)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = allocation_fault::FailureKind::UnexpectedException;
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration, baseline));
        report.saw_success = iteration.failure == allocation_fault::FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != allocation_fault::FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.exhausted_without_success = !report.saw_success;
        report.iterations.push_back(std::move(iteration));
    }

    return report;
}

[[nodiscard]] inline bool PassedObservedRestoreSweep(const allocation_fault::SweepReport &report) noexcept
{
    const auto passed = report.Passed() && (report.saw_failure || report.observed_allocations == 0) &&
                        report.saw_success && report.observed_allocations >= 0 &&
                        report.max_fault_index >= report.observed_allocations;
    if (!passed)
    {
        std::cerr << report.api << " restore sweep failed: observed=" << report.observed_allocations
                  << " spare=" << report.observed_allocation_spare << " max_fault_index=" << report.max_fault_index
                  << " saw_success=" << report.saw_success << " saw_failure=" << report.saw_failure
                  << " postconditions_met=" << report.postconditions_met
                  << " exhausted_without_success=" << report.exhausted_without_success << '\n';
        for (const auto &iteration : report.iterations)
        {
            if (!iteration.postcondition_met || iteration.failure == allocation_fault::FailureKind::UnexpectedException)
            {
                std::cerr << "  fault_index=" << iteration.fault_index
                          << " failure=" << allocation_fault::ToString(iteration.failure)
                          << " invoked=" << iteration.method_invoked
                          << " postcondition=" << iteration.postcondition_met << " detail=" << iteration.detail
                          << '\n';
            }
        }
    }
    return passed;
}
} // namespace epidemic::tests::restore_fault
