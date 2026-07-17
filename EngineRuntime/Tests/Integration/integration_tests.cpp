#include "Epidemic/Runtime/Support/runtime_support.h"

#include <Epidemic/Core/application.h>

#include <iostream>
#include <string_view>

namespace
{
bool Expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}
} // namespace

int main()
{
    epidemic::core::Application app{};
    const auto runtime = epidemic::runtime::RegisterDefaultEngineRuntime(app);
    bool ok = Expect(runtime.HasValue(), "default runtime composition should register");
    ok &= Expect(runtime.HasValue() && runtime.Value().registered_majors.size() == 16,
                 "default runtime should include all freeze majors");
    ok &= Expect(epidemic::runtime::GetRuntimeUpdateOrder().size() == 12, "update order should be registered");
    ok &= Expect(epidemic::runtime::GetRuntimeShutdownOrder().size() == 9, "shutdown order should be registered");
    return ok ? 0 : 1;
}
