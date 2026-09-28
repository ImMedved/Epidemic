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
    static_assert(!std::is_same_v<std::remove_cv_t<TValue>, Error>, "Result<Error> is ambiguous and unsupported");

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
        return std::holds_alternative<TValue>(storage_.Get());
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

        return std::get<TValue>(storage_.Get());
    }

    // Returns a read-only reference to the success value.
    // Throws if the result currently stores an Error.
    [[nodiscard]] const TValue &Value() const &
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }

        return std::get<TValue>(storage_.Get());
    }

    // Moves the success value out of the result.
    // Relationship: this overload is important for move-only or large payload types.
    [[nodiscard]] TValue &&Value() &&
    {
        if (!HasValue())
        {
            throw std::runtime_error("Result does not contain a value");
        }

        return std::get<TValue>(std::move(storage_.Get()));
    }

    // Returns a mutable reference to the stored Error.
    // Throws if the result currently stores a success value.
    [[nodiscard]] Error &GetError() &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }

        return std::get<Error>(storage_.Get());
    }

    // Returns a read-only reference to the stored Error.
    // Throws if the result currently stores a success value.
    [[nodiscard]] const Error &GetError() const &
    {
        if (HasValue())
        {
            throw std::runtime_error("Result does not contain an error");
        }

        return std::get<Error>(storage_.Get());
    }

  private:
    // Guards the two-branch invariant across assignment. std::variant itself may become
    // valueless_by_exception during a throwing branch-changing assignment. Copy assignment
    // therefore stages the complete replacement and only commits when Variant move assignment
    // is statically known not to throw. Potentially throwing move assignment is disabled.
    class Storage
    {
      public:
        using Variant = std::variant<TValue, Error>;

        explicit Storage(TValue value) : value_(std::in_place_type<TValue>, std::move(value))
        {
        }

        explicit Storage(Error error) : value_(std::in_place_type<Error>, std::move(error))
        {
        }

        Storage(const Storage &) = default;
        Storage(Storage &&) noexcept(std::is_nothrow_move_constructible_v<Variant>) = default;

        Storage &operator=(const Storage &other)
            requires(std::is_copy_constructible_v<Variant> && std::is_nothrow_move_assignable_v<Variant>)
        {
            if (this != &other)
            {
                Variant staged(other.value_);
                value_ = std::move(staged);
            }
            return *this;
        }

        Storage &operator=(const Storage &)
            requires(!(std::is_copy_constructible_v<Variant> && std::is_nothrow_move_assignable_v<Variant>)) = delete;

        Storage &operator=(Storage &&) noexcept
            requires(std::is_nothrow_move_assignable_v<Variant>) = default;

        Storage &operator=(Storage &&)
            requires(!std::is_nothrow_move_assignable_v<Variant>) = delete;

        [[nodiscard]] Variant &Get() noexcept
        {
            return value_;
        }

        [[nodiscard]] const Variant &Get() const noexcept
        {
            return value_;
        }

      private:
        Variant value_;
    };

    // Stores the success payload branch.
    explicit Result(TValue value) : storage_(std::move(value))
    {
    }

    // Stores the failure payload branch.
    explicit Result(Error error) : storage_(std::move(error))
    {
    }

    Storage storage_;
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
        return storage_.HasValue();
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
        if (!HasValue())
        {
            throw std::runtime_error("Result<void> does not contain success");
        }
    }

    // Returns the stored failure payload.
    // Throws if the represented operation actually succeeded.
    [[nodiscard]] const Error &GetError() const
    {
        if (HasValue())
        {
            throw std::runtime_error("Result<void> does not contain an error");
        }

        return storage_.GetError();
    }

  private:
    // Keeps the branch flag and Error payload consistent when Error copying throws.
    class Storage
    {
      public:
        Storage(bool has_value, Error error) : has_value_(has_value), error_(std::move(error))
        {
        }

        Storage(const Storage &) = default;
        Storage(Storage &&) noexcept = default;

        Storage &operator=(const Storage &other)
        {
            if (this != &other)
            {
                Error staged(other.error_);
                static_assert(std::is_nothrow_move_assignable_v<Error>);
                error_ = std::move(staged);
                has_value_ = other.has_value_;
            }
            return *this;
        }

        Storage &operator=(Storage &&) noexcept = default;

        [[nodiscard]] bool HasValue() const noexcept
        {
            return has_value_;
        }

        [[nodiscard]] const Error &GetError() const noexcept
        {
            return error_;
        }

      private:
        bool has_value_{false};
        Error error_;
    };

    // Internal constructor used by the static factories above.
    Result(bool has_value, Error error) : storage_(has_value, std::move(error))
    {
    }

    Storage storage_;
};
} // namespace epidemic::foundation
