add_executable(EpidemicRegressionTests
    regression_test_main.cpp
    ../test_cases.cpp
)

target_link_libraries(EpidemicRegressionTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicCore EpidemicDiagnostics EpidemicPlatform EpidemicInput EpidemicRHI
)

epidemic_configure_target(EpidemicRegressionTests)

add_test(NAME EpidemicRegressionTests COMMAND EpidemicRegressionTests)

