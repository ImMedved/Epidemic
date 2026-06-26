#pragma once

#include "foundation/error/error.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace epidemic::foundation
{
template <typename TValue> class Result
{
  public:
    static_assert(!std::is_reference_v<TValue>, "Result does not support reference types");

    [[nodiscard]] static Result Success(TValue value)
    {
        return Result(std::move(value));
    }

    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(std::move(error));
    }

    [[nodiscard]] bool HasValue() const noexcept
    {
        return std::holds_alternative<TValue>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    [[nodiscard]] TValue &Value() &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }
        return std::get<TValue>(storage_);
    }

    [[nodiscard]] const TValue &Value() const &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }
        return std::get<TValue>(storage_);
    }

    [[nodiscard]] Error &GetError() &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }
        return std::get<Error>(storage_);
    }

    [[nodiscard]] const Error &GetError() const &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }
        return std::get<Error>(storage_);
    }

  private:
    explicit Result(TValue value) : storage_(std::move(value))
    {
    }

    explicit Result(Error error) : storage_(std::move(error))
    {
    }

    std::variant<TValue, Error> storage_;
};

template <> class Result<void>
{
  public:
    [[nodiscard]] static Result Success()
    {
        return Result(true, Error{});
    }

    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(false, std::move(error));
    }

    [[nodiscard]] bool HasValue() const noexcept
    {
        return has_value_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    void Value() const
    {
        if (!has_value_)
        {
            throw std::runtime_error("Result<void> does not contain success");
        }
    }

    [[nodiscard]] const Error &GetError() const
    {
        if (has_value_)
        {
            throw std::runtime_error("Result<void> does not contain an error");
        }
        return error_;
    }

  private:
    Result(bool has_value, Error error) : has_value_(has_value), error_(std::move(error))
    {
    }

    bool has_value_{false};
    Error error_;
};
} // namespace epidemic::foundation
