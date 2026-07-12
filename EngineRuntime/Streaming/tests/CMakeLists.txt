add_executable(EpidemicRuntimeStreamingTests
    streaming_tests.cpp
)

target_include_directories(EpidemicRuntimeStreamingTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_link_libraries(EpidemicRuntimeStreamingTests
    PRIVATE EpidemicRuntimeStreaming
)

epidemic_configure_target(EpidemicRuntimeStreamingTests)
add_test(NAME EpidemicRuntimeStreamingTests COMMAND EpidemicRuntimeStreamingTests)
