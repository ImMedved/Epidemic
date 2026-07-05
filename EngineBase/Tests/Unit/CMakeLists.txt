add_executable(EpidemicUnitTests
    unit_test_main.cpp
    ../test_cases.cpp
)

target_link_libraries(EpidemicUnitTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicCore EpidemicDiagnostics EpidemicPlatform EpidemicInput EpidemicRHI
)

epidemic_configure_target(EpidemicUnitTests)

add_test(NAME EpidemicUnitTests COMMAND EpidemicUnitTests)

