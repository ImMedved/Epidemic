#pragma once

#if defined(EPIDEMIC_CORE_ENABLE_TEST_HOOKS)
namespace epidemic::core::testing
{
enum class ModuleRegistryFaultPoint
{
    None,
    BeforeRegistrationCommit,
};

void SetModuleRegistryFaultPoint(ModuleRegistryFaultPoint fault_point) noexcept;
void ClearModuleRegistryFaultPoint() noexcept;
} // namespace epidemic::core::testing
#endif
