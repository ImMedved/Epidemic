add_executable(EpidemicRuntimeNavigationTests
    navigation_tests.cpp
)

target_include_directories(EpidemicRuntimeNavigationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeNavigationTests
    PRIVATE EpidemicRuntimeNavigation
)

epidemic_configure_target(EpidemicRuntimeNavigationTests)
add_test(NAME EpidemicRuntimeNavigationTests COMMAND EpidemicRuntimeNavigationTests)
