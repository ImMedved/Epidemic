add_executable(EpidemicRuntimeSupportTests
    runtime_support_tests.cpp
)

target_link_libraries(EpidemicRuntimeSupportTests
    PRIVATE EpidemicRuntimeSupport
)

epidemic_configure_target(EpidemicRuntimeSupportTests)
add_test(NAME EpidemicRuntimeSupportTests COMMAND EpidemicRuntimeSupportTests)
