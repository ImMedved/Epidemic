add_executable(EpidemicIntegrationTests
    integration_test_main.cpp
    ../test_cases.cpp
)

target_link_libraries(EpidemicIntegrationTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicCore EpidemicDiagnostics EpidemicPlatform EpidemicInput EpidemicRHI
)

epidemic_configure_target(EpidemicIntegrationTests)

add_test(NAME EpidemicIntegrationTests COMMAND EpidemicIntegrationTests)

