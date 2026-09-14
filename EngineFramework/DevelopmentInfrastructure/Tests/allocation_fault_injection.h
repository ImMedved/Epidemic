#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace epidemic::tests::allocation_fault
{
inline std::atomic<long long> allocations_before_failure{-1};
inline std::atomic<bool> allocation_observation_enabled{false};
inline std::atomic<long long> observed_allocations{0};
inline std::atomic<long long> injected_failures{0};

inline void Disable() noexcept
{
    allocations_before_failure.store(-1, std::memory_order_relaxed);
}

inline void ResetInjectedFailureCount() noexcept
{
    injected_failures.store(0, std::memory_order_relaxed);
}

[[nodiscard]] inline long long InjectedFailureCount() noexcept
{
    return injected_failures.load(std::memory_order_relaxed);
}

[[nodiscard]] inline bool IsIgnoredBookkeepingAllocation(std::size_t size) noexcept
{
#if defined(_MSC_VER) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL > 0
    // MSVC checked iterators use a two-pointer node allocated inside noexcept STL
    // bookkeeping. Failing that implementation detail terminates before user code
    // can observe bad_alloc.
    if (size == sizeof(void *) * 2)
        return true;
#endif

    return false;
}

inline bool ShouldFail(std::size_t size) noexcept
{
    if (IsIgnoredBookkeepingAllocation(size))
    {
        return false;
    }

    if (allocation_observation_enabled.load(std::memory_order_relaxed))
    {
        observed_allocations.fetch_add(1, std::memory_order_relaxed);
    }

    auto remaining = allocations_before_failure.load(std::memory_order_relaxed);
    while (remaining >= 0)
    {
        if (remaining == 0)
        {
            allocations_before_failure.store(-1, std::memory_order_relaxed);
            injected_failures.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (allocations_before_failure.compare_exchange_weak(remaining, remaining - 1, std::memory_order_relaxed))
            return false;
    }
    return false;
}

class ObserveAllocations
{
  public:
    ObserveAllocations() noexcept
    {
        Disable();
        observed_allocations.store(0, std::memory_order_relaxed);
        allocation_observation_enabled.store(true, std::memory_order_relaxed);
    }

    ~ObserveAllocations()
    {
        Stop();
    }

    ObserveAllocations(const ObserveAllocations &) = delete;
    ObserveAllocations &operator=(const ObserveAllocations &) = delete;

    [[nodiscard]] long long Count() const noexcept
    {
        return observed_allocations.load(std::memory_order_relaxed);
    }

    long long Stop() noexcept
    {
        allocation_observation_enabled.store(false, std::memory_order_relaxed);
        return Count();
    }
};

class FailAfter
{
  public:
    explicit FailAfter(long long successful_allocations_before_failure) noexcept
    {
        allocations_before_failure.store(successful_allocations_before_failure, std::memory_order_relaxed);
    }
    ~FailAfter()
    {
        Disable();
    }
    FailAfter(const FailAfter &) = delete;
    FailAfter &operator=(const FailAfter &) = delete;
};

enum class FailureKind
{
    None,
    BadAlloc,
    ControlledAllocationFailure,
    ControlledFailure,
    CallbackFailure,
    UnexpectedException,
};

struct InvocationResult
{
    bool method_invoked{true};
    FailureKind failure{FailureKind::None};
    std::string detail;

    [[nodiscard]] static InvocationResult Success(bool invoked = true)
    {
        return InvocationResult{invoked, FailureKind::None, {}};
    }

    [[nodiscard]] static InvocationResult ControlledFailure(std::string detail = {})
    {
        return InvocationResult{true, FailureKind::ControlledFailure, std::move(detail)};
    }

    [[nodiscard]] static InvocationResult ControlledAllocationFailure(std::string detail = {})
    {
        return InvocationResult{true, FailureKind::ControlledAllocationFailure, std::move(detail)};
    }

    [[nodiscard]] static InvocationResult CallbackFailure(std::string detail = {})
    {
        return InvocationResult{true, FailureKind::CallbackFailure, std::move(detail)};
    }
};

struct SweepIteration
{
    std::string api;
    long long fault_index{-1};
    bool fault_injected{false};
    bool method_invoked{false};
    FailureKind failure{FailureKind::None};
    bool postcondition_met{false};
    std::string detail;
};

struct SweepRunContext
{
    long long fault_index{-1};
    bool method_invoked{false};

    void MarkMethodInvoked() noexcept
    {
        method_invoked = true;
    }
};

struct SweepReport
{
    std::string api;
    std::vector<SweepIteration> iterations;
    long long observed_allocations{-1};
    long long observed_allocation_spare{0};
    long long max_fault_index{-1};
    bool saw_success{false};
    bool saw_failure{false};
    bool saw_injected_failure{false};
    bool postconditions_met{true};
    bool exhausted_without_success{false};

    [[nodiscard]] bool Passed() const noexcept
    {
        return saw_success && postconditions_met && !exhausted_without_success;
    }
};

[[nodiscard]] inline std::string_view ToString(FailureKind failure) noexcept
{
    switch (failure)
    {
    case FailureKind::None:
        return "success";
    case FailureKind::BadAlloc:
        return "bad_alloc";
    case FailureKind::ControlledAllocationFailure:
        return "controlled_allocation_failure";
    case FailureKind::ControlledFailure:
        return "controlled_failure";
    case FailureKind::CallbackFailure:
        return "callback_failure";
    case FailureKind::UnexpectedException:
        return "unexpected_exception";
    }

    return "unknown";
}

[[nodiscard]] inline bool EqualsIgnoreAsciiCase(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto lhs = left[index] >= 'A' && left[index] <= 'Z' ? static_cast<char>(left[index] - 'A' + 'a')
                                                                  : left[index];
        const auto rhs = right[index] >= 'A' && right[index] <= 'Z' ? static_cast<char>(right[index] - 'A' + 'a')
                                                                    : right[index];
        if (lhs != rhs)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool ContainsIgnoreAsciiCase(std::string_view value, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    if (needle.size() > value.size())
    {
        return false;
    }

    for (std::size_t index = 0; index + needle.size() <= value.size(); ++index)
    {
        if (EqualsIgnoreAsciiCase(value.substr(index, needle.size()), needle))
        {
            return true;
        }
    }
    return false;
}

template <typename T>
concept ResultLike = requires(const T &result) {
    { static_cast<bool>(result) } -> std::convertible_to<bool>;
    { result.GetError().code } -> std::convertible_to<std::string>;
};

[[nodiscard]] inline bool LooksLikeAllocationCode(std::string_view code) noexcept
{
    return ContainsIgnoreAsciiCase(code, "alloc") || ContainsIgnoreAsciiCase(code, "memory") ||
           ContainsIgnoreAsciiCase(code, "bad_alloc");
}

template <typename TResult> [[nodiscard]] InvocationResult ClassifyInvocation(TResult &&result)
{
    using Decayed = std::remove_cvref_t<TResult>;

    if constexpr (std::is_same_v<Decayed, InvocationResult>)
    {
        return std::forward<TResult>(result);
    }
    else if constexpr (std::is_same_v<Decayed, bool>)
    {
        return result ? InvocationResult::Success() : InvocationResult::ControlledFailure();
    }
    else if constexpr (ResultLike<Decayed>)
    {
        if (static_cast<bool>(result))
        {
            return InvocationResult::Success();
        }

        const auto &error = result.GetError();
        if (LooksLikeAllocationCode(error.code))
        {
            return InvocationResult::ControlledAllocationFailure(error.code);
        }
        return InvocationResult::ControlledFailure(error.code);
    }
    else
    {
        return InvocationResult::Success();
    }
}

template <typename TInvoke> [[nodiscard]] InvocationResult InvokeForSweep(TInvoke &invoke, SweepRunContext &context)
{
    if constexpr (std::is_invocable_v<TInvoke &, SweepRunContext &>)
    {
        if constexpr (std::is_void_v<std::invoke_result_t<TInvoke &, SweepRunContext &>>)
        {
            invoke(context);
            return InvocationResult::Success(context.method_invoked);
        }
        else
        {
            auto invocation = ClassifyInvocation(invoke(context));
            invocation.method_invoked = invocation.method_invoked || context.method_invoked;
            return invocation;
        }
    }
    else if constexpr (std::is_invocable_v<TInvoke &, long long>)
    {
        if constexpr (std::is_void_v<std::invoke_result_t<TInvoke &, long long>>)
        {
            invoke(context.fault_index);
            return InvocationResult::Success(true);
        }
        else
        {
            return ClassifyInvocation(invoke(context.fault_index));
        }
    }
    else
    {
        static_assert(std::is_invocable_v<TInvoke &, SweepRunContext &> || std::is_invocable_v<TInvoke &, long long>,
                      "RunBoundedSweep callback must accept SweepRunContext& or long long fault index");
    }
}

template <typename TInvoke, typename TVerify>
[[nodiscard]] SweepReport RunBoundedSweep(std::string api, long long max_fault_index, TInvoke &&invoke, TVerify &&verify)
{
    SweepReport report;
    report.api = std::move(api);
    report.max_fault_index = max_fault_index;

    for (long long fault_index = 0; fault_index <= max_fault_index && !report.saw_success; ++fault_index)
    {
        SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = fault_index;
        iteration.fault_injected = true;
        SweepRunContext context{fault_index, false};

        {
            FailAfter fault(fault_index);
            try
            {
                const auto invocation = InvokeForSweep(invoke, context);
                iteration.method_invoked = invocation.method_invoked;
                iteration.failure = invocation.failure;
                iteration.detail = invocation.detail;
            }
            catch (const std::bad_alloc &)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::BadAlloc;
            }
            catch (const std::exception &exception)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::UnexpectedException;
                iteration.detail = exception.what();
            }
            catch (...)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::UnexpectedException;
            }
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration));
        report.saw_success = report.saw_success || iteration.failure == FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.iterations.push_back(std::move(iteration));
    }

    if (!report.saw_success)
    {
        SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = -1;
        iteration.fault_injected = false;
        SweepRunContext context{-1, false};

        try
        {
            const auto invocation = InvokeForSweep(invoke, context);
            iteration.method_invoked = invocation.method_invoked;
            iteration.failure = invocation.failure;
            iteration.detail = invocation.detail;
        }
        catch (const std::bad_alloc &)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::BadAlloc;
        }
        catch (const std::exception &exception)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::UnexpectedException;
            iteration.detail = exception.what();
        }
        catch (...)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::UnexpectedException;
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration));
        report.saw_success = iteration.failure == FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.exhausted_without_success = !report.saw_success;
        report.iterations.push_back(std::move(iteration));
    }

    return report;
}

template <typename TInvoke>
[[nodiscard]] SweepReport RunBoundedSweep(std::string api, long long max_fault_index, TInvoke &&invoke)
{
    return RunBoundedSweep(std::move(api), max_fault_index, std::forward<TInvoke>(invoke), [](const SweepIteration &) {
        return true;
    });
}

struct ObservedAllocationProbe
{
    long long observed_allocations{0};
    InvocationResult invocation{InvocationResult::Success(false)};
};

template <typename TMakeInvoke> [[nodiscard]] ObservedAllocationProbe CountObservedAllocations(TMakeInvoke &&make_invoke)
{
    ObservedAllocationProbe probe;
    auto invoke = make_invoke();
    SweepRunContext context{-1, false};

    try
    {
        ObserveAllocations observation;
        probe.invocation = InvokeForSweep(invoke, context);
        probe.observed_allocations = observation.Stop();
    }
    catch (const std::bad_alloc &)
    {
        probe.invocation = InvocationResult{context.method_invoked, FailureKind::BadAlloc, {}};
    }
    catch (const std::exception &exception)
    {
        probe.invocation = InvocationResult{context.method_invoked, FailureKind::UnexpectedException, exception.what()};
    }
    catch (...)
    {
        probe.invocation = InvocationResult{context.method_invoked, FailureKind::UnexpectedException, {}};
    }

    return probe;
}

template <typename TMakeInvoke, typename TVerify>
[[nodiscard]] SweepReport RunObservedBoundedSweep(std::string api,
                                                  long long observed_allocation_spare,
                                                  TMakeInvoke &&make_invoke,
                                                  TVerify &&verify)
{
    const auto probe = CountObservedAllocations(make_invoke);
    const auto spare = observed_allocation_spare < 0 ? 0 : observed_allocation_spare;
    const auto max_fault_index = probe.observed_allocations + spare;

    SweepReport report;
    report.api = std::move(api);
    report.observed_allocations = probe.observed_allocations;
    report.observed_allocation_spare = spare;
    report.max_fault_index = max_fault_index;

    for (long long fault_index = 0; fault_index <= max_fault_index && !report.saw_success; ++fault_index)
    {
        auto invoke = make_invoke();
        SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = fault_index;
        iteration.fault_injected = true;
        SweepRunContext context{fault_index, false};

        {
            FailAfter fault(fault_index);
            try
            {
                const auto invocation = InvokeForSweep(invoke, context);
                iteration.method_invoked = invocation.method_invoked;
                iteration.failure = invocation.failure;
                iteration.detail = invocation.detail;
            }
            catch (const std::bad_alloc &)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::BadAlloc;
            }
            catch (const std::exception &exception)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::UnexpectedException;
                iteration.detail = exception.what();
            }
            catch (...)
            {
                iteration.method_invoked = context.method_invoked;
                iteration.failure = FailureKind::UnexpectedException;
            }
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration));
        report.saw_success = report.saw_success || iteration.failure == FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.iterations.push_back(std::move(iteration));
    }

    if (!report.saw_success)
    {
        auto invoke = make_invoke();
        SweepIteration iteration;
        iteration.api = report.api;
        iteration.fault_index = -1;
        iteration.fault_injected = false;
        SweepRunContext context{-1, false};

        try
        {
            const auto invocation = InvokeForSweep(invoke, context);
            iteration.method_invoked = invocation.method_invoked;
            iteration.failure = invocation.failure;
            iteration.detail = invocation.detail;
        }
        catch (const std::bad_alloc &)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::BadAlloc;
        }
        catch (const std::exception &exception)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::UnexpectedException;
            iteration.detail = exception.what();
        }
        catch (...)
        {
            iteration.method_invoked = context.method_invoked;
            iteration.failure = FailureKind::UnexpectedException;
        }

        iteration.postcondition_met = static_cast<bool>(verify(iteration));
        report.saw_success = iteration.failure == FailureKind::None;
        report.saw_failure = report.saw_failure || iteration.failure != FailureKind::None;
        report.postconditions_met = report.postconditions_met && iteration.postcondition_met;
        report.exhausted_without_success = !report.saw_success;
        report.iterations.push_back(std::move(iteration));
    }

    return report;
}

template <typename TMakeInvoke>
[[nodiscard]] SweepReport RunObservedBoundedSweep(std::string api,
                                                  long long observed_allocation_spare,
                                                  TMakeInvoke &&make_invoke)
{
    return RunObservedBoundedSweep(
        std::move(api), observed_allocation_spare, std::forward<TMakeInvoke>(make_invoke), [](const SweepIteration &) {
            return true;
        });
}
} // namespace epidemic::tests::allocation_fault

void *operator new(std::size_t size)
{
    if (epidemic::tests::allocation_fault::ShouldFail(size))
        throw std::bad_alloc();
    if (void *memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void *memory) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory) noexcept
{
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void *memory, std::size_t) noexcept
{
    std::free(memory);
}
