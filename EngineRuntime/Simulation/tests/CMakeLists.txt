add_executable(EpidemicRuntimeSimulationTests
    simulation_tests.cpp
)

target_include_directories(EpidemicRuntimeSimulationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeSimulationTests
    PRIVATE EpidemicRuntimeSimulation
)

epidemic_configure_target(EpidemicRuntimeSimulationTests)
add_test(NAME EpidemicRuntimeSimulationTests COMMAND EpidemicRuntimeSimulationTests)
