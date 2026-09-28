add_executable(EpidemicRuntimeFoundationTests
    runtime_foundation_tests.cpp
)

target_link_libraries(EpidemicRuntimeFoundationTests
    PRIVATE EpidemicRuntimeFoundation
)
epidemic_configure_target(EpidemicRuntimeFoundationTests)
epidemic_add_test(EpidemicRuntimeFoundationTests)


