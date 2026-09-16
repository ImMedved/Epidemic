#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string_view>

namespace epidemic::freeze_oracle
{
struct Result;

Result RegisterFree(int value);
[[nodiscard]] int QueryFree(const int &value) noexcept;
[[nodiscard]] constexpr int operator|(Result, Result) noexcept;
Result BuildFree(int value = {});

class Widget
{
  public:
    Widget();
    explicit Widget(int value);
    ~Widget();

    Result Cancel(int id);
    Result Cancel(std::string_view id);
    static Widget Create();
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] Result CaptureSnapshot() const;
    [[nodiscard]] Widget Join(std::string_view child) const;
    void Mystery() &;

    template <typename TValue> Result Set(TValue &&value);
    template <typename TValue> Result Configure(TValue value = TValue{});
    template <typename TValue>
    Result Constrained(TValue value) requires requires(TValue candidate) { candidate.Valid(); };

    Result WithNestedDefault(
        std::span<const int> values = std::span<const int>{});
    void WithNestedNoexcept() noexcept(noexcept(std::span<const int>{}.empty()));

    Widget &operator=(const Widget &other);
    [[nodiscard]] bool operator==(const Widget &other) const
    {
        return this == &other;
    }

    void InlineDefinition()
    {
    }

    std::function<void()> callback;

  protected:
    void ProtectedMethod();

  private:
    void HiddenMethod();
};

struct AllmanStyle
{
    [[nodiscard]] int
    ReadValue(
        int fallback = 0) const noexcept;

    static AllmanStyle Build();
};

#define EPIDEMIC_FREEZE_ORACLE_ID(name) \
    struct name                           \
    {                                     \
        [[nodiscard]] bool IsValid() const; \
    }

EPIDEMIC_FREEZE_ORACLE_ID(OracleId);

Result BrokenDeclaration(
} // namespace epidemic::freeze_oracle
