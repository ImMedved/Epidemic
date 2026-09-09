#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace epidemic::truth_tests
{
class Failure final : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

inline void Require(bool condition, std::string_view expression, std::string_view file, int line)
{
    if (!condition)
    {
        throw Failure(std::string{file} + ":" + std::to_string(line) + ": requirement failed: " +
                      std::string{expression});
    }
}

using TestFunction = void (*)();
using TestCase = std::pair<std::string_view, TestFunction>;

inline int Run(std::string_view suite, const std::vector<TestCase>& tests)
{
    std::size_t failed = 0;
    std::cout << "[suite] " << suite << '\n';
    for (const auto& [name, test] : tests)
    {
        try
        {
            test();
            std::cout << "  [pass] " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "  [fail] " << name << ": " << error.what() << '\n';
        }
        catch (...)
        {
            ++failed;
            std::cerr << "  [fail] " << name << ": unknown exception\n";
        }
    }
    std::cout << "[result] " << (tests.size() - failed) << '/' << tests.size() << " passed\n";
    return failed == 0 ? 0 : 1;
}
} // namespace epidemic::truth_tests

#define TRUTH_REQUIRE(expression)                                                                                       \
    ::epidemic::truth_tests::Require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)