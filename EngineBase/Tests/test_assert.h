#pragma once

#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <iostream>

namespace epidemic::tests
{
struct TestFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

inline void Assert(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

struct NamedTest
{
    const char *name;
    void (*run)();
};

inline int RunNamedTests(std::initializer_list<NamedTest> tests)
{
    try
    {
        for (const auto &test : tests)
        {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        }

        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "[FAIL] " << exception.what() << '\n';
        return 1;
    }
}
} // namespace epidemic::tests