add_executable(EpidemicRegressionTests
    ../shared_test_main.cpp
)

target_link_libraries(EpidemicRegressionTests
    PRIVATE EpidemicFoundation EpidemicCore EpidemicDiagnostics EpidemicPlatform
)

epidemic_configure_target(EpidemicRegressionTests)

add_test(NAME EpidemicRegressionTests COMMAND EpidemicRegressionTests)