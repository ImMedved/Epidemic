#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace epidemic::freeze_oracle
{
struct Result;

Result RegisterFree(int value);
[[nodiscard]] int QueryFree(const int &value) noexcept;

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
    void Mystery() &;

    template <typename TValue> Result Set(TValue &&value);

    Widget &operator=(const Widget &other);
    [[nodiscard]] bool operator==(const Widget &other) const = default;

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
} // namespace epidemic::freeze_oracle
