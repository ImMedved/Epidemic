#include "../test_assert.h"

#include <Epidemic/Foundation/error.h>
#include <Epidemic/Foundation/handle.h>
#include <Epidemic/Foundation/path.h>
#include <Epidemic/Foundation/result.h>
#include <Epidemic/Foundation/string_id.h>
#include <Epidemic/Foundation/time.h>

#include <cmath>
#include <unordered_set>

namespace
{
using epidemic::tests::Assert;

bool NearlyEqual(double left, double right, double epsilon = 1e-9)
{
    return std::abs(left - right) <= epsilon;
}

void TestFoundationPrimitives()
{
    const auto ok = epidemic::foundation::Result<int>::Success(42);
    Assert(ok.HasValue(), "Result<int>::Success must hold a value");
    Assert(ok.Value() == 42, "Successful result must return stored value");

    const auto failure = epidemic::foundation::Result<int>::Failure(
        epidemic::foundation::Error::Create("test.failure", "Failure path", "foundation.tests"));
    Assert(!failure.HasValue(), "Result<int>::Failure must not hold a value");
    Assert(failure.GetError().HasCode("test.failure"), "Failure result must expose stored error code");
    Assert(failure.GetError().HasMessage(), "Failure result must expose a message");

    const auto path = epidemic::foundation::Path::FromString("resource\\textures\\..\\models");
    Assert(path.GenericString() == "resource/models", "Path must normalize separators and dot segments");
    Assert(path.Join("ship").GenericString() == "resource/models/ship", "Path::Join must append child segment");

    constexpr auto empty_string_id = epidemic::foundation::StringId::FromString("");
    constexpr auto first_string_id = epidemic::foundation::StringId::FromString("alpha");
    constexpr auto second_string_id = epidemic::foundation::StringId::FromString("alpha");
    static_assert(!empty_string_id.IsValid());
    static_assert(first_string_id == second_string_id);

    const auto module_id = epidemic::foundation::ModuleId::FromString("core.module");
    const auto duplicate_module_id = epidemic::foundation::ModuleId::FromString("core.module");
    Assert(module_id.IsValid(), "ModuleId must be valid for non-empty text");
    Assert(module_id == duplicate_module_id, "ModuleId generation must be stable");

    struct TextureTag
    {
    };
    const epidemic::foundation::Handle<TextureTag> valid_handle(7, 3);
    Assert(valid_handle.IsValid(), "Explicit handle must be valid");

    const auto frame_time_from_seconds = epidemic::foundation::FrameTime::FromSeconds(0.5);
    const auto frame_time_from_milliseconds = epidemic::foundation::FrameTime::FromMilliseconds(16.5);
    Assert(NearlyEqual(frame_time_from_seconds.Milliseconds(), 500.0), "FrameTime seconds conversion must work");
    Assert(NearlyEqual(frame_time_from_milliseconds.Seconds(), 0.0165), "FrameTime millisecond conversion must work");

    epidemic::foundation::FrameIndex frame_index;
    ++frame_index;
    Assert(frame_index.Value() == 1, "FrameIndex increment must work");
}
}

int main()
{
    return epidemic::tests::RunNamedTests({{"FoundationPrimitives", &TestFoundationPrimitives}});
}