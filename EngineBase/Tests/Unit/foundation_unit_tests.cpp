// This file exercises the complete local freeze contract for EngineBase/Foundation.

#include "../test_assert.h"

#include <Epidemic/Foundation/error.h>
#include <Epidemic/Foundation/handle.h>
#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Foundation/string_id.h>
#include <Epidemic/Foundation/time.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace
{
using epidemic::tests::Assert;
using namespace epidemic::foundation;

// Compares floating-point values with an epsilon for stable test assertions.
bool NearlyEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) <= epsilon;
}

template <typename TException, typename TCallable>
void AssertThrows(TCallable &&callable, std::string_view message)
{
    bool caught = false;
    try
    {
        std::forward<TCallable>(callable)();
    }
    catch (const TException &)
    {
        caught = true;
    }
    Assert(caught, message);
}

void TestErrorStableCode()
{
    auto error = Error::Create("foundation.stable_code", "first message", "first context");
    Assert(error.HasCode("foundation.stable_code"), "Error must preserve its exact machine-readable code");
    Assert(!error.HasCode("foundation.other_code"), "Error code matching must be exact");
    Assert(error.HasMessage(), "Error must report a non-empty message");
    Assert(error.HasContext(), "Error must report non-empty context");

    auto copied = error;
    copied.message = "message text may change independently";
    copied.context = "context may change independently";
    Assert(copied.HasCode("foundation.stable_code"), "Changing diagnostic text must not change the error code");

    auto moved = std::move(copied);
    Assert(moved.HasCode("foundation.stable_code"), "Moving Error must preserve its machine-readable code");

    const auto minimal = Error::Create("foundation.minimal", "");
    Assert(minimal.HasCode("foundation.minimal"), "Error code must not depend on message presence");
    Assert(!minimal.HasMessage(), "Empty diagnostic message must remain observable as empty");
    Assert(!minimal.HasContext(), "Omitted context must remain empty");
}

void TestResultValueErrorMoveAndMisuse()
{
    static_assert(!std::is_default_constructible_v<Result<int>>, "Result must never have an uninitialized empty state");

    auto value = Result<int>::Success(42);
    Assert(value.HasValue(), "Result<int>::Success must hold a value");
    Assert(static_cast<bool>(value), "Successful Result must convert to true");
    Assert(value.Value() == 42, "Successful Result must return its stored value");
    const auto &const_value = value;
    Assert(const_value.Value() == 42, "Const Result success access must return the stored value");
    AssertThrows<std::runtime_error>([&value] { (void)value.GetError(); },
                                     "Reading error from a successful Result must be rejected");

    auto failure = Result<int>::Failure(Error::Create("foundation.result_failure", "failure"));
    Assert(!failure.HasValue(), "Result<int>::Failure must not hold a value");
    Assert(!static_cast<bool>(failure), "Failed Result must convert to false");
    Assert(failure.GetError().HasCode("foundation.result_failure"), "Failed Result must expose the stored error code");
    const auto &const_failure = failure;
    Assert(const_failure.GetError().HasCode("foundation.result_failure"),
           "Const Result failure access must expose the stored error code");
    AssertThrows<std::runtime_error>([&failure] { (void)failure.Value(); },
                                     "Reading value from a failed Result must be rejected");

    auto move_only = Result<std::unique_ptr<int>>::Success(std::make_unique<int>(77));
    auto moved_result = std::move(move_only);
    Assert(moved_result.HasValue(), "Moving Result must preserve the active success branch");
    Assert(move_only.HasValue(), "Moved-from Result must retain a valid branch rather than become empty");
    auto moved_value = std::move(moved_result).Value();
    Assert(moved_value && *moved_value == 77, "Rvalue Value() must support move-only payload extraction");

    auto void_success = Result<void>::Success();
    Assert(void_success.HasValue(), "Result<void>::Success must represent success");
    void_success.Value();
    AssertThrows<std::runtime_error>([&void_success] { (void)void_success.GetError(); },
                                     "Result<void> success must reject error access");

    auto void_failure = Result<void>::Failure(Error::Create("foundation.void_failure", "failure"));
    Assert(!void_failure.HasValue(), "Result<void>::Failure must represent failure");
    Assert(void_failure.GetError().HasCode("foundation.void_failure"), "Result<void> must retain its failure code");
    AssertThrows<std::runtime_error>([&void_failure] { void_failure.Value(); },
                                     "Result<void> failure must reject success access");

    auto copy_assignment_target = Result<int>::Failure(Error::Create("foundation.old", "old"));
    const auto copy_assignment_source = Result<int>::Success(99);
    copy_assignment_target = copy_assignment_source;
    Assert(copy_assignment_target.HasValue() && copy_assignment_target.Value() == 99,
           "Copy assignment must atomically change a failure Result into a success Result");
    const auto failure_assignment_source = Result<int>::Failure(Error::Create("foundation.new", "new"));
    copy_assignment_target = failure_assignment_source;
    Assert(!copy_assignment_target && copy_assignment_target.GetError().HasCode("foundation.new"),
           "Copy assignment must atomically change a success Result into a failure Result");

    auto move_assignment_target =
        Result<std::unique_ptr<int>>::Failure(Error::Create("foundation.move_old", "old"));
    auto move_assignment_source = Result<std::unique_ptr<int>>::Success(std::make_unique<int>(123));
    move_assignment_target = std::move(move_assignment_source);
    Assert(move_assignment_target.HasValue() && move_assignment_target.Value() &&
               *move_assignment_target.Value() == 123,
           "Move assignment must commit a move-only success payload without an empty intermediate state");

    auto void_assignment_target = Result<void>::Success();
    const auto void_assignment_source = Result<void>::Failure(Error::Create("foundation.void_assign", "failure"));
    void_assignment_target = void_assignment_source;
    Assert(!void_assignment_target && void_assignment_target.GetError().HasCode("foundation.void_assign"),
           "Result<void> copy assignment must atomically commit the represented branch");

    struct CopyMayThrow
    {
        int value{0};
        bool *throw_on_copy{nullptr};

        CopyMayThrow(int input, bool *failure_flag) : value(input), throw_on_copy(failure_flag)
        {
        }

        CopyMayThrow(const CopyMayThrow &other) : value(other.value), throw_on_copy(other.throw_on_copy)
        {
            if (throw_on_copy != nullptr && *throw_on_copy)
            {
                throw std::runtime_error("copy failure");
            }
        }

        CopyMayThrow &operator=(const CopyMayThrow &) = default;
        CopyMayThrow(CopyMayThrow &&) noexcept = default;
        CopyMayThrow &operator=(CopyMayThrow &&) noexcept = default;
    };

    bool throw_on_copy = false;
    const auto throwing_copy_source = Result<CopyMayThrow>::Success(CopyMayThrow{7, &throw_on_copy});
    auto preserved_failure = Result<CopyMayThrow>::Failure(Error::Create("foundation.preserved", "old state"));
    throw_on_copy = true;
    AssertThrows<std::runtime_error>([&] { preserved_failure = throwing_copy_source; },
                                     "Throwing payload copy must fail before Result assignment commits");
    Assert(!preserved_failure && preserved_failure.GetError().HasCode("foundation.preserved"),
           "Failed Result copy assignment must preserve the complete destination branch");

    struct PotentiallyThrowingMove
    {
        PotentiallyThrowingMove() = default;
        PotentiallyThrowingMove(const PotentiallyThrowingMove &) = delete;
        PotentiallyThrowingMove &operator=(const PotentiallyThrowingMove &) = delete;
        PotentiallyThrowingMove(PotentiallyThrowingMove &&) noexcept(false)
        {
        }
        PotentiallyThrowingMove &operator=(PotentiallyThrowingMove &&) noexcept(false)
        {
            return *this;
        }
    };
    static_assert(!std::is_move_assignable_v<Result<PotentiallyThrowingMove>>,
                  "Unsafe payload assignment must be rejected instead of allowing variant::valueless_by_exception");
}

void TestTypedIdsInvalidStateAndTypeSafety()
{
    static_assert(!std::is_same_v<StringId, ModuleId>, "Different ID spaces must have different C++ types");
    static_assert(!std::is_convertible_v<StringId, ModuleId>, "Different typed IDs must not convert implicitly");
    static_assert(!std::is_constructible_v<ModuleId, StringId>, "Different typed IDs must not construct from each other");

    constexpr StringId default_invalid{};
    constexpr auto empty_invalid = StringId::FromString("");
    constexpr StringId explicit_invalid{0};
    static_assert(!default_invalid.IsValid());
    static_assert(default_invalid == empty_invalid);
    static_assert(default_invalid == explicit_invalid);
    static_assert(default_invalid.Raw() == 0);

    constexpr auto alpha = StringId::FromString("alpha");
    constexpr auto alpha_again = StringId::FromString("alpha");
    constexpr auto beta = StringId::FromString("beta");
    static_assert(alpha.IsValid());
    static_assert(alpha.Raw() != 0);
    static_assert(alpha == alpha_again);
    static_assert(alpha != beta);

    const auto module = ModuleId::FromString("alpha");
    Assert(module.IsValid(), "Non-empty ModuleId source must never map to the reserved invalid value");
    Assert(static_cast<bool>(module), "Valid typed ID boolean conversion must agree with IsValid");
}

void TestHashEqualityConsistency()
{
    const auto first = StringId::FromString("same");
    const auto second = StringId::FromString("same");
    Assert(first == second, "Equal typed IDs must compare equal");
    Assert(std::hash<StringId>{}(first) == std::hash<StringId>{}(second),
           "Equal typed IDs must have equal hashes");

    struct TextureTag
    {
    };
    const Handle<TextureTag> handle_a(7, 3);
    const Handle<TextureTag> handle_b(7, 3);
    const Handle<TextureTag> next_generation(7, 4);
    Assert(handle_a == handle_b, "Equal handles must compare by index and generation");
    Assert(std::hash<Handle<TextureTag>>{}(handle_a) == std::hash<Handle<TextureTag>>{}(handle_b),
           "Equal handles must have equal hashes");
    Assert(handle_a != next_generation, "Different generations must make handles unequal");
    Assert(handle_a.Packed() != next_generation.Packed(), "Packed handle identity must include generation");

    const FrameIndex frame_a(123);
    const FrameIndex frame_b(123);
    Assert(frame_a == frame_b, "Equal FrameIndex values must compare equal");
    Assert(std::hash<FrameIndex>{}(frame_a) == std::hash<FrameIndex>{}(frame_b),
           "Equal FrameIndex values must have equal hashes");

    std::unordered_set<StringId> ids;
    ids.insert(first);
    ids.insert(second);
    Assert(ids.size() == 1, "Hash/equality must coalesce duplicate typed IDs in unordered containers");
}

void TestHandleGenerationAndCanonicalInvalidState()
{
    struct TextureTag
    {
    };
    struct MeshTag
    {
    };
    static_assert(!std::is_convertible_v<Handle<TextureTag>, Handle<MeshTag>>,
                  "Handles from different identity spaces must not convert implicitly");

    const Handle<TextureTag> invalid;
    const Handle<TextureTag> explicit_invalid(Handle<TextureTag>::kInvalidIndex, 99);
    Assert(!invalid.IsValid() && !explicit_invalid.IsValid(), "Invalid handle index must always be rejected");
    Assert(invalid == explicit_invalid, "All invalid handles must canonicalize to one structural representation");
    Assert(explicit_invalid.Generation() == 0, "Canonical invalid handle generation must be zero");
    Assert(std::hash<Handle<TextureTag>>{}(invalid) == std::hash<Handle<TextureTag>>{}(explicit_invalid),
           "Canonical invalid handles must hash identically");

    std::uint32_t live_generation = 10;
    const Handle<TextureTag> original(5, live_generation);
    const auto is_current = [&live_generation](const Handle<TextureTag> &handle) {
        return handle.IsValid() && handle.Index() == 5 && handle.Generation() == live_generation;
    };
    Assert(is_current(original), "Fresh handle must match the owner generation");

    ++live_generation; // Simulates remove followed by recreation in the same slot.
    const Handle<TextureTag> recreated(5, live_generation);
    Assert(original.IsValid() && recreated.IsValid(), "Real slot handles must be valid");
    Assert(original.Index() == recreated.Index(), "Slot reuse test must target the same slot index");
    Assert(original != recreated, "A stale handle must not equal a recreated object with a newer generation");
    Assert(!is_current(original) && is_current(recreated),
           "Owner validation by full handle generation must reject stale identity after recreation");
}

void TestFrameTimeConversionsAndBoundaries()
{
    const auto seconds = FrameTime::FromSeconds(0.5);
    const auto milliseconds = FrameTime::FromMilliseconds(16.5);
    const auto negative = FrameTime::FromSeconds(-0.25);
    Assert(NearlyEqual(seconds.Milliseconds(), 500.0), "FrameTime seconds conversion must work");
    Assert(NearlyEqual(milliseconds.Seconds(), 0.0165), "FrameTime millisecond conversion must work");
    Assert(NearlyEqual(negative.Seconds(), -0.25), "FrameTime must preserve representable negative durations");

    const auto zero = FrameTime::FromDuration(Duration::zero());
    const auto minimum = FrameTime::FromDuration(Duration::min());
    const auto maximum = FrameTime::FromDuration(Duration::max());
    Assert(zero.IsZero(), "Zero Duration must remain zero");
    Assert(static_cast<Duration>(zero) == Duration::zero(), "Explicit FrameTime conversion must preserve raw duration");
    Assert(FrameTime::FromDuration(Duration{1}) == FrameTime::FromDuration(Duration{1}),
           "FrameTime equality must compare the wrapped duration");
    Assert(minimum.Raw() == Duration::min(), "Minimum native Duration must roundtrip exactly");
    Assert(maximum.Raw() == Duration::max(), "Maximum native Duration must roundtrip exactly");
    Assert(FrameTime::FromChrono(Duration::min()).Raw() == Duration::min(),
           "FromChrono must accept the minimum native Duration without lossy range checks");
    Assert(FrameTime::FromChrono(Duration::max()).Raw() == Duration::max(),
           "FromChrono must accept the maximum native Duration without lossy range checks");
    if constexpr (std::is_integral_v<Duration::rep>)
    {
        using UnsignedRep = std::make_unsigned_t<Duration::rep>;
        using UnsignedNativeDuration = std::chrono::duration<UnsignedRep, Duration::period>;
        const auto signed_max = static_cast<UnsignedRep>(std::numeric_limits<Duration::rep>::max());
        Assert(FrameTime::FromChrono(UnsignedNativeDuration{signed_max}).Raw() == Duration::max(),
               "Same-period unsigned input at the signed native maximum must convert exactly");
        if (signed_max < std::numeric_limits<UnsignedRep>::max())
        {
            AssertThrows<std::out_of_range>(
                [] { (void)FrameTime::FromChrono(UnsignedNativeDuration{signed_max + 1}); },
                "Same-period unsigned input above the signed native maximum must be rejected");
        }
    }

    AssertThrows<std::invalid_argument>(
        [] { (void)FrameTime::FromSeconds(std::numeric_limits<double>::quiet_NaN()); },
        "NaN FrameTime input must be rejected before duration_cast");
    AssertThrows<std::invalid_argument>(
        [] { (void)FrameTime::FromSeconds(std::numeric_limits<double>::infinity()); },
        "Infinite FrameTime input must be rejected before duration_cast");
    AssertThrows<std::invalid_argument>(
        [] { (void)FrameTime::FromMilliseconds(-std::numeric_limits<double>::infinity()); },
        "Negative infinite FrameTime input must be rejected before duration_cast");
    AssertThrows<std::out_of_range>(
        [] { (void)FrameTime::FromSeconds(std::numeric_limits<double>::max()); },
        "Positive out-of-range FrameTime input must be rejected");
    AssertThrows<std::out_of_range>(
        [] { (void)FrameTime::FromSeconds(-std::numeric_limits<double>::max()); },
        "Negative out-of-range FrameTime input must be rejected");
}

void TestFrameIndexCheckedArithmetic()
{
    FrameIndex frame_index;
    const auto previous = frame_index++;
    Assert(previous.Value() == 0 && frame_index.Value() == 1, "Postfix increment must return the previous frame");
    ++frame_index;
    Assert(frame_index.Value() == 2, "Prefix increment must advance the frame");
    Assert(frame_index.Next().Value() == 3, "Next must return a non-mutating successor");
    Assert(frame_index.Value() == 2, "Next must not mutate the current frame");

    const auto max_value = std::numeric_limits<std::uint64_t>::max();
    const FrameIndex max_const(max_value);
    AssertThrows<std::overflow_error>([&max_const] { (void)max_const.Next(); },
                                      "FrameIndex::Next must reject uint64 overflow");
    Assert(max_const.Value() == max_value, "Failed Next must leave the source value unchanged");

    FrameIndex prefix_overflow(max_value);
    AssertThrows<std::overflow_error>([&prefix_overflow] { ++prefix_overflow; },
                                      "Prefix increment must reject uint64 overflow");
    Assert(prefix_overflow.Value() == max_value, "Failed prefix increment must be atomic");

    FrameIndex postfix_overflow(max_value);
    AssertThrows<std::overflow_error>([&postfix_overflow] { postfix_overflow++; },
                                      "Postfix increment must reject uint64 overflow");
    Assert(postfix_overflow.Value() == max_value, "Failed postfix increment must be atomic");
}

void TestPathNormalizationAndMalformedInput()
{
    const auto empty = Path::FromString("");
    Assert(empty.Empty(), "Empty path text must produce the canonical empty path");
    Assert(empty.GenericString().empty(), "Canonical empty path must have an empty generic string");
    Assert(empty.Join("").Empty(), "Joining an empty child onto an empty path must remain empty");

#ifdef _WIN32
    const auto normalized = Path::FromString("resource\\textures\\..\\models");
    Assert(normalized.GenericString() == "resource/models", "Windows separators and dot segments must normalize");
    const auto rooted = Path::FromString("C:\\root\\folder\\..\\models");
    Assert(rooted.GenericString() == "C:/root/models", "Windows drive roots must survive lexical normalization");
#else
    const auto normalized = Path::FromString("resource/textures/../models");
    Assert(normalized.GenericString() == "resource/models", "Separators and dot segments must normalize");
    const auto rooted = Path::FromString("/root/folder/../models");
    Assert(rooted.GenericString() == "/root/models", "Root directory must survive lexical normalization");
#endif

    Assert(normalized.Join("ship/../vehicle").GenericString() == "resource/models/vehicle",
           "Join must append and lexically normalize a relative child path");
    Assert(normalized.Join("") == normalized, "Empty child join must be a semantic no-op");

    const Path unnormalized(std::filesystem::path("alpha/../beta"));
    Assert(!unnormalized.Native().empty(), "Native must expose the stored path without changing ownership");
    Assert(unnormalized.LexicallyNormal().GenericString() == "beta",
           "LexicallyNormal must normalize an explicitly materialized native path");

    AssertThrows<std::invalid_argument>([&normalized] { (void)normalized.Join("/"); },
                                        "Rooted child path must not replace the Join base path");
#ifdef _WIN32
    AssertThrows<std::invalid_argument>([&normalized] { (void)normalized.Join("C:\\replacement"); },
                                        "Drive-rooted child path must not replace the Join base path");
#endif

    const std::string malformed{"bad\0path", 8};
    AssertThrows<std::invalid_argument>(
        [&malformed] { (void)Path::FromString(std::string_view(malformed.data(), malformed.size())); },
        "Embedded NUL in path text must be rejected");
    AssertThrows<std::invalid_argument>(
        [&normalized, &malformed] { (void)normalized.Join(std::string_view(malformed.data(), malformed.size())); },
        "Embedded NUL in child path text must be rejected");
    AssertThrows<std::invalid_argument>(
        [&malformed] { (void)Path(std::filesystem::path(malformed)); },
        "Embedded NUL in an already materialized native path must be rejected");
}
}

// Runs the Foundation local-freeze unit-test group.
int main()
{
    return epidemic::tests::RunNamedTests({
        {"ErrorStableCode", &TestErrorStableCode},
        {"ResultValueErrorMoveAndMisuse", &TestResultValueErrorMoveAndMisuse},
        {"TypedIdsInvalidStateAndTypeSafety", &TestTypedIdsInvalidStateAndTypeSafety},
        {"HashEqualityConsistency", &TestHashEqualityConsistency},
        {"HandleGenerationAndCanonicalInvalidState", &TestHandleGenerationAndCanonicalInvalidState},
        {"FrameTimeConversionsAndBoundaries", &TestFrameTimeConversionsAndBoundaries},
        {"FrameIndexCheckedArithmetic", &TestFrameIndexCheckedArithmetic},
        {"PathNormalizationAndMalformedInput", &TestPathNormalizationAndMalformedInput},
    });
}
