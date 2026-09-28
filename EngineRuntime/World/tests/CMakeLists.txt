add_executable(EpidemicRuntimeWorldTests
    world_tests.cpp
)

target_link_libraries(EpidemicRuntimeWorldTests
    PRIVATE EpidemicRuntimeWorld
)

target_include_directories(EpidemicRuntimeWorldTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

epidemic_configure_target(EpidemicRuntimeWorldTests)
epidemic_add_test(EpidemicRuntimeWorldTests)
