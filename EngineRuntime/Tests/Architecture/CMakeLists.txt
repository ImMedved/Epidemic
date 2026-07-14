add_executable(EpidemicRuntimeArchitectureTests
    architecture_tests.cpp
)

target_link_libraries(EpidemicRuntimeArchitectureTests
    PRIVATE EpidemicRuntimeFoundation
    PRIVATE EpidemicRuntimeResources
)

epidemic_configure_target(EpidemicRuntimeArchitectureTests)
add_test(NAME EpidemicRuntimeArchitectureTests COMMAND EpidemicRuntimeArchitectureTests)
