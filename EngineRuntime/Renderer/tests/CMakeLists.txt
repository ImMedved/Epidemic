add_executable(EpidemicRuntimeRendererTests
    renderer_tests.cpp
)

target_include_directories(EpidemicRuntimeRendererTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeRendererTests
    PRIVATE EpidemicRuntimeRenderer
)

epidemic_configure_target(EpidemicRuntimeRendererTests)
add_test(NAME EpidemicRuntimeRendererTests COMMAND EpidemicRuntimeRendererTests)
