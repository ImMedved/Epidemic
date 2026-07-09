add_executable(EpidemicRuntimeFoundationTests
    runtime_foundation_tests.cpp
)

target_link_libraries(EpidemicRuntimeFoundationTests
    PRIVATE EpidemicRuntimeFoundation
)

target_include_directories(EpidemicRuntimeFoundationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Resources/include"
)

epidemic_configure_target(EpidemicRuntimeFoundationTests)
add_test(NAME EpidemicRuntimeFoundationTests COMMAND EpidemicRuntimeFoundationTests)
