add_executable(EpidemicRuntimeEnvironmentTests
    environment_tests.cpp
)

target_link_libraries(EpidemicRuntimeEnvironmentTests
    PRIVATE EpidemicRuntimeEnvironment
)

target_include_directories(EpidemicRuntimeEnvironmentTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeEnvironmentTests)
add_test(NAME EpidemicRuntimeEnvironmentTests COMMAND EpidemicRuntimeEnvironmentTests)
