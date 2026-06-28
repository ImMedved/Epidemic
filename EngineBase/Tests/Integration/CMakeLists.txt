add_executable(EpidemicIntegrationTests
    ../shared_test_main.cpp
)

target_link_libraries(EpidemicIntegrationTests
    PRIVATE EpidemicFoundation EpidemicCore EpidemicDiagnostics EpidemicPlatform
)

epidemic_configure_target(EpidemicIntegrationTests)

add_test(NAME EpidemicIntegrationTests COMMAND EpidemicIntegrationTests)