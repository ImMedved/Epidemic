add_executable(EpidemicRuntimeAnimationTests
    animation_tests.cpp
)

target_include_directories(EpidemicRuntimeAnimationTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeAnimationTests
    PRIVATE EpidemicRuntimeAnimation
)

epidemic_configure_target(EpidemicRuntimeAnimationTests)
add_test(NAME EpidemicRuntimeAnimationTests COMMAND EpidemicRuntimeAnimationTests)
