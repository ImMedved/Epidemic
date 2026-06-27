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

    // Builds a successful result that owns a value.
    [[nodiscard]] static Result Success(TValue value)
    {
        return Result(std::move(value));
    }

    // Builds a failed result that owns an error description.
    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(std::move(error));
    }

    // Returns true when the result contains a value instead of an error.
    [[nodiscard]] bool HasValue() const noexcept
    {
        return std::holds_alternative<TValue>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    // Accesses the stored value or throws if the result represents a failure.
    [[nodiscard]] TValue &Value() &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }
        return std::get<TValue>(storage_);
    }

    // Accesses the stored value or throws if the result represents a failure.
    [[nodiscard]] const TValue &Value() const &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }
        return std::get<TValue>(storage_);
    }

    // Accesses the stored error or throws if the result represents success.
    [[nodiscard]] Error &GetError() &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }
        return std::get<Error>(storage_);
    }

    // Accesses the stored error or throws if the result represents success.
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
    // Builds a successful result for APIs that return only success or failure.
    [[nodiscard]] static Result Success()
    {
        return Result(true, Error{});
    }

    // Builds a failed result for APIs that have no payload on success.
    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(false, std::move(error));
    }

    // Returns true when the result represents success.
    [[nodiscard]] bool HasValue() const noexcept
    {
        return has_value_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    // Verifies success and throws if the result contains an error.
    void Value() const
    {
        if (!has_value_)
        {
            throw std::runtime_error("Result<void> does not contain success");
        }
    }

    // Returns the stored error or throws if the result is successful.
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
