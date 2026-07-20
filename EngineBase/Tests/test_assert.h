#pragma once

#include <exception>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace epidemic::tests
{
// This file defines the tiny assertion and named-test runner helpers shared by EngineBase tests.
// The helpers keep the baseline test harness dependency-free while still producing readable failures.

struct NamedTest
{
    std::string_view name;
    void (*function)();
};

// Throws when a test condition is false.
inline void Assert(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(message));
    }
}

// Runs a list of named tests, prints progress to stdout/stderr, and returns a process exit code.
inline int RunNamedTests(std::initializer_list<NamedTest> tests)
{
    try
    {
        for (const auto &test : tests)
        {
            test.function();
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