add_executable(EpidemicRuntimeIntegrationTests
    integration_tests.cpp
)

target_link_libraries(EpidemicRuntimeIntegrationTests
    PRIVATE EpidemicRuntimeSupport
)

epidemic_configure_target(EpidemicRuntimeIntegrationTests)
epidemic_add_test(EpidemicRuntimeIntegrationTests)
