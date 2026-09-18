// Diagnostics local-freeze tests. These tests deliberately exercise failure containment because diagnostics
// is observational and must never become a control-flow dependency of authoritative engine state.

#include "../test_assert.h"

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/Diagnostics/thread_context.h>

#if !defined(_MSC_VER)
#include <atomic>
#include <cstdlib>
#include <new>
#endif
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>


#if !defined(_MSC_VER)
namespace diagnostics_allocation_fault
{
std::atomic_bool fail_next{false};
}

void *operator new(std::size_t size)
{
    if (diagnostics_allocation_fault::fail_next.exchange(false, std::memory_order_relaxed))
    {
        throw std::bad_alloc();
    }
    if (void *memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
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
#endif

namespace
{
using epidemic::tests::Assert;
namespace diagnostics = epidemic::diagnostics;

class RecordingLogger final : public diagnostics::ILogger
{
  public:
    struct Record
    {
        diagnostics::LogLevel level{};
        std::string module;
        std::string category;
        std::string message;
        std::string thread_name;
    };

    std::vector<Record> records;

  private:
    void Write(const diagnostics::LogMessage &message) override
    {
        records.push_back(Record{message.level, std::string(message.module_name), std::string(message.category),
                                 std::string(message.message), message.thread_name});
    }
};

class ThrowingLogger final : public diagnostics::ILogger
{
  public:
    int write_attempts{0};

  private:
    void Write(const diagnostics::LogMessage &) override
    {
        ++write_attempts;
        throw std::runtime_error("diagnostic sink failure");
    }
};

class ThrowingProfileCollector final : public diagnostics::IProfileCollector
{
  public:
    void Record(diagnostics::ProfileEvent) override
    {
        ++record_attempts;
        throw std::runtime_error("collector failure");
    }

    int record_attempts{0};
};

void ResetGlobalDiagnostics()
{
    diagnostics::SetLoggingEnabled(true);
    diagnostics::SetProfilingEnabled(true);
    diagnostics::SetProfileCollector(nullptr);
    diagnostics::SetCurrentThreadName({});
    diagnostics::GlobalCounters().Reset();
}

void TestDiagnosticValueHelpers()
{
    ResetGlobalDiagnostics();
    Assert(diagnostics::CounterCount() == static_cast<std::size_t>(diagnostics::CounterId::Count),
           "CounterCount must match the CounterId storage sentinel");
    Assert(diagnostics::ToString(diagnostics::CounterId::Frames) == "frames" &&
               diagnostics::ToString(diagnostics::CounterId::Count) == "unknown",
           "Counter names must be stable and invalid counter enums must map to unknown");
    Assert(diagnostics::ToString(diagnostics::LogLevel::Warning) == "WARN" &&
               diagnostics::ToString(static_cast<diagnostics::LogLevel>(999)) == "UNKNOWN",
           "Log-level names must be stable and malformed enums must map to UNKNOWN");
    Assert(&diagnostics::GlobalCounters() == &diagnostics::GlobalCounters(),
           "GlobalCounters must expose one process-lifetime diagnostics registry");

    diagnostics::LogMessage default_message;
    Assert(default_message.level == diagnostics::LogLevel::Info && default_message.thread_id == std::this_thread::get_id(),
           "Default LogMessage metadata must describe the current logging call context");
    diagnostics::ProfileEvent default_profile_event;
    Assert(default_profile_event.thread_id == std::this_thread::get_id(),
           "Default ProfileEvent thread metadata must identify the constructing thread");

    auto collector = std::make_shared<diagnostics::InMemoryProfileCollector>();
    diagnostics::SetProfileCollector(collector);
    Assert(diagnostics::GetProfileCollector() == collector, "GetProfileCollector must return the installed shared collector");
    diagnostics::SetProfilingEnabled(false);
    Assert(!diagnostics::IsProfilingEnabled(), "Profiling enable state must roundtrip through its query");
    diagnostics::SetProfilingEnabled(true);
    Assert(diagnostics::IsProfilingEnabled(), "Profiling must be re-enabled without replacing the collector");
    ResetGlobalDiagnostics();
}

void TestLoggerOverloadsAndDisableContract()
{
    ResetGlobalDiagnostics();
    diagnostics::SetCurrentThreadName("DiagnosticsMain");

    RecordingLogger logger;
    logger.Log(diagnostics::LogMessage{diagnostics::LogLevel::Info, "Diagnostics", "Raw", "raw", {}, {}, "OwnedName"});
    logger.Log(diagnostics::LogLevel::Debug, "Diagnostics", "Explicit", "four args");
    logger.Log(diagnostics::LogLevel::Info, "Diagnostics", "three args");
    logger.Trace("Diagnostics", "Trace", "category");
    logger.Trace("Diagnostics", "plain");
    logger.Debug("Diagnostics", "Debug", "category");
    logger.Debug("Diagnostics", "plain");
    logger.Info("Diagnostics", "Info", "category");
    logger.Info("Diagnostics", "plain");
    logger.Warn("Diagnostics", "Warn", "category");
    logger.Warn("Diagnostics", "plain");
    logger.Error("Diagnostics", "Error", "category");
    logger.Error("Diagnostics", "plain");
    logger.Fatal("Diagnostics", "Fatal", "category");
    logger.Fatal("Diagnostics", "plain");

    Assert(logger.records.size() == 15, "Every public logger overload must dispatch exactly one record when enabled");
    Assert(logger.records.front().thread_name == "OwnedName", "LogMessage must own its thread diagnostic name");
    Assert(logger.records[1].thread_name == "DiagnosticsMain",
           "Convenience logging must capture the current thread diagnostic name");
    Assert(logger.records[3].level == diagnostics::LogLevel::Trace && logger.records[9].level == diagnostics::LogLevel::Warning &&
               logger.records[13].level == diagnostics::LogLevel::Fatal,
           "Convenience methods must preserve their declared severity");
    Assert(logger.records[4].category.empty() && logger.records[3].category == "Trace",
           "Categorized and uncategorized overloads must remain distinct contracts");

    diagnostics::SetLoggingEnabled(false);
    const auto records_before_disabled_write = logger.records.size();
    const std::string disabled_payload(4096, 'l');
    int authoritative_probe = 17;
#if !defined(_MSC_VER)
    diagnostics_allocation_fault::fail_next.store(true, std::memory_order_relaxed);
#endif
    logger.Info("Diagnostics", "Disabled", disabled_payload);
#if !defined(_MSC_VER)
    const bool disabled_logger_attempted_allocation =
        !diagnostics_allocation_fault::fail_next.exchange(false, std::memory_order_relaxed);
#endif
    ++authoritative_probe;
    Assert(!diagnostics::IsLoggingEnabled(), "Logging disable state must be observable without consulting a sink");
#if !defined(_MSC_VER)
    Assert(!disabled_logger_attempted_allocation,
           "Disabled logging must return before thread-name capture or diagnostic allocation");
#endif
    Assert(logger.records.size() == records_before_disabled_write,
           "Disabled logging must not invoke the sink or publish a diagnostic record");
    Assert(authoritative_probe == 18, "Disabled logging must not alter surrounding engine control flow or state");

    diagnostics::SetLoggingEnabled(true);
    logger.Info("Diagnostics", "Reenabled", "dispatch resumes");
    Assert(logger.records.size() == records_before_disabled_write + 1,
           "Re-enabling logging must resume dispatch without replaying skipped messages");
    ResetGlobalDiagnostics();
}

void TestLoggerSinkFailureIsContained()
{
    ResetGlobalDiagnostics();
    ThrowingLogger logger;

    int authoritative_probe = 10;
    logger.Info("Diagnostics", "Failure", "sink throws");
    authoritative_probe += 5;
    Assert(logger.write_attempts == 1, "Enabled logging must attempt the configured sink exactly once");
    Assert(authoritative_probe == 15, "A throwing logging sink must not interrupt authoritative engine mutation");

    bool original_error_observed = false;
    try
    {
        try
        {
            throw std::runtime_error("original engine failure");
        }
        catch (...)
        {
            logger.Error("Diagnostics", "FailurePath", "logging must not replace the active failure");
            throw;
        }
    }
    catch (const std::runtime_error &exception)
    {
        original_error_observed = std::string(exception.what()) == "original engine failure";
    }

    Assert(original_error_observed, "Logging from a failure path must preserve the original exception");
    Assert(logger.write_attempts == 2, "Failure-path logging must still attempt the sink once");

    diagnostics::SetLoggingEnabled(false);
    logger.Fatal("Diagnostics", "DisabledFailure", "disabled sink must not be touched");
    Assert(logger.write_attempts == 2, "A disabled logger must not invoke even a throwing sink");
    ResetGlobalDiagnostics();
}

void TestCounterArithmeticAndInvalidIds()
{
    diagnostics::DiagnosticsCounters counters;
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();

    counters.Increment(diagnostics::CounterId::Frames);
    counters.Increment(diagnostics::CounterId::Frames, 4);
    counters.Decrement(diagnostics::CounterId::Frames, 2);
    Assert(counters.Get(diagnostics::CounterId::Frames) == 3, "Counter increment/decrement must preserve exact normal arithmetic");

    counters.Set(diagnostics::CounterId::Frames, maximum - 1);
    counters.Increment(diagnostics::CounterId::Frames, 10);
    Assert(counters.Get(diagnostics::CounterId::Frames) == maximum, "Counter increment must saturate at INT64_MAX");
    counters.Increment(diagnostics::CounterId::Frames, 1);
    Assert(counters.Get(diagnostics::CounterId::Frames) == maximum, "Counter increment at INT64_MAX must remain saturated");

    counters.Set(diagnostics::CounterId::Frames, minimum + 1);
    counters.Decrement(diagnostics::CounterId::Frames, 10);
    Assert(counters.Get(diagnostics::CounterId::Frames) == minimum, "Counter decrement must saturate at INT64_MIN");
    counters.Decrement(diagnostics::CounterId::Frames, 1);
    Assert(counters.Get(diagnostics::CounterId::Frames) == minimum, "Counter decrement at INT64_MIN must remain saturated");

    counters.Set(diagnostics::CounterId::Frames, maximum - 1);
    counters.Decrement(diagnostics::CounterId::Frames, std::numeric_limits<std::int64_t>::min());
    Assert(counters.Get(diagnostics::CounterId::Frames) == maximum,
           "Subtracting INT64_MIN must saturate instead of overflowing during sign inversion");
    counters.Set(diagnostics::CounterId::Frames, minimum + 1);
    counters.Increment(diagnostics::CounterId::Frames, std::numeric_limits<std::int64_t>::min());
    Assert(counters.Get(diagnostics::CounterId::Frames) == minimum,
           "Adding INT64_MIN must saturate without undefined signed arithmetic");

    counters.Set(diagnostics::CounterId::TasksCompleted, 91);
    const auto invalid = static_cast<diagnostics::CounterId>(std::numeric_limits<std::uint8_t>::max());
    counters.Set(diagnostics::CounterId::Count, 1);
    counters.Increment(invalid, 100);
    counters.Decrement(invalid, 100);
    Assert(!diagnostics::IsValidCounterId(diagnostics::CounterId::Count) && !diagnostics::IsValidCounterId(invalid),
           "CounterId::Count and out-of-range enum values must be rejected as invalid IDs");
    Assert(counters.Get(diagnostics::CounterId::Count) == 0 && counters.Get(invalid) == 0,
           "Invalid counter queries must return the documented neutral value without out-of-bounds access");
    Assert(counters.Get(diagnostics::CounterId::TasksCompleted) == 91,
           "Invalid counter mutations must not corrupt neighboring valid counter slots");

    counters.Reset();
    Assert(counters.Get(diagnostics::CounterId::Frames) == 0 && counters.Get(diagnostics::CounterId::TasksCompleted) == 0,
           "Reset must clear all valid counters");
}

void TestInMemoryCollectorAllocationFailureIsAtomic()
{
    diagnostics::InMemoryProfileCollector collector;
#if !defined(_MSC_VER)
    diagnostics::ProfileEvent event;
    event.name = "allocation-fault";
    event.thread_name = "diagnostics-test";

    bool bad_alloc_observed = false;
    diagnostics_allocation_fault::fail_next.store(true, std::memory_order_relaxed);
    try
    {
        collector.Record(std::move(event));
    }
    catch (const std::bad_alloc &)
    {
        bad_alloc_observed = true;
    }
    diagnostics_allocation_fault::fail_next.store(false, std::memory_order_relaxed);

    Assert(bad_alloc_observed, "The targeted collector allocation fault must be observed by a direct caller");
    Assert(collector.Snapshot().empty(), "Failed InMemoryProfileCollector append must preserve the exact empty pre-state");
#endif

    collector.Record(diagnostics::ProfileEvent{"after-fault", {}, {}, "diagnostics-test"});
    Assert(collector.Snapshot().size() == 1 && collector.Snapshot()[0].name == "after-fault",
           "Collector must record and expose an appended event");
    collector.Reset();
    Assert(collector.Snapshot().empty(), "Collector reset must clear every previously recorded event");
}

void TestProfileScopeRaiiAndFailureContainment()
{
    ResetGlobalDiagnostics();
    diagnostics::SetCurrentThreadName("ProfileThread");
    auto collector = std::make_shared<diagnostics::InMemoryProfileCollector>();
    diagnostics::SetProfileCollector(collector);

    {
        EPIDEMIC_PROFILE_SCOPE("NormalScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto events = collector->Snapshot();
    Assert(events.size() == 1 && events[0].name == "NormalScope", "ProfileScope must close and publish on normal return");
    Assert(events[0].duration.count() >= 0 && events[0].thread_name == "ProfileThread",
           "ProfileScope must publish non-negative duration and an owned thread name");

    bool original_error_observed = false;
    try
    {
        EPIDEMIC_PROFILE_SCOPE("ExceptionScope");
        throw std::runtime_error("profiled operation failed");
    }
    catch (const std::runtime_error &exception)
    {
        original_error_observed = std::string(exception.what()) == "profiled operation failed";
    }
    events = collector->Snapshot();
    Assert(original_error_observed, "ProfileScope destruction during unwinding must preserve the original exception");
    Assert(events.size() == 2 && events[1].name == "ExceptionScope",
           "ProfileScope must close and publish while unwinding an exception");

    collector->Reset();
    diagnostics::SetProfilingEnabled(false);
    const std::string disabled_scope_name(4096, 'd');
#if !defined(_MSC_VER)
    diagnostics_allocation_fault::fail_next.store(true, std::memory_order_relaxed);
#endif
    {
        diagnostics::ProfileScope disabled_scope(disabled_scope_name);
    }
#if !defined(_MSC_VER)
    const bool disabled_scope_attempted_allocation =
        !diagnostics_allocation_fault::fail_next.exchange(false, std::memory_order_relaxed);
    Assert(!disabled_scope_attempted_allocation,
           "Disabled profiling must return before copying the scope name or performing diagnostic allocation");
#endif
    Assert(collector->Snapshot().empty(), "Disabled profiling must not invoke or mutate the collector");
    diagnostics::SetProfilingEnabled(true);

    auto throwing_collector = std::make_shared<ThrowingProfileCollector>();
    diagnostics::SetProfileCollector(throwing_collector);
    {
        EPIDEMIC_PROFILE_SCOPE("ThrowingCollectorScope");
    }
    Assert(throwing_collector->record_attempts == 1, "ProfileScope must attempt an enabled collector exactly once");

    original_error_observed = false;
    try
    {
        EPIDEMIC_PROFILE_SCOPE("ThrowingCollectorDuringUnwind");
        throw std::runtime_error("original profiled failure");
    }
    catch (const std::runtime_error &exception)
    {
        original_error_observed = std::string(exception.what()) == "original profiled failure";
    }
    Assert(original_error_observed, "A throwing profile collector must not replace an exception already being unwound");
    Assert(throwing_collector->record_attempts == 2, "Collector failures must be contained for every completed scope");
    ResetGlobalDiagnostics();
}

void TestProfileScopeCapturesOriginalCollector()
{
    ResetGlobalDiagnostics();
    auto original = std::make_shared<diagnostics::InMemoryProfileCollector>();
    auto replacement = std::make_shared<diagnostics::InMemoryProfileCollector>();
    diagnostics::SetProfileCollector(original);

    {
        diagnostics::ProfileScope scope("CollectorCaptureScope");
        diagnostics::SetProfileCollector(replacement);
    }

    Assert(original->Snapshot().size() == 1,
           "An active ProfileScope must retain the collector selected when the scope started");
    Assert(replacement->Snapshot().empty(),
           "Replacing the global collector must not redirect an already-started profiling event");

    {
        diagnostics::ProfileScope scope("ReplacementScope");
    }
    Assert(replacement->Snapshot().size() == 1, "New ProfileScope objects must use the replacement collector");
    ResetGlobalDiagnostics();
}

void TestThreadDiagnosticNamesOwnTheirLifetime()
{
    ResetGlobalDiagnostics();
    diagnostics::SetCurrentThreadName("short-name");
    const std::string retained_name = diagnostics::GetCurrentThreadName();
    diagnostics::SetCurrentThreadName(std::string(4096, 'x'));
    Assert(retained_name == "short-name", "A previously returned thread name must remain valid after thread-local storage changes");

    std::string worker_name;
    std::thread worker([&worker_name] {
        diagnostics::SetCurrentThreadName("worker-thread");
        worker_name = diagnostics::GetCurrentThreadName();
    });
    worker.join();

    Assert(worker_name == "worker-thread", "Each thread must observe its own diagnostic name");
    Assert(diagnostics::GetCurrentThreadName() == std::string(4096, 'x'),
           "Changing a worker thread name must not overwrite the calling thread context");

    diagnostics::SetCurrentThreadName({});
    Assert(diagnostics::GetCurrentThreadName().empty(), "Clearing a thread diagnostic name must produce an empty owning value");
    ResetGlobalDiagnostics();
}

static_assert(!std::is_copy_constructible_v<diagnostics::ProfileScope>);
static_assert(!std::is_copy_assignable_v<diagnostics::ProfileScope>);
} // namespace

int main()
{
    return epidemic::tests::RunNamedTests({
        {"DiagnosticValueHelpers", &TestDiagnosticValueHelpers},
        {"LoggerOverloadsAndDisableContract", &TestLoggerOverloadsAndDisableContract},
        {"LoggerSinkFailureIsContained", &TestLoggerSinkFailureIsContained},
        {"CounterArithmeticAndInvalidIds", &TestCounterArithmeticAndInvalidIds},
        {"InMemoryCollectorStateTransitions", &TestInMemoryCollectorAllocationFailureIsAtomic},
        {"ProfileScopeRaiiAndFailureContainment", &TestProfileScopeRaiiAndFailureContainment},
        {"ProfileScopeCapturesOriginalCollector", &TestProfileScopeCapturesOriginalCollector},
        {"ThreadDiagnosticNamesOwnTheirLifetime", &TestThreadDiagnosticNamesOwnTheirLifetime},
    });
}
