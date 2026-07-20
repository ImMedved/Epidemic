#pragma once

#include <Epidemic/Foundation/error.h>

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace epidemic::foundation
{
// This file defines the core success/failure transport used by EngineBase public APIs.
// Result<T> carries either a success payload or an Error and is preferred for expected
// runtime failures such as platform, input, window, and RHI operations.

// Represents either a successful TValue or an Error.
// Relationship: used by most lower-level contracts that should not throw on expected failures.
template <typename TValue> class Result
{
  public:
    static_assert(!std::is_reference_v<TValue>, "Result does not support reference types");

    // Wraps a success payload into Result<TValue>.
    // Input: owned success value.
    // Output: successful result instance.
    [[nodiscard]] static Result Success(TValue value)
    {
        return Result(std::move(value));
    }

    // Wraps an Error into Result<TValue>.
    // Input: owned failure payload.
    // Output: failed result instance.
    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(std::move(error));
    }

    // Returns true when the result currently stores TValue.
    [[nodiscard]] bool HasValue() const noexcept
    {
        return std::holds_alternative<TValue>(storage_);
    }

    // Convenience boolean conversion for `if (result)` style checks.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    // Returns a mutable reference to the success value.
    // Throws if the result currently stores an Error.
    [[nodiscard]] TValue &Value() &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }

        return std::get<TValue>(storage_);
    }

    // Returns a read-only reference to the success value.
    // Throws if the result currently stores an Error.
    [[nodiscard]] const TValue &Value() const &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }

        return std::get<TValue>(storage_);
    }

    // Moves the success value out of the result.
    // Relationship: this overload is important for move-only or large payload types.
    [[nodiscard]] TValue &&Value() &&
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }

        return std::get<TValue>(std::move(storage_));
    }

    // Returns a mutable reference to the stored Error.
    // Throws if the result currently stores a success value.
    [[nodiscard]] Error &GetError() &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }

        return std::get<Error>(storage_);
    }

    // Returns a read-only reference to the stored Error.
    // Throws if the result currently stores a success value.
    [[nodiscard]] const Error &GetError() const &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }

        return std::get<Error>(storage_);
    }

  private:
    // Stores the success payload variant branch.
    explicit Result(TValue value) : storage_(std::move(value))
    {
    }

    // Stores the failure payload variant branch.
    explicit Result(Error error) : storage_(std::move(error))
    {
    }

    std::variant<TValue, Error> storage_;
};

// Specialization for operations that succeed/fail without an additional payload.
template <> class Result<void>
{
  public:
    // Builds a successful Result<void>.
    [[nodiscard]] static Result Success()
    {
        return Result(true, Error{});
    }

    // Builds a failed Result<void>.
    [[nodiscard]] static Result Failure(Error error)
    {
        return Result(false, std::move(error));
    }

    // Returns true when the represented operation succeeded.
    [[nodiscard]] bool HasValue() const noexcept
    {
        return has_value_;
    }

    // Convenience boolean conversion for `if (result)` style checks.
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return HasValue();
    }

    // Validates that the result is successful.
    // Throws if the represented operation failed.
    // Relationship: mirrors Value() from the generic specialization.
    void Value() const
    {
        if (!has_value_)
        {
            throw std::runtime_error("Result<void> does not contain success");
        }
    }

    // Returns the stored failure payload.
    // Throws if the represented operation actually succeeded.
    [[nodiscard]] const Error &GetError() const
    {
        if (has_value_)
        {
            throw std::runtime_error("Result<void> does not contain an error");
        }

        return error_;
    }

  private:
    // Internal constructor used by the static factories above.
    Result(bool has_value, Error error) : has_value_(has_value), error_(std::move(error))
    {
    }

    bool has_value_{false};
    Error error_;
};
} 