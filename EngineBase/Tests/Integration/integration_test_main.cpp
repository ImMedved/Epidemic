#include "../test_cases.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <vector>

namespace
{
int RunAllTests()
{
    struct NamedTest
    {
        const char *name;
        void (*run)();
    };

    const std::vector<NamedTest> tests{
        {"TaskSchedulerContracts", &epidemic::tests::TestTaskSchedulerContracts},
        {"FrameLoopContracts", &epidemic::tests::TestFrameLoopContracts},
        {"ApplicationLifecycle", &epidemic::tests::TestApplicationLifecycle},
        {"PlatformRuntime", &epidemic::tests::TestPlatformRuntime},
    };

    for (const auto &test : tests)
    {
        test.run();
        std::cout << "[PASS] " << test.name << '\n';
    }

    return EXIT_SUCCESS;
}
} // namespace

int main()
{
    try
    {
        return RunAllTests();
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
