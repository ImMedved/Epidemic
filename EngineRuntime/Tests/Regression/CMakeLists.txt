add_executable(EpidemicRuntimeRegressionTests
    regression_tests.cpp
)

target_link_libraries(EpidemicRuntimeRegressionTests
    PRIVATE EpidemicRuntimeSupport
)

epidemic_configure_target(EpidemicRuntimeRegressionTests)
add_test(NAME EpidemicRuntimeRegressionTests COMMAND EpidemicRuntimeRegressionTests)
