// This file exercises the Diagnostics baseline contracts such as logging, counters, profiling, and thread naming.

#include "../test_assert.h"

#include <Epidemic/Diagnostics/counters.h>
#include <Epidemic/Diagnostics/logger.h>
#include <Epidemic/Diagnostics/profiling.h>
#include <Epidemic/Diagnostics/thread_context.h>

#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using epidemic::tests::Assert;

class RecordingLogger final : public epidemic::diagnostics::ILogger
{
  public:
    void Log(const epidemic::diagnostics::LogMessage &message) override
    {
        records.push_back(std::string(message.module_name) + ":" + std::string(message.category) + ":" +
                          std::string(message.message));
    }

    std::vector<std::string> records;
};

class ThrowingProfileCollector final : public epidemic::diagnostics::IProfileCollector
{
  public:
    void Record(epidemic::diagnostics::ProfileEvent) override
    {
        ++record_attempts;
        throw std::runtime_error("collector failure");
    }

    int record_attempts{0};
};

// Verifies the baseline diagnostics services and helper primitives.
void TestDiagnosticsBaseline()
{
    epidemic::diagnostics::GlobalCounters().Reset();
    epidemic::diagnostics::SetCurrentThreadName("DiagnosticsTest");

    RecordingLogger logger;
    logger.Info("Diagnostics", "Logger", "baseline ready");
    Assert(logger.records.size() == 1, "Logger must receive messages");

    auto collector = std::make_shared<epidemic::diagnostics::InMemoryProfileCollector>();
    epidemic::diagnostics::SetProfileCollector(collector);
    epidemic::diagnostics::SetProfilingEnabled(true);
    {
        EPIDEMIC_PROFILE_SCOPE("DiagnosticsScope");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Assert(collector->Snapshot().size() == 1, "Profiling scope must record an event");

    epidemic::diagnostics::GlobalCounters().Increment(epidemic::diagnostics::CounterId::Frames);
    epidemic::diagnostics::GlobalCounters().Increment(epidemic::diagnostics::CounterId::TasksScheduled, 3);
    epidemic::diagnostics::GlobalCounters().Decrement(epidemic::diagnostics::CounterId::TasksScheduled);
    epidemic::diagnostics::GlobalCounters().Set(epidemic::diagnostics::CounterId::FrameTimeMicros, 16667);
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::Frames) == 1,
           "Diagnostics counters must increment correctly");
    Assert(epidemic::diagnostics::GlobalCounters().Get(epidemic::diagnostics::CounterId::TasksScheduled) == 2,
           "Diagnostics counters must support decrement");

    epidemic::diagnostics::SetProfileCollector(nullptr);
    epidemic::diagnostics::SetProfilingEnabled(false);
    {
        EPIDEMIC_PROFILE_SCOPE("DisabledDiagnosticsScope");
    }
    epidemic::diagnostics::SetProfilingEnabled(true);
}

void TestProfileScopeContainsCollectorExceptions()
{
    auto collector = std::make_shared<ThrowingProfileCollector>();
    epidemic::diagnostics::SetProfileCollector(collector);
    epidemic::diagnostics::SetProfilingEnabled(true);
    {
        EPIDEMIC_PROFILE_SCOPE("ThrowingCollectorScope");
    }
    Assert(collector->record_attempts == 1, "ProfileScope must attempt to emit the completed event");

    bool original_exception_observed = false;
    try
    {
        EPIDEMIC_PROFILE_SCOPE("ThrowingCollectorDuringUnwind");
        throw std::runtime_error("original failure");
    }
    catch (const std::runtime_error &exception)
    {
        original_exception_observed = std::string(exception.what()) == "original failure";
    }

    Assert(original_exception_observed, "ProfileScope must not replace an exception already being unwound");
    Assert(collector->record_attempts == 2, "ProfileScope must contain collector failures during unwinding");
    epidemic::diagnostics::SetProfileCollector(nullptr);
}
}

// Runs the Diagnostics unit-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"DiagnosticsBaseline", &TestDiagnosticsBaseline},
        {"ProfileScopeContainsCollectorExceptions", &TestProfileScopeContainsCollectorExceptions},
    });
}
