add_executable(EpidemicUnitTests
    ../shared_test_main.cpp
)

target_link_libraries(EpidemicUnitTests
    PRIVATE EpidemicFoundation EpidemicMemory EpidemicCore EpidemicDiagnostics EpidemicPlatform
)

epidemic_configure_target(EpidemicUnitTests)

add_test(NAME EpidemicUnitTests COMMAND EpidemicUnitTests)