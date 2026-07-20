add_executable(EpidemicRuntimeTimeTests
    time_tests.cpp
)

target_link_libraries(EpidemicRuntimeTimeTests
    PRIVATE EpidemicRuntimeTime
)

target_include_directories(EpidemicRuntimeTimeTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeTimeTests)
add_test(NAME EpidemicRuntimeTimeTests COMMAND EpidemicRuntimeTimeTests)
